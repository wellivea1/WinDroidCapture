#include "frame_hub.h"
#include "microphone_client.h"
#include "preview.h"
#include "timing.h"
#include "video_queue.h"
#include "thread_class.h"
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/imgutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
typedef struct Host {
    AwmHub hub;AwmMicrophone mic;HANDLE cancel,camera_thread;bool cancel_from_hub;
    HANDLE clients[4],owner;CRITICAL_SECTION clients_lock;volatile LONG next_client_id;
    int rotate;unsigned scaler,framing;bool mirror,allow_missing_mic,screen;
    uint8_t *pixels;uint64_t audio_packets;float max_peak;
    FILE *trace,*packets;uint64_t trace_session;bool trace_frames,smooth,has_last_pts,receiving_status_set,decode_mode_logged;unsigned capture_fps;AwmVideoQueue *video;AwmTiming decode,convert,arrival,source_gap;double last_packet,report_at;int64_t last_pts_us;
    NativePreview *converter;AVBufferRef *hw_device_ctx;AVFrame *sw_frame;
} Host;
static Host host;
typedef struct PacketTiming {double arrival_ms,decode_start_ms,source_gap_ms;} PacketTiming;
static BOOL WINAPI console_stop(DWORD event){if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT){if(host.cancel)SetEvent(host.cancel);return TRUE;}return FALSE;}
static enum AVPixelFormat d3d11_get_format(AVCodecContext *ctx,const enum AVPixelFormat *pix_fmts){
    (void)ctx;
    for(const enum AVPixelFormat *p=pix_fmts;*p!=AV_PIX_FMT_NONE;p++)if(*p==AV_PIX_FMT_D3D11)return *p;
    for(const enum AVPixelFormat *p=pix_fmts;*p!=AV_PIX_FMT_NONE;p++)if(*p==AV_PIX_FMT_YUV420P||*p==AV_PIX_FMT_NV12||*p==AV_PIX_FMT_YUVJ420P)return *p;
    return pix_fmts[0];
}
static bool codec_supports_d3d11va(const AVCodec *codec){
    for(int i=0;;i++){
        const AVCodecHWConfig *cfg=avcodec_get_hw_config(codec,i);
        if(!cfg)return false;
        if((cfg->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)&&cfg->device_type==AV_HWDEVICE_TYPE_D3D11VA)return true;
    }
}
static void init_hw_device(void){
    if(GetEnvironmentVariableW(L"AWM_DISABLE_HWDEC",NULL,0)>0)return;
    if(!host.converter)host.converter=native_preview_create(NULL);
    if(!host.converter||!native_preview_is_hardware(host.converter))return;
    AVBufferRef *hw=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);if(!hw)return;
    AVHWDeviceContext *dc=(AVHWDeviceContext *)hw->data;
    AVD3D11VADeviceContext *d3d=(AVD3D11VADeviceContext *)dc->hwctx;
    ID3D11Device *dev=(ID3D11Device *)native_preview_d3d_device(host.converter);
    ID3D11DeviceContext *ctx=(ID3D11DeviceContext *)native_preview_d3d_context(host.converter);
    ID3D11Device_AddRef(dev);ID3D11DeviceContext_AddRef(ctx);
    d3d->device=dev;d3d->device_context=ctx;
    if(av_hwdevice_ctx_init(hw)==0)host.hw_device_ctx=hw;else av_buffer_unref(&hw);
}
static AVCodecContext *alloc_decoder(const AVCodec *codec,uint32_t w,uint32_t h){
    AVCodecContext *dec=avcodec_alloc_context3(codec);if(!dec)return NULL;
    dec->width=(int)w;dec->height=(int)h;dec->pkt_timebase=(AVRational){1,1000000};
    dec->thread_count=4;dec->thread_type=FF_THREAD_SLICE;dec->flags|=AV_CODEC_FLAG_LOW_DELAY|AV_CODEC_FLAG_COPY_OPAQUE;
    if(host.hw_device_ctx&&codec_supports_d3d11va(codec)){dec->hw_device_ctx=av_buffer_ref(host.hw_device_ctx);dec->get_format=d3d11_get_format;}
    return dec;
}
static bool video_convert(const AVFrame *f,AwmVideoTiming *timing){
    uint32_t w=(uint32_t)f->width,h=(uint32_t)f->height;if(!awm_frame_shape(w,h))return false;
    int rot=(int)InterlockedCompareExchange(&host.hub.view->live_rotate,0,0);
    if(rot!=0&&rot!=90&&rot!=180&&rot!=270)rot=host.rotate;
    bool mir=InterlockedCompareExchange(&host.hub.view->live_mirror,0,0)!=0;
    if(!host.decode_mode_logged){printf("VIDEO DECODE: %s\n",f->format==AV_PIX_FMT_D3D11?"D3D11VA hardware":"CPU software");fflush(stdout);host.decode_mode_logged=true;}
    bool full_range=f->color_range==AVCOL_RANGE_JPEG||f->format==AV_PIX_FMT_YUVJ420P;
    bool bt709=f->colorspace==AVCOL_SPC_BT709;
    bool converted=false;
    if(f->format==AV_PIX_FMT_D3D11){
        AVHWDeviceContext *dc=host.hw_device_ctx?(AVHWDeviceContext *)host.hw_device_ctx->data:NULL;
        AVD3D11VADeviceContext *d3d=dc?(AVD3D11VADeviceContext *)dc->hwctx:NULL;
        if(d3d&&d3d->lock)d3d->lock(d3d->lock_ctx);
        converted=native_d3d11_nv12_bgra(&host.converter,f->data[0],(uint32_t)(intptr_t)f->data[1],w,h,full_range,bt709,rot,mir,host.pixels);
        if(d3d&&d3d->unlock)d3d->unlock(d3d->lock_ctx);
        if(!converted){
            if(!host.sw_frame)host.sw_frame=av_frame_alloc();
            if(host.sw_frame){
                av_frame_unref(host.sw_frame);
                if(av_hwframe_transfer_data(host.sw_frame,f,0)==0&&(host.sw_frame->format==AV_PIX_FMT_NV12||host.sw_frame->format==AV_PIX_FMT_YUV420P)){
                    NativeYuvFrame decoded={0};decoded.width=w;decoded.height=h;decoded.nv12=host.sw_frame->format==AV_PIX_FMT_NV12;decoded.full_range=full_range;decoded.bt709=bt709;decoded.rotate=rot;decoded.mirror=mir;
                    for(unsigned i=0;i<3;i++){decoded.plane[i]=host.sw_frame->data[i];decoded.stride[i]=host.sw_frame->linesize[i];}
                    converted=native_yuv_bgra(&host.converter,&decoded,host.pixels);
                }
            }
        }
        if(!converted){fprintf(stderr,"GPU D3D11VA YUV conversion failed; no silent range/quality fallback\n");return false;}
    }else{
        if(f->format!=AV_PIX_FMT_YUV420P&&f->format!=AV_PIX_FMT_NV12&&f->format!=AV_PIX_FMT_YUVJ420P){fprintf(stderr,"Unsupported decoded pixel format %d; no silent color fallback\n",f->format);return false;}
        NativeYuvFrame decoded={0};decoded.width=w;decoded.height=h;decoded.nv12=f->format==AV_PIX_FMT_NV12;decoded.full_range=full_range;decoded.bt709=bt709;decoded.rotate=rot;decoded.mirror=mir;
        for(unsigned i=0;i<3;i++){decoded.plane[i]=f->data[i];decoded.stride[i]=f->linesize[i];}
        if(!native_yuv_bgra(&host.converter,&decoded,host.pixels)){fprintf(stderr,"GPU YUV conversion failed; no silent range/quality fallback\n");return false;}
    }
    uint32_t ow=(rot==90||rot==270)?h:w,oh=(rot==90||rot==270)?w:h;
    timing->convert_end_ms=awm_clock_ms();return awm_video_queue_push_swap(host.video,&host.pixels,ow,oh,f->pts,timing);
}
typedef struct CamShm {HANDLE file,map;BYTE *view;uint32_t pid,id;} CamShm;
static bool cam_shm_open(CamShm *s,uint32_t unique_id,uint32_t slot){
    memset(s,0,sizeof(*s));wchar_t dir[MAX_PATH],path[MAX_PATH];
    DWORD n=GetEnvironmentVariableW(L"ProgramData",dir,MAX_PATH-80);if(!n||n>=MAX_PATH-80)return false;
    swprintf(path,MAX_PATH,L"%s\\AndroidWebcamManager",dir);CreateDirectoryW(path,NULL);
    uint32_t pid=GetCurrentProcessId();
    swprintf(path,MAX_PATH,L"%s\\AndroidWebcamManager\\awm-cam-%u-%u.shm",dir,pid,unique_id);
    s->file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
    if(s->file!=INVALID_HANDLE_VALUE){s->pid=pid;s->id=unique_id;}
    else{
        /* Fallback for installations whose ProgramData ACL grants IU GRGW without DELETE. */
        swprintf(path,MAX_PATH,L"%s\\AndroidWebcamManager\\awm-cam-slot-%u.shm",dir,slot);
        s->file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,NULL);
        if(s->file==INVALID_HANDLE_VALUE){s->file=NULL;return false;}
        s->pid=0;s->id=slot;
    }
    s->map=CreateFileMappingW(s->file,NULL,PAGE_READWRITE,0,AWM_FRAME_BYTES,NULL);
    if(!s->map){CloseHandle(s->file);memset(s,0,sizeof(*s));return false;}
    s->view=MapViewOfFile(s->map,FILE_MAP_ALL_ACCESS,0,0,AWM_FRAME_BYTES);
    if(!s->view){CloseHandle(s->map);CloseHandle(s->file);memset(s,0,sizeof(*s));return false;}
    return true;
}
static void cam_shm_close(CamShm *s){if(s->view)UnmapViewOfFile(s->view);if(s->map)CloseHandle(s->map);if(s->file)CloseHandle(s->file);memset(s,0,sizeof(*s));}
typedef struct CameraClient {HANDLE pipe;uint32_t slot;} CameraClient;
static DWORD WINAPI camera_client(void *data){
    CameraClient *c=data;HANDLE pipe=c->pipe;uint32_t slot=c->slot;free(c);
    BYTE *source=malloc(AWM_FRAME_BYTES),*scaled=malloc(AWM_FRAME_BYTES),*nv12=malloc(AWM_MAX_PIXELS*3/2);
    HANDLE io_event=CreateEventW(NULL,TRUE,FALSE,NULL);HANDLE multimedia=awm_multimedia_begin(L"Capture");
    uint32_t w=0,h=0,last_epoch=0;uint64_t last_frame=0;int64_t pts=0;NativePreview *resampler=native_preview_create(NULL);
    bool exact_rgb_cached=false,shm_tried=false,shm_valid=false;unsigned last_scaler=~0u,last_framing=~0u;long last_zoom_milli=-1,last_pan_x_milli=-1,last_pan_y_milli=-1;CamShm shm={0};
    AwmCameraRequest previous={0};LARGE_INTEGER frequency,begin,end;QueryPerformanceFrequency(&frequency);uint64_t count=0;double total_ms=0,max_ms=0;
    if(source&&scaled&&nv12&&io_event)while(WaitForSingleObject(host.cancel,0)!=WAIT_OBJECT_0){
        AwmCameraRequest q;if(!awm_pipe_io_ev(pipe,&q,sizeof(q),false,host.cancel,5000,io_event))break;
        if(q.magic!=AWM_FRAME_MAGIC||(q.version!=AWM_CAMERA_VERSION&&q.version!=AWM_CAMERA_VERSION_V3)||!awm_frame_shape(q.width,q.height)||(q.format!=1&&q.format!=2))break;
        bool use_v3=q.version==AWM_CAMERA_VERSION_V3;
        if(use_v3&&!shm_tried){shm_tried=true;cam_shm_open(&shm,(uint32_t)InterlockedIncrement(&host.next_client_id),slot);}
        unsigned cur_scaler=(unsigned)InterlockedCompareExchange(&host.hub.view->live_scaler,0,0);if(cur_scaler>3)cur_scaler=host.scaler;
        unsigned cur_framing=(unsigned)InterlockedCompareExchange(&host.hub.view->live_framing,0,0);if(cur_framing>1)cur_framing=host.framing;
        long cur_zoom_milli=InterlockedCompareExchange(&host.hub.view->live_zoom_milli,0,0);if(cur_zoom_milli<1000||cur_zoom_milli>8000)cur_zoom_milli=1000;
        long cur_pan_x_milli=InterlockedCompareExchange(&host.hub.view->live_pan_x_milli,0,0);if(cur_pan_x_milli<0||cur_pan_x_milli>1000)cur_pan_x_milli=500;
        long cur_pan_y_milli=InterlockedCompareExchange(&host.hub.view->live_pan_y_milli,0,0);if(cur_pan_y_milli<0||cur_pan_y_milli>1000)cur_pan_y_milli=500;
        float cur_zoom=(float)cur_zoom_milli/1000.0f,cur_pan_x=(float)cur_pan_x_milli/1000.0f,cur_pan_y=(float)cur_pan_y_milli/1000.0f;
        host.hub.view->framing=cur_framing;
        QueryPerformanceCounter(&begin);bool live=awm_hub_live(&host.hub,GetTickCount64());
        bool cached=last_frame&&last_epoch==host.hub.view->video_epoch&&q.width==previous.width&&q.height==previous.height&&q.format==previous.format&&cur_scaler==last_scaler&&cur_framing==last_framing&&cur_zoom_milli==last_zoom_milli&&cur_pan_x_milli==last_pan_x_milli&&cur_pan_y_milli==last_pan_y_milli&&(!use_v3||!shm.view||shm_valid);
        bool repeat=live&&cached&&last_frame==host.hub.view->video_frames;
        if(live&&!repeat){AwmFrameInfo info;if(awm_hub_copy_frame(&host.hub,source,AWM_FRAME_BYTES,&info)){w=info.width;h=info.height;pts=info.pts_us;last_frame=info.number;last_epoch=info.epoch;}else if(cached&&awm_hub_live(&host.hub,GetTickCount64()))repeat=true;else live=false;}
        InterlockedIncrement64((volatile LONG64 *)&host.hub.view->camera_requests);if(repeat)InterlockedIncrement64((volatile LONG64 *)&host.hub.view->repeated_samples);
        host.hub.view->request_width=q.width;host.hub.view->request_height=q.height;host.hub.view->request_format=q.format;
        uint32_t bytes=q.format==AWM_CAMERA_NV12?q.width*q.height*3/2:q.width*q.height*4;
        bool write_shm=use_v3&&shm.view!=NULL;
        if(!repeat){
            exact_rgb_cached=false;shm_valid=false;
            if(live){
                bool zoomed=cur_zoom_milli>1000;
                if(!resampler&&(q.format==AWM_CAMERA_NV12||w!=q.width||h!=q.height||zoomed))resampler=native_preview_create(NULL);
                native_preview_set_scaler(resampler,cur_scaler);native_preview_set_framing(resampler,cur_framing);native_preview_set_zoom(resampler,cur_zoom,cur_pan_x,cur_pan_y);
                if(q.format==AWM_CAMERA_NV12){
                    BYTE *dst=write_shm?shm.view:nv12;
                    if(!native_scale_nv12(&resampler,source,w,h,dst,q.width,q.height)){live=false;fprintf(stderr,"GPU camera NV12 resampling failed; no low-quality scaling fallback\n");}
                    else if(write_shm)shm_valid=true;
                }else if(w==q.width&&h==q.height&&!zoomed){
                    if(write_shm){memcpy(shm.view,source,bytes);shm_valid=true;}else exact_rgb_cached=true;
                }else{
                    BYTE *dst=write_shm?shm.view:scaled;
                    if(!native_scale_bgra(&resampler,source,w,h,dst,q.width,q.height)){live=false;fprintf(stderr,"GPU camera resampling failed; no low-quality scaling fallback\n");}
                    else if(write_shm)shm_valid=true;
                }
            }
            if(!live){last_frame=0;if(!use_v3){memset(scaled,0,(size_t)q.width*q.height*4);if(q.format==AWM_CAMERA_NV12)awm_bgra_to_nv12(scaled,q.width,q.height,nv12);}}
            previous=q;last_scaler=cur_scaler;last_framing=cur_framing;last_zoom_milli=cur_zoom_milli;last_pan_x_milli=cur_pan_x_milli;last_pan_y_milli=cur_pan_y_milli;
        }
        QueryPerformanceCounter(&end);double ms=1000.0*(end.QuadPart-begin.QuadPart)/frequency.QuadPart;total_ms+=ms;if(ms>max_ms)max_ms=ms;count++;
        if(count%300==0){printf("CAMERA PROCESSING: %ux%u format=%u mean=%.3f ms max=%.3f ms over %llu requests; excludes pipe transfer and receiving app\n",q.width,q.height,q.format,total_ms/count,max_ms,(unsigned long long)count);fflush(stdout);}
        live=live&&awm_hub_live(&host.hub,GetTickCount64())&&last_epoch==host.hub.view->video_epoch;
        if(use_v3){
            if(!live){
                AwmCameraReplyV3 r3={{AWM_FRAME_MAGIC,AWM_CAMERA_VERSION_V3,q.width,q.height,q.format,0,0,host.hub.view->generation,pts},0,0,0,0};
                if(!awm_pipe_io_ev(pipe,&r3,sizeof(r3),true,host.cancel,1000,io_event))break;
            }else if(write_shm&&shm_valid){
                MemoryBarrier();
                AwmCameraReplyV3 r3={{AWM_FRAME_MAGIC,AWM_CAMERA_VERSION_V3,q.width,q.height,q.format,0,1,host.hub.view->generation,pts},shm.pid,shm.id,bytes,0};
                if(!awm_pipe_io_ev(pipe,&r3,sizeof(r3),true,host.cancel,1000,io_event))break;
            }else{
                void *pixels=q.format==AWM_CAMERA_NV12?(void *)nv12:exact_rgb_cached?(void *)source:(void *)scaled;
                AwmCameraReplyV3 r3={{AWM_FRAME_MAGIC,AWM_CAMERA_VERSION_V3,q.width,q.height,q.format,bytes,1,host.hub.view->generation,pts},0,0,0,0};
                if(!awm_pipe_io_ev(pipe,&r3,sizeof(r3),true,host.cancel,1000,io_event)||!awm_pipe_io_ev(pipe,pixels,bytes,true,host.cancel,1000,io_event))break;
            }
        }else{
            void *pixels=q.format==AWM_CAMERA_NV12?(void *)nv12:exact_rgb_cached?(void *)source:(void *)scaled;
            AwmCameraReply r={AWM_FRAME_MAGIC,AWM_CAMERA_VERSION,q.width,q.height,q.format,bytes,live?1u:0u,host.hub.view->generation,pts};
            if(!awm_pipe_io_ev(pipe,&r,sizeof(r),true,host.cancel,1000,io_event)||!awm_pipe_io_ev(pipe,pixels,bytes,true,host.cancel,1000,io_event))break;
        }
    }
    cam_shm_close(&shm);awm_multimedia_end(multimedia);if(io_event)CloseHandle(io_event);native_preview_destroy(resampler);free(source);free(scaled);free(nv12);DisconnectNamedPipe(pipe);CloseHandle(pipe);return 0;
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
        CloseHandle(o.hEvent);if(!connected){CloseHandle(pipe);break;}CameraClient *c=malloc(sizeof(*c));if(!c){CloseHandle(pipe);break;}c->pipe=pipe;c->slot=(uint32_t)slot;
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
    static const wchar_t prefix[]=L"\\\\.\\pipe\\AWM.Export.",map_prefix[]=L"Local\\AWM.Camera.Frames.";if(host.scaler>3||host.framing>1||wcsncmp(export_name,prefix,ARRAYSIZE(prefix)-1)||wcsncmp(mapping,map_prefix,ARRAYSIZE(map_prefix)-1)||(host.rotate!=0&&host.rotate!=90&&host.rotate!=180&&host.rotate!=270)){fprintf(stderr,"Invalid owned transport, scaler, framing or rotation\n");return 2;}
    host.trace_session=awm_trace_session(mapping);host.packets=awm_csv_companion(L"AWM_CAPTURE_TRACE_CSV",L".packets.csv","session,packet_ms,source_gap_ms");
    SECURITY_ATTRIBUTES owner_sa;PSECURITY_DESCRIPTOR owner_sd=NULL;if(!awm_user_security(&owner_sa,&owner_sd,false))return 3;host.owner=CreateMutexW(&owner_sa,FALSE,AWM_OWNER_MUTEX);LocalFree(owner_sd);DWORD owned=host.owner?WaitForSingleObject(host.owner,0):WAIT_FAILED;if(owned!=WAIT_OBJECT_0&&owned!=WAIT_ABANDONED){fprintf(stderr,"Another app-owned capture session holds the producer guard\n");return 3;}
    SetConsoleCtrlHandler(console_stop,TRUE);if(!awm_hub_open_named(&host.hub,true,mapping)){fprintf(stderr,"Frame hub unavailable (%lu)\n",GetLastError());return 3;}
    if(host.hub.stop_event){HANDLE old=host.cancel;host.cancel=host.hub.stop_event;host.cancel_from_hub=true;if(old)CloseHandle(old);}
    host.hub.view->source_kind=host.screen?1:0;host.hub.view->framing=host.framing;
    InterlockedExchange(&host.hub.view->live_rotate,host.rotate);InterlockedExchange(&host.hub.view->live_mirror,host.mirror?1:0);
    InterlockedExchange(&host.hub.view->live_framing,(LONG)host.framing);InterlockedExchange(&host.hub.view->live_scaler,(LONG)host.scaler);
    InterlockedExchange(&host.hub.view->live_zoom_milli,1000);InterlockedExchange(&host.hub.view->live_pan_x_milli,500);InterlockedExchange(&host.hub.view->live_pan_y_milli,500);
    host.hub.view->microphone_driver=mic_requested&&awm_microphone_open(&host.mic,host.hub.view->generation)?1:0;
    if(mic_requested&&!host.mic.device&&!host.allow_missing_mic){snprintf(host.hub.view->status,512,"Microphone driver unavailable (%lu). Install the signed output package.",GetLastError());fprintf(stderr,"%s\n",host.hub.view->status);awm_hub_stop(&host.hub);awm_hub_close(&host.hub);return 4;}
    if(mic_requested&&!host.mic.device)fprintf(stderr,"TEST MODE: microphone PCM observed only; no virtual microphone delivery.\n");
    host.pixels=malloc(AWM_FRAME_BYTES);SECURITY_ATTRIBUTES sa;PSECURITY_DESCRIPTOR sd=NULL;if(!awm_user_security(&sa,&sd,false))return 5;
    host.video=awm_video_queue_create(&host.hub,host.capture_fps,host.smooth,awm_trace_session(mapping));if(!host.video){fprintf(stderr,"Video pacing queue unavailable (%lu)\n",GetLastError());return 5;}
    init_hw_device();
    HANDLE pipe=CreateNamedPipeW(export_name,PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,1024,4*1024*1024,0,&sa);LocalFree(sd);
    if(pipe==INVALID_HANDLE_VALUE||!host.pixels){fprintf(stderr,"Could not prepare media transport (%lu)\n",GetLastError());return 5;}
    host.camera_thread=CreateThread(NULL,0,camera_server,NULL,0,NULL);snprintf(host.hub.view->status,512,"Waiting for headless scrcpy packets");printf("AWM media host protocol 3 ready; source=%s; d3d11va=%s\n",host.screen?"display":"camera",host.hw_device_ctx?"available":"unavailable");fflush(stdout);
    OVERLAPPED connection={0};connection.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);BOOL connected=ConnectNamedPipe(pipe,&connection);DWORD error=GetLastError();
    if(!connected&&error==ERROR_PIPE_CONNECTED)connected=TRUE;
    if(!connected&&error==ERROR_IO_PENDING){HANDLE waits[]={host.cancel,connection.hEvent};if(WaitForMultipleObjects(2,waits,FALSE,15000)==WAIT_OBJECT_0+1){DWORD n;connected=GetOverlappedResult(pipe,&connection,&n,FALSE);}else{CancelIoEx(pipe,&connection);DWORD n;GetOverlappedResult(pipe,&connection,&n,TRUE);}}
    CloseHandle(connection.hEvent);AVCodecContext *decoder=NULL;AVPacket *packet=av_packet_alloc();AVFrame *frame=av_frame_alloc();BYTE *payload=malloc(AWM_PACKET_MAX+AV_INPUT_BUFFER_PADDING_SIZE);HANDLE pipe_event=CreateEventW(NULL,TRUE,FALSE,NULL);uint32_t audio_channels=0,audio_rate=0,expected_width=0,expected_height=0,decode_errors=0;bool codec_open=false,await_key=false,failed=!connected;ULONGLONG start=GetTickCount64();uint32_t last_sequence=0;
    if(!packet||!frame||!payload||!pipe_event)failed=true;
    HANDLE multimedia=awm_multimedia_begin(L"Capture");printf("MMCSS decode scheduling: %s; no global timer/power changes\n",multimedia?"active":"unavailable; normal scheduler");fflush(stdout);
    while(!failed&&WaitForSingleObject(host.cancel,0)!=WAIT_OBJECT_0&&(!duration||GetTickCount64()-start<(ULONGLONG)duration*1000)){
        if(host.hub.view->stop)break;AwmPacketHeader p;
        if(!awm_pipe_io_ev(pipe,&p,sizeof(p),false,host.cancel,6000,pipe_event)||!awm_packet_valid(&p)||p.sequence!=last_sequence+1){if(host.hub.view->stop||WaitForSingleObject(host.cancel,0)==WAIT_OBJECT_0)break;fprintf(stderr,"Export stopped, timed out, or invalid packet\n");failed=true;break;}last_sequence=p.sequence;
        if(p.size&&!awm_pipe_io_ev(pipe,payload,p.size,false,host.cancel,6000,pipe_event)){if(host.hub.view->stop||WaitForSingleObject(host.cancel,0)==WAIT_OBJECT_0)break;failed=true;break;}memset(payload+p.size,0,AV_INPUT_BUFFER_PADDING_SIZE);
        *(volatile uint64_t *)&host.hub.view->session_heartbeat_ms=GetTickCount64();
        if(p.type==AWM_MSG_HEARTBEAT)continue;
        if(p.type==AWM_MSG_SESSION){if(!decoder){failed=true;break;}expected_width=p.width;expected_height=p.height;awm_video_queue_reconfigure(host.video);if(codec_open)avcodec_flush_buffers(decoder);decoder->width=(int)p.width;decoder->height=(int)p.height;await_key=true;printf("VIDEO SESSION: epoch=%u shape=%ux%u client_resized=%u\n",host.hub.view->video_epoch,p.width,p.height,p.flags);fflush(stdout);continue;}
        if(p.type==AWM_MSG_DISABLED){if(mic_requested){failed=true;fprintf(stderr,"Required phone microphone disabled\n");}continue;}
        if(p.type==AWM_MSG_CLOSE)break;
        if(p.type==AWM_MSG_OPEN){if(p.stream==1){audio_channels=p.channels;audio_rate=p.sample_rate;if(p.codec!=AV_CODEC_ID_PCM_S16LE){failed=true;fprintf(stderr,"This transport requires explicit raw phone audio\n");}}else{
            if(decoder){failed=true;break;}const AVCodec *codec=avcodec_find_decoder((enum AVCodecID)p.codec);if(!codec){failed=true;break;}decoder=alloc_decoder(codec,p.width,p.height);if(!decoder){failed=true;break;}
            expected_width=p.width;expected_height=p.height;
            if(p.size){decoder->extradata=av_mallocz(p.size+AV_INPUT_BUFFER_PADDING_SIZE);if(!decoder->extradata){failed=true;break;}decoder->extradata_size=(int)p.size;memcpy(decoder->extradata,payload,p.size);}
        }continue;}
        if(p.stream==1){if(!audio_packet(&p,payload,audio_channels,audio_rate)){failed=true;fprintf(stderr,"Microphone input or driver write failed\n");}continue;}
        if(!decoder){failed=true;break;}
        if(p.flags&AWM_FLAG_CONFIG){if(codec_open){const AVCodec *codec=decoder->codec;avcodec_free_context(&decoder);decoder=alloc_decoder(codec,expected_width,expected_height);if(!decoder){failed=true;break;}codec_open=false;await_key=true;awm_video_queue_reconfigure(host.video);}av_freep(&decoder->extradata);decoder->extradata=av_mallocz(p.size+AV_INPUT_BUFFER_PADDING_SIZE);if(!decoder->extradata){failed=true;break;}decoder->extradata_size=(int)p.size;memcpy(decoder->extradata,payload,p.size);continue;}
        if(await_key&&!(p.flags&AV_PKT_FLAG_KEY))continue;await_key=false;
        if(!codec_open){if(avcodec_open2(decoder,decoder->codec,NULL)<0){if(decoder->hw_device_ctx){av_buffer_unref(&decoder->hw_device_ctx);decoder->get_format=NULL;if(avcodec_open2(decoder,decoder->codec,NULL)<0){failed=true;break;}}else{failed=true;break;}}codec_open=true;}
        double arrived=awm_clock_ms(),source_gap=host.has_last_pts&&p.pts_us>host.last_pts_us?(p.pts_us-host.last_pts_us)/1000.0:NAN;if(host.last_packet)awm_timing_add(&host.arrival,arrived-host.last_packet);host.last_packet=arrived;awm_timing_add(&host.source_gap,source_gap);host.last_pts_us=p.pts_us;host.has_last_pts=true;
        if(host.packets)fprintf(host.packets,"%016llx,%.6f,%.6f\n",(unsigned long long)host.trace_session,arrived,source_gap);
        av_packet_unref(packet);if(av_new_packet(packet,(int)p.size)<0){failed=true;break;}memcpy(packet->data,payload,p.size);packet->pts=p.pts_us;packet->dts=p.pts_us;packet->flags=(int)(p.flags&0xffff);double decode_at=awm_clock_ms();
        packet->opaque_ref=av_buffer_alloc(sizeof(PacketTiming));if(!packet->opaque_ref){failed=true;break;}PacketTiming stamp={arrived,decode_at,source_gap};memcpy(packet->opaque_ref->data,&stamp,sizeof(stamp));
        int send_rc=avcodec_send_packet(decoder,packet);
        if(send_rc==AVERROR(EAGAIN)){while(avcodec_receive_frame(decoder,frame)==0)av_frame_unref(frame);send_rc=avcodec_send_packet(decoder,packet);}
        if(send_rc<0){
            if(send_rc==AVERROR_INVALIDDATA&&++decode_errors<16){avcodec_flush_buffers(decoder);await_key=true;fprintf(stderr,"Transient corrupt video packet; resyncing on next keyframe (%u/16)\n",decode_errors);continue;}
            failed=true;break;
        }
        while(avcodec_receive_frame(decoder,frame)==0){
            double conversion_at=awm_clock_ms();awm_timing_add(&host.decode,conversion_at-decode_at);
            if((uint32_t)frame->width!=expected_width||(uint32_t)frame->height!=expected_height){
                av_frame_unref(frame);
                if(++decode_errors<8){avcodec_flush_buffers(decoder);await_key=true;fprintf(stderr,"Transient session-size mismatch; waiting for matching keyframe (%u/8)\n",decode_errors);break;}
                fprintf(stderr,"Decoded size differs from declared session; refusing a mislabeled frame\n");failed=true;break;
            }
            decode_errors=0;
            PacketTiming stamp={NAN,NAN,NAN};if(frame->opaque_ref&&frame->opaque_ref->size==sizeof(stamp))memcpy(&stamp,frame->opaque_ref->data,sizeof(stamp));
            AwmVideoTiming timing={stamp.arrival_ms,stamp.decode_start_ms,conversion_at,NAN,stamp.source_gap_ms};
            if(!video_convert(frame,&timing)){failed=true;break;}awm_timing_add(&host.convert,awm_clock_ms()-conversion_at);decode_at=awm_clock_ms();av_frame_unref(frame);
            if(!host.receiving_status_set){snprintf(host.hub.view->status,512,"Receiving %s; video pacing %s; microphone %s",host.screen?"screen updates":"camera frames",host.smooth?"smooth / one frame":"minimum latency",host.mic.device?"driver active":mic_requested?"PCM test only":"disabled");host.receiving_status_set=true;}
        }
        if(awm_clock_ms()>=host.report_at){AwmVideoQueueStats queue;awm_video_queue_stats(host.video,&queue);printf("VIDEO PACING: published=%llu dropped=%llu late=%llu intentional_delay=%.3f ms\n",(unsigned long long)queue.published,(unsigned long long)queue.dropped,(unsigned long long)queue.late,queue.delay_ms);fflush(stdout);if(host.trace){fprintf(host.trace,"CAPTURE frames=%llu; local packet arrival/decode/convert-enqueue and source PTS intervals; no media saved\n",(unsigned long long)host.hub.view->video_frames);awm_timing_print(host.trace,"packet_gap",&host.arrival);awm_timing_print(host.trace,"source_pts_gap",&host.source_gap);awm_timing_print(host.trace,"decode",&host.decode);awm_timing_print(host.trace,"convert_enqueue",&host.convert);fflush(host.trace);}if(host.packets)fflush(host.packets);host.report_at=awm_clock_ms()+5000;}
    }
    uint64_t video=host.hub.view->video_frames,audio=host.hub.view->audio_frames;double seconds=(double)(GetTickCount64()-start)/1000;
    awm_hub_stop(&host.hub);awm_video_queue_destroy(host.video);host.video=NULL;awm_microphone_close(&host.mic);SetEvent(host.cancel);CancelIoEx(pipe,NULL);CloseHandle(pipe);if(pipe_event)CloseHandle(pipe_event);
    if(host.camera_thread){WaitForSingleObject(host.camera_thread,4000);CloseHandle(host.camera_thread);}
    HANDLE active_clients[4]={0};EnterCriticalSection(&host.clients_lock);for(int i=0;i<4;i++){active_clients[i]=host.clients[i];host.clients[i]=NULL;}LeaveCriticalSection(&host.clients_lock);
    for(int i=0;i<4;i++)if(active_clients[i]){WaitForSingleObject(active_clients[i],4000);CloseHandle(active_clients[i]);}
    printf("REAL CAPTURE: video_frames=%llu audio_samples=%llu seconds=%.3f video_fps=%.2f peak=%.4f microphone_driver=%ld\n",(unsigned long long)video,(unsigned long long)audio,seconds,seconds?video/seconds:0,host.max_peak,host.hub.view->microphone_driver);
    if(host.packets)fclose(host.packets);if(host.trace){awm_timing_print(host.trace,"final_packet_gap",&host.arrival);awm_timing_print(host.trace,"final_source_pts_gap",&host.source_gap);awm_timing_print(host.trace,"final_decode",&host.decode);awm_timing_print(host.trace,"final_convert_enqueue",&host.convert);fclose(host.trace);}
    awm_multimedia_end(multimedia);avcodec_free_context(&decoder);av_frame_free(&host.sw_frame);av_buffer_unref(&host.hw_device_ctx);native_preview_destroy(host.converter);av_packet_free(&packet);av_frame_free(&frame);free(payload);free(host.pixels);awm_hub_close(&host.hub);ReleaseMutex(host.owner);CloseHandle(host.owner);if(!host.cancel_from_hub&&host.cancel)CloseHandle(host.cancel);DeleteCriticalSection(&host.clients_lock);return failed?6:0;
}
