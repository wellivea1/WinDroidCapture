#include "frame_hub.h"
#include "microphone_client.h"
#include "preview.h"
#include "timing.h"
#include "video_queue.h"
#include "thread_class.h"
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
typedef struct Host {
    AwmHub hub;AwmMicrophone mic;HANDLE cancel,camera_thread;
    HANDLE clients[4],owner;CRITICAL_SECTION clients_lock;int rotate;unsigned scaler,framing;bool mirror,allow_missing_mic,screen;
    uint8_t *pixels,*transformed;uint64_t audio_packets;float max_peak;
    FILE *trace,*packets;uint64_t trace_session;bool trace_frames,smooth;unsigned capture_fps;AwmVideoQueue *video;AwmTiming decode,convert,arrival,source_gap;double last_packet,last_pts,report_at;
    NativePreview *converter;
} Host;
static Host host;
typedef struct PacketTiming {double arrival_ms,decode_start_ms,source_gap_ms;} PacketTiming;
static BOOL WINAPI console_stop(DWORD event){if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT){SetEvent(host.cancel);return TRUE;}return FALSE;}
static bool video_convert(const AVFrame *f,AwmVideoTiming *timing){
    uint32_t w=(uint32_t)f->width,h=(uint32_t)f->height;if(!awm_frame_shape(w,h))return false;
    if(f->format!=AV_PIX_FMT_YUV420P&&f->format!=AV_PIX_FMT_NV12&&f->format!=AV_PIX_FMT_YUVJ420P){fprintf(stderr,"Unsupported decoded pixel format %d; no silent color fallback\n",f->format);return false;}
    NativeYuvFrame decoded={0};decoded.width=w;decoded.height=h;decoded.nv12=f->format==AV_PIX_FMT_NV12;decoded.full_range=f->color_range==AVCOL_RANGE_JPEG||f->format==AV_PIX_FMT_YUVJ420P;decoded.bt709=f->colorspace==AVCOL_SPC_BT709;
    for(unsigned i=0;i<3;i++){decoded.plane[i]=f->data[i];decoded.stride[i]=f->linesize[i];}
    if(!native_yuv_bgra(&host.converter,&decoded,host.pixels)){fprintf(stderr,"GPU YUV conversion failed; no silent range/quality fallback\n");return false;}
    uint32_t ow=(host.rotate==90||host.rotate==270)?h:w,oh=(host.rotate==90||host.rotate==270)?w:h;BYTE *output=host.pixels;
    if(host.rotate||host.mirror){for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){
        uint32_t dx=x,dy=y;if(host.rotate==90){dx=h-1-y;dy=x;}else if(host.rotate==180){dx=w-1-x;dy=h-1-y;}else if(host.rotate==270){dx=y;dy=w-1-x;}if(host.mirror)dx=ow-1-dx;
        memcpy(host.transformed+((size_t)dy*ow+dx)*4,host.pixels+((size_t)y*w+x)*4,4);}
        output=host.transformed;
    }
    timing->convert_end_ms=awm_clock_ms();return awm_video_queue_push(host.video,output,ow,oh,f->pts,timing);
}
typedef struct CameraClient {HANDLE pipe;} CameraClient;
static DWORD WINAPI camera_client(void *data){
    CameraClient *c=data;HANDLE pipe=c->pipe;free(c);BYTE *source=malloc(AWM_FRAME_BYTES),*scaled=malloc(AWM_FRAME_BYTES),*nv12=malloc(AWM_MAX_PIXELS*3/2);
    uint32_t w=0,h=0,last_epoch=0;uint64_t last_frame=0;int64_t pts=0;NativePreview *resampler=NULL;
    AwmCameraRequest previous={0};LARGE_INTEGER frequency,begin,end;QueryPerformanceFrequency(&frequency);uint64_t count=0;double total_ms=0,max_ms=0;
    if(source&&scaled&&nv12)while(WaitForSingleObject(host.cancel,0)!=WAIT_OBJECT_0){
        AwmCameraRequest q;if(!awm_pipe_io(pipe,&q,sizeof(q),false,host.cancel,3000))break;
        if(q.magic!=AWM_FRAME_MAGIC||q.version!=AWM_CAMERA_VERSION||!awm_frame_shape(q.width,q.height)||(q.format!=1&&q.format!=2))break;
        QueryPerformanceCounter(&begin);bool live=awm_hub_live(&host.hub,GetTickCount64());
        bool cached=last_frame&&last_epoch==host.hub.view->video_epoch&&q.width==previous.width&&q.height==previous.height&&q.format==previous.format;
        bool repeat=live&&cached&&last_frame==host.hub.view->video_frames;
        if(live&&!repeat){AwmFrameInfo info;if(awm_hub_copy_frame(&host.hub,source,AWM_FRAME_BYTES,&info)){w=info.width;h=info.height;pts=info.pts_us;last_frame=info.number;last_epoch=info.epoch;}else if(cached&&awm_hub_live(&host.hub,GetTickCount64()))repeat=true;else live=false;}
        InterlockedIncrement64((volatile LONG64 *)&host.hub.view->camera_requests);if(repeat)InterlockedIncrement64((volatile LONG64 *)&host.hub.view->repeated_samples);
        host.hub.view->request_width=q.width;host.hub.view->request_height=q.height;host.hub.view->request_format=q.format;
        if(!repeat){if(live&&(w!=q.width||h!=q.height)&&!resampler)resampler=native_preview_create(NULL);native_preview_set_scaler(resampler,host.scaler);native_preview_set_framing(resampler,host.framing);
            if(live&&!native_scale_bgra(&resampler,source,w,h,scaled,q.width,q.height)){live=false;fprintf(stderr,"GPU camera resampling failed; no low-quality scaling fallback\n");}if(!live)memset(scaled,0,(size_t)q.width*q.height*4);
            if(q.format==AWM_CAMERA_NV12)awm_bgra_to_nv12(scaled,q.width,q.height,nv12);if(!live)last_frame=0;previous=q;}
        void *pixels=scaled;uint32_t bytes=q.width*q.height*4;
        if(q.format==AWM_CAMERA_NV12){pixels=nv12;bytes=q.width*q.height*3/2;}
        QueryPerformanceCounter(&end);double ms=1000.0*(end.QuadPart-begin.QuadPart)/frequency.QuadPart;total_ms+=ms;if(ms>max_ms)max_ms=ms;count++;
        if(count%300==0){printf("CAMERA PROCESSING: %ux%u format=%u mean=%.3f ms max=%.3f ms over %llu requests; excludes pipe transfer and receiving app\n",q.width,q.height,q.format,total_ms/count,max_ms,(unsigned long long)count);fflush(stdout);}
        live=live&&awm_hub_live(&host.hub,GetTickCount64())&&last_epoch==host.hub.view->video_epoch;
        AwmCameraReply r={AWM_FRAME_MAGIC,AWM_CAMERA_VERSION,q.width,q.height,q.format,bytes,live?1u:0u,host.hub.view->generation,pts};
        if(!awm_pipe_io(pipe,&r,sizeof(r),true,host.cancel,1000)||!awm_pipe_io(pipe,pixels,bytes,true,host.cancel,1000))break;
    }
    native_preview_destroy(resampler);free(source);free(scaled);free(nv12);DisconnectNamedPipe(pipe);CloseHandle(pipe);return 0;
}
static DWORD WINAPI camera_server(void *unused){
    (void)unused;SECURITY_ATTRIBUTES sa;PSECURITY_DESCRIPTOR sd=NULL;if(!awm_user_security(&sa,&sd,true))return GetLastError();bool first=true;
    while(WaitForSingleObject(host.cancel,0)!=WAIT_OBJECT_0){
        EnterCriticalSection(&host.clients_lock);int slot=-1;for(int i=0;i<4;i++){if(host.clients[i]&&WaitForSingleObject(host.clients[i],0)==WAIT_OBJECT_0){CloseHandle(host.clients[i]);host.clients[i]=NULL;}if(!host.clients[i]&&slot<0)slot=i;}LeaveCriticalSection(&host.clients_lock);
        if(slot<0){if(WaitForSingleObject(host.cancel,100)==WAIT_OBJECT_0)break;continue;}
        HANDLE pipe=CreateNamedPipeW(AWM_CAMERA_PIPE,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|(first?FILE_FLAG_FIRST_PIPE_INSTANCE:0),PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,4,1024*1024,1024,0,&sa);
        if(pipe==INVALID_HANDLE_VALUE)break;first=false;OVERLAPPED o={0};o.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);BOOL connected=ConnectNamedPipe(pipe,&o);DWORD err=GetLastError();
        if(!connected&&err==ERROR_PIPE_CONNECTED)connected=TRUE;
        if(!connected&&err==ERROR_IO_PENDING){HANDLE waits[]={host.cancel,o.hEvent};if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0+1){DWORD n;connected=GetOverlappedResult(pipe,&o,&n,FALSE);}else{CancelIoEx(pipe,&o);DWORD n;GetOverlappedResult(pipe,&o,&n,TRUE);}}
        CloseHandle(o.hEvent);if(!connected){CloseHandle(pipe);break;}CameraClient *c=malloc(sizeof(*c));if(!c){CloseHandle(pipe);break;}c->pipe=pipe;
        EnterCriticalSection(&host.clients_lock);host.clients[slot]=CreateThread(NULL,0,camera_client,c,0,NULL);LeaveCriticalSection(&host.clients_lock);if(!host.clients[slot]){free(c);CloseHandle(pipe);}
    }LocalFree(sd);return 0;
}
static bool audio_packet(const AwmPacketHeader *p,const BYTE *bytes,uint32_t channels,uint32_t rate){
    if(rate!=48000||channels<1||channels>8||p->size%(channels*2))return false;const int16_t *in=(const int16_t *)bytes;uint32_t frames=p->size/(channels*2);int16_t mono[9600];float peak=0;
    for(uint32_t offset=0;offset<frames;){uint32_t n=frames-offset;if(n>9600)n=9600;for(uint32_t i=0;i<n;i++){int32_t sum=0;for(uint32_t c=0;c<channels;c++)sum+=in[(size_t)(offset+i)*channels+c];int16_t sample=(int16_t)(sum/(int32_t)channels);float level=fabsf((float)sample/32768);if(level>peak)peak=level;mono[i]=host.hub.view->muted?0:sample;}
        if(host.mic.device&&!awm_microphone_write(&host.mic,mono,n,p->pts_us+(int64_t)offset*1000000/48000))return false;offset+=n;}
    host.hub.view->microphone_peak=host.hub.view->muted?0:peak;host.hub.view->audio_frames+=frames;host.hub.view->microphone_live=1;host.audio_packets++;if(peak>host.max_peak)host.max_peak=peak;return true;
}
int wmain(int argc,wchar_t **argv){
    wchar_t export_name[512]=L"",mapping[200]=AWM_FRAME_MAPPING;unsigned duration=0;bool mic_requested=false;host.cancel=CreateEventW(NULL,TRUE,FALSE,NULL);InitializeCriticalSection(&host.clients_lock);
    host.trace=awm_timing_open(L"AWM_CAPTURE_TIMING_LOG");host.report_at=awm_clock_ms()+5000;host.capture_fps=30;host.trace_frames=GetEnvironmentVariableW(L"AWM_CAPTURE_TRACE_CSV",NULL,0)>0;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--pipe")&&i+1<argc)wcsncpy_s(export_name,512,argv[++i],_TRUNCATE);
        else if(!wcscmp(argv[i],L"--mapping")&&i+1<argc)wcsncpy_s(mapping,200,argv[++i],_TRUNCATE);
        else if(!wcscmp(argv[i],L"--video-buffer")&&i+1<argc){int mode=_wtoi(argv[++i]);if(mode<0||mode>1)return 2;host.smooth=mode==1;}
        else if(!wcscmp(argv[i],L"--capture-fps")&&i+1<argc){host.capture_fps=(unsigned)_wtoi(argv[++i]);if(host.capture_fps<1||host.capture_fps>240)return 2;}
        else if(!wcscmp(argv[i],L"--screen"))host.screen=true;
        else if(!wcscmp(argv[i],L"--framing")&&i+1<argc)host.framing=(unsigned)_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--microphone"))mic_requested=true;
        else if(!wcscmp(argv[i],L"--allow-missing-microphone"))host.allow_missing_mic=true;
        else if(!wcscmp(argv[i],L"--mirror"))host.mirror=true;
        else if(!wcscmp(argv[i],L"--scaler")&&i+1<argc)host.scaler=(unsigned)_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--rotate")&&i+1<argc)host.rotate=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--duration")&&i+1<argc)duration=(unsigned)_wtoi(argv[++i]);
        else{fprintf(stderr,"Invalid media-host argument\n");return 2;}
    }
    static const wchar_t prefix[]=L"\\\\.\\pipe\\AWM.Export.",map_prefix[]=L"Local\\AWM.Camera.Frames.";if(host.scaler>2||host.framing>1||wcsncmp(export_name,prefix,ARRAYSIZE(prefix)-1)||wcsncmp(mapping,map_prefix,ARRAYSIZE(map_prefix)-1)||(host.rotate!=0&&host.rotate!=90&&host.rotate!=180&&host.rotate!=270)){fprintf(stderr,"Invalid owned transport, scaler, framing or rotation\n");return 2;}
    host.trace_session=awm_trace_session(mapping);host.packets=awm_csv_companion(L"AWM_CAPTURE_TRACE_CSV",L".packets.csv","session,packet_ms,source_gap_ms");
    SECURITY_ATTRIBUTES owner_sa;PSECURITY_DESCRIPTOR owner_sd=NULL;if(!awm_user_security(&owner_sa,&owner_sd,false))return 3;host.owner=CreateMutexW(&owner_sa,FALSE,AWM_OWNER_MUTEX);LocalFree(owner_sd);DWORD owned=host.owner?WaitForSingleObject(host.owner,0):WAIT_FAILED;if(owned!=WAIT_OBJECT_0&&owned!=WAIT_ABANDONED){fprintf(stderr,"Another app-owned capture session holds the producer guard\n");return 3;}
    SetConsoleCtrlHandler(console_stop,TRUE);if(!awm_hub_open_named(&host.hub,true,mapping)){fprintf(stderr,"Frame hub unavailable (%lu)\n",GetLastError());return 3;}
    host.hub.view->source_kind=host.screen?1:0;host.hub.view->framing=host.framing;
    host.hub.view->microphone_driver=mic_requested&&awm_microphone_open(&host.mic,host.hub.view->generation)?1:0;
    if(mic_requested&&!host.mic.device&&!host.allow_missing_mic){snprintf(host.hub.view->status,512,"Microphone driver unavailable (%lu). Install the signed output package.",GetLastError());fprintf(stderr,"%s\n",host.hub.view->status);awm_hub_stop(&host.hub);awm_hub_close(&host.hub);return 4;}
    if(mic_requested&&!host.mic.device)fprintf(stderr,"TEST MODE: microphone PCM observed only; no virtual microphone delivery.\n");
    host.pixels=malloc(AWM_FRAME_BYTES);host.transformed=malloc(AWM_FRAME_BYTES);SECURITY_ATTRIBUTES sa;PSECURITY_DESCRIPTOR sd=NULL;if(!awm_user_security(&sa,&sd,false))return 5;
    host.video=awm_video_queue_create(&host.hub,host.capture_fps,host.smooth,awm_trace_session(mapping));if(!host.video){fprintf(stderr,"Video pacing queue unavailable (%lu)\n",GetLastError());return 5;}
    HANDLE pipe=CreateNamedPipeW(export_name,PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,1024,4*1024*1024,0,&sa);LocalFree(sd);
    if(pipe==INVALID_HANDLE_VALUE||!host.pixels||!host.transformed){fprintf(stderr,"Could not prepare media transport (%lu)\n",GetLastError());return 5;}
    host.camera_thread=CreateThread(NULL,0,camera_server,NULL,0,NULL);snprintf(host.hub.view->status,512,"Waiting for headless scrcpy packets");printf("AWM media host protocol 3 ready; source=%s\n",host.screen?"display":"camera");fflush(stdout);
    OVERLAPPED connection={0};connection.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);BOOL connected=ConnectNamedPipe(pipe,&connection);DWORD error=GetLastError();
    if(!connected&&error==ERROR_PIPE_CONNECTED)connected=TRUE;
    if(!connected&&error==ERROR_IO_PENDING){HANDLE waits[]={host.cancel,connection.hEvent};if(WaitForMultipleObjects(2,waits,FALSE,15000)==WAIT_OBJECT_0+1){DWORD n;connected=GetOverlappedResult(pipe,&connection,&n,FALSE);}else{CancelIoEx(pipe,&connection);DWORD n;GetOverlappedResult(pipe,&connection,&n,TRUE);}}
    CloseHandle(connection.hEvent);AVCodecContext *decoder=NULL;AVPacket *packet=av_packet_alloc();AVFrame *frame=av_frame_alloc();BYTE *payload=malloc(AWM_PACKET_MAX+AV_INPUT_BUFFER_PADDING_SIZE);uint32_t audio_channels=0,audio_rate=0,expected_width=0,expected_height=0;bool codec_open=false,await_key=false,failed=!connected;ULONGLONG start=GetTickCount64();uint32_t last_sequence=0;
    if(!packet||!frame||!payload)failed=true;
    HANDLE multimedia=awm_multimedia_begin(L"Capture");printf("MMCSS decode scheduling: %s; no global timer/power changes\n",multimedia?"active":"unavailable; normal scheduler");fflush(stdout);
    while(!failed&&WaitForSingleObject(host.cancel,0)!=WAIT_OBJECT_0&&(!duration||GetTickCount64()-start<(ULONGLONG)duration*1000)){
        if(host.hub.view->stop)break;AwmPacketHeader p;
        if(!awm_pipe_io(pipe,&p,sizeof(p),false,host.cancel,3000)||!awm_packet_valid(&p)||p.sequence!=last_sequence+1){fprintf(stderr,"Export stopped, timed out, or invalid packet\n");failed=true;break;}last_sequence=p.sequence;
        if(p.size&&!awm_pipe_io(pipe,payload,p.size,false,host.cancel,3000)){failed=true;break;}memset(payload+p.size,0,AV_INPUT_BUFFER_PADDING_SIZE);
        host.hub.view->session_heartbeat_ms=GetTickCount64();
        if(p.type==AWM_MSG_HEARTBEAT)continue;
        if(p.type==AWM_MSG_SESSION){if(!decoder){failed=true;break;}expected_width=p.width;expected_height=p.height;awm_video_queue_reconfigure(host.video);if(codec_open)avcodec_flush_buffers(decoder);decoder->width=(int)p.width;decoder->height=(int)p.height;await_key=true;printf("VIDEO SESSION: epoch=%u shape=%ux%u client_resized=%u\n",host.hub.view->video_epoch,p.width,p.height,p.flags);fflush(stdout);continue;}
        if(p.type==AWM_MSG_DISABLED){if(mic_requested){failed=true;fprintf(stderr,"Required phone microphone disabled\n");}continue;}
        if(p.type==AWM_MSG_CLOSE)break;
        if(p.type==AWM_MSG_OPEN){if(p.stream==1){audio_channels=p.channels;audio_rate=p.sample_rate;if(p.codec!=AV_CODEC_ID_PCM_S16LE){failed=true;fprintf(stderr,"This transport requires explicit raw phone audio\n");}}else{
            if(decoder){failed=true;break;}const AVCodec *codec=avcodec_find_decoder((enum AVCodecID)p.codec);if(!codec){failed=true;break;}decoder=avcodec_alloc_context3(codec);if(!decoder){failed=true;break;}
            expected_width=p.width;expected_height=p.height;decoder->width=(int)p.width;decoder->height=(int)p.height;decoder->pkt_timebase=(AVRational){1,1000000};decoder->thread_count=4;decoder->thread_type=FF_THREAD_SLICE;decoder->flags|=AV_CODEC_FLAG_LOW_DELAY|AV_CODEC_FLAG_COPY_OPAQUE;
            if(p.size){decoder->extradata=av_mallocz(p.size+AV_INPUT_BUFFER_PADDING_SIZE);if(!decoder->extradata){failed=true;break;}decoder->extradata_size=(int)p.size;memcpy(decoder->extradata,payload,p.size);}
        }continue;}
        if(p.stream==1){if(!audio_packet(&p,payload,audio_channels,audio_rate)){failed=true;fprintf(stderr,"Microphone input or driver write failed\n");}continue;}
        if(!decoder){failed=true;break;}
        if(p.flags&AWM_FLAG_CONFIG){if(codec_open){const AVCodec *codec=decoder->codec;avcodec_free_context(&decoder);decoder=avcodec_alloc_context3(codec);if(!decoder){failed=true;break;}decoder->width=(int)expected_width;decoder->height=(int)expected_height;decoder->pkt_timebase=(AVRational){1,1000000};decoder->thread_count=4;decoder->thread_type=FF_THREAD_SLICE;decoder->flags|=AV_CODEC_FLAG_LOW_DELAY|AV_CODEC_FLAG_COPY_OPAQUE;codec_open=false;await_key=true;awm_video_queue_reconfigure(host.video);}av_freep(&decoder->extradata);decoder->extradata=av_mallocz(p.size+AV_INPUT_BUFFER_PADDING_SIZE);if(!decoder->extradata){failed=true;break;}decoder->extradata_size=(int)p.size;memcpy(decoder->extradata,payload,p.size);continue;}
        if(await_key&&!(p.flags&AV_PKT_FLAG_KEY))continue;await_key=false;
        if(!codec_open){if(avcodec_open2(decoder,decoder->codec,NULL)<0){failed=true;break;}codec_open=true;}
        double arrived=awm_clock_ms(),source_gap=host.last_pts&&p.pts_us>host.last_pts?(p.pts_us-host.last_pts)/1000.0:NAN;if(host.last_packet)awm_timing_add(&host.arrival,arrived-host.last_packet);host.last_packet=arrived;awm_timing_add(&host.source_gap,source_gap);host.last_pts=(double)p.pts_us;
        if(host.packets)fprintf(host.packets,"%016llx,%.6f,%.6f\n",(unsigned long long)host.trace_session,arrived,source_gap);
        av_packet_unref(packet);if(av_new_packet(packet,(int)p.size)<0){failed=true;break;}memcpy(packet->data,payload,p.size);packet->pts=p.pts_us;packet->dts=p.pts_us;packet->flags=(int)(p.flags&0xffff);double decode_at=awm_clock_ms();
        packet->opaque_ref=av_buffer_alloc(sizeof(PacketTiming));if(!packet->opaque_ref){failed=true;break;}PacketTiming stamp={arrived,decode_at,source_gap};memcpy(packet->opaque_ref->data,&stamp,sizeof(stamp));
        if(avcodec_send_packet(decoder,packet)<0){failed=true;break;}
        while(avcodec_receive_frame(decoder,frame)==0){
            double conversion_at=awm_clock_ms();awm_timing_add(&host.decode,conversion_at-decode_at);
            if((uint32_t)frame->width!=expected_width||(uint32_t)frame->height!=expected_height){fprintf(stderr,"Decoded size differs from declared session; refusing a mislabeled frame\n");failed=true;break;}
            PacketTiming stamp={NAN,NAN,NAN};if(frame->opaque_ref&&frame->opaque_ref->size==sizeof(stamp))memcpy(&stamp,frame->opaque_ref->data,sizeof(stamp));
            AwmVideoTiming timing={stamp.arrival_ms,stamp.decode_start_ms,conversion_at,NAN,stamp.source_gap_ms};
            if(!video_convert(frame,&timing)){failed=true;break;}awm_timing_add(&host.convert,awm_clock_ms()-conversion_at);decode_at=awm_clock_ms();av_frame_unref(frame);
            snprintf(host.hub.view->status,512,"Receiving %s; video pacing %s; microphone %s",host.screen?"screen updates":"camera frames",host.smooth?"smooth / one frame":"minimum latency",host.mic.device?"driver active":mic_requested?"PCM test only":"disabled");
        }
        if(awm_clock_ms()>=host.report_at){AwmVideoQueueStats queue;awm_video_queue_stats(host.video,&queue);printf("VIDEO PACING: published=%llu dropped=%llu late=%llu intentional_delay=%.3f ms\n",(unsigned long long)queue.published,(unsigned long long)queue.dropped,(unsigned long long)queue.late,queue.delay_ms);fflush(stdout);if(host.trace){fprintf(host.trace,"CAPTURE frames=%llu; local packet arrival/decode/convert-enqueue and source PTS intervals; no media saved\n",(unsigned long long)host.hub.view->video_frames);awm_timing_print(host.trace,"packet_gap",&host.arrival);awm_timing_print(host.trace,"source_pts_gap",&host.source_gap);awm_timing_print(host.trace,"decode",&host.decode);awm_timing_print(host.trace,"convert_enqueue",&host.convert);fflush(host.trace);}if(host.packets)fflush(host.packets);host.report_at=awm_clock_ms()+5000;}
    }
    uint64_t video=host.hub.view->video_frames,audio=host.hub.view->audio_frames;double seconds=(double)(GetTickCount64()-start)/1000;
    awm_hub_stop(&host.hub);awm_video_queue_destroy(host.video);host.video=NULL;awm_microphone_close(&host.mic);SetEvent(host.cancel);CancelIoEx(pipe,NULL);CloseHandle(pipe);
    if(host.camera_thread){WaitForSingleObject(host.camera_thread,4000);CloseHandle(host.camera_thread);}for(int i=0;i<4;i++)if(host.clients[i]){WaitForSingleObject(host.clients[i],4000);CloseHandle(host.clients[i]);}
    printf("REAL CAPTURE: video_frames=%llu audio_samples=%llu seconds=%.3f video_fps=%.2f peak=%.4f microphone_driver=%ld\n",(unsigned long long)video,(unsigned long long)audio,seconds,seconds?video/seconds:0,host.max_peak,host.hub.view->microphone_driver);
    if(host.packets)fclose(host.packets);if(host.trace){awm_timing_print(host.trace,"final_packet_gap",&host.arrival);awm_timing_print(host.trace,"final_source_pts_gap",&host.source_gap);awm_timing_print(host.trace,"final_decode",&host.decode);awm_timing_print(host.trace,"final_convert_enqueue",&host.convert);fclose(host.trace);}
    awm_multimedia_end(multimedia);native_preview_destroy(host.converter);avcodec_free_context(&decoder);av_packet_free(&packet);av_frame_free(&frame);free(payload);free(host.pixels);free(host.transformed);awm_hub_close(&host.hub);ReleaseMutex(host.owner);CloseHandle(host.owner);CloseHandle(host.cancel);DeleteCriticalSection(&host.clients_lock);return failed?6:0;
}
