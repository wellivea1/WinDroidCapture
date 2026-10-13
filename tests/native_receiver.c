/* Explicit, local receiving application. No fallback, network, or recording. */
#define CONST_VTABLE
#include "../src/audio.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/camera_ids.h"
#include "../src/native/preview.h"
#include "../src/native/timing.h"
#define RECEIVER_FRAME (WM_APP+42)
typedef struct Callback {IMFSourceReaderCallback iface;LONG refs;} Callback;
static struct {HWND window,preview;NativePreview *gpu,*converter;CRITICAL_SECTION lock;IMFMediaSource *source;IMFSourceReader *reader;AudioMeter meter;Callback cb;BYTE *pixels,*snapshot,*display;DWORD bytes;UINT32 width,height;LONG stride,paint_pending;bool running,mic,render_failed;HRESULT error;uint64_t frames,hash,changed,drawn,presented;float peak,max_peak;ULONGLONG started;unsigned seconds,request_width,request_height,request_fps;bool video_only;LARGE_INTEGER clock_frequency,last_callback;double gaps[20000];unsigned gap_count;FILE *trace;double trace_flush;wchar_t camera_name[256],mic_name[256];} receiver;
static void request_frame(void){if(receiver.window&&InterlockedCompareExchange(&receiver.paint_pending,1,0)==0)PostMessageW(receiver.window,RECEIVER_FRAME,0,0);}
static void render_frame(void){
    InterlockedExchange(&receiver.paint_pending,0);bool redraw=false;
    EnterCriticalSection(&receiver.lock);if(receiver.snapshot&&receiver.frames&&receiver.frames!=receiver.drawn){memcpy(receiver.snapshot,receiver.pixels,receiver.bytes);receiver.drawn=receiver.frames;redraw=true;}LeaveCriticalSection(&receiver.lock);
    if(!redraw||receiver.render_failed)return;
    /* Release the callback lock before GPU conversion/presentation. The
       receiver measures our explicitly selected camera, without RGB MFTs. */
    NativeYuvFrame yuv={0};yuv.plane[0]=receiver.snapshot;yuv.plane[1]=receiver.snapshot+(size_t)receiver.width*receiver.height;yuv.stride[0]=yuv.stride[1]=(int)receiver.width;yuv.width=receiver.width;yuv.height=receiver.height;yuv.nv12=true;yuv.bt709=true;
    if(!native_yuv_bgra(&receiver.converter,&yuv,receiver.display)){receiver.render_failed=true;return;}
    if(!receiver.gpu)receiver.gpu=native_preview_create(receiver.preview);native_preview_set_scaler(receiver.gpu,AWM_SCALE_EWA_LANCZOS);
    if(!native_preview_frame(receiver.gpu,receiver.display,receiver.width,receiver.height))receiver.render_failed=true;else receiver.presented++;
}
static HRESULT STDMETHODCALLTYPE query(IMFSourceReaderCallback *i,REFIID id,void **out){if(!out)return E_POINTER;*out=NULL;if(!IsEqualIID(id,&IID_IUnknown)&&!IsEqualIID(id,&IID_IMFSourceReaderCallback))return E_NOINTERFACE;*out=i;InterlockedIncrement(&((Callback *)i)->refs);return S_OK;}
static ULONG STDMETHODCALLTYPE add(IMFSourceReaderCallback *i){return InterlockedIncrement(&((Callback *)i)->refs);}
static ULONG STDMETHODCALLTYPE release(IMFSourceReaderCallback *i){return InterlockedDecrement(&((Callback *)i)->refs);}
static HRESULT STDMETHODCALLTYPE sample(IMFSourceReaderCallback *i,HRESULT hr,DWORD stream,DWORD flags,LONGLONG timestamp,IMFSample *s){
    (void)i;(void)stream;(void)timestamp;double callback_at=awm_clock_ms();EnterCriticalSection(&receiver.lock);
    if(!receiver.running){LeaveCriticalSection(&receiver.lock);return S_OK;}
    if(FAILED(hr))receiver.error=hr;
    if(s&&SUCCEEDED(hr)){IMFMediaBuffer *buffer=NULL;if(SUCCEEDED(IMFSample_ConvertToContiguousBuffer(s,&buffer))){IMF2DBuffer *two=NULL;bool copied=false;
        if(SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer,&IID_IMF2DBuffer,(void **)&two))){BYTE *scan=NULL;LONG pitch=0;if(SUCCEEDED(IMF2DBuffer_Lock2D(two,&scan,&pitch))){if(pitch>0&&(UINT32)pitch>=receiver.width){for(UINT32 y=0;y<receiver.height*3/2;y++)memcpy(receiver.pixels+(size_t)y*receiver.width,scan+(ptrdiff_t)y*pitch,receiver.width);copied=true;}IMF2DBuffer_Unlock2D(two);}IMF2DBuffer_Release(two);}
        else{BYTE *p=NULL;DWORD n=0;if(SUCCEEDED(IMFMediaBuffer_Lock(buffer,&p,NULL,&n))){size_t pitch=receiver.stride>0?(size_t)receiver.stride:0;if(pitch>=receiver.width&&n>=pitch*receiver.height*3/2&&receiver.pixels){for(UINT32 y=0;y<receiver.height*3/2;y++)memcpy(receiver.pixels+(size_t)y*receiver.width,p+(size_t)y*pitch,receiver.width);copied=true;}IMFMediaBuffer_Unlock(buffer);}}
        if(copied){LARGE_INTEGER tick;QueryPerformanceCounter(&tick);if(receiver.last_callback.QuadPart&&receiver.gap_count<20000)receiver.gaps[receiver.gap_count++]=1000.0*(tick.QuadPart-receiver.last_callback.QuadPart)/receiver.clock_frequency.QuadPart;receiver.last_callback=tick;receiver.frames++;uint64_t hash=1469598103934665603ull;for(DWORD k=0;k<receiver.bytes;k+=257){hash^=receiver.pixels[k];hash*=1099511628211ull;}if(receiver.hash&&receiver.hash!=hash)receiver.changed++;receiver.hash=hash;if(receiver.trace){fprintf(receiver.trace,"%llu,%.6f\n",(unsigned long long)receiver.frames,callback_at);if(callback_at>=receiver.trace_flush){fflush(receiver.trace);receiver.trace_flush=callback_at+5000;}}request_frame();}IMFMediaBuffer_Release(buffer);}}
    IMFSourceReader *next=receiver.running&&SUCCEEDED(hr)&&!(flags&MF_SOURCE_READERF_ENDOFSTREAM)?receiver.reader:NULL;if(next)IMFSourceReader_AddRef(next);LeaveCriticalSection(&receiver.lock);
    if(next){IMFSourceReader_ReadSample(next,MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,NULL,NULL,NULL,NULL);IMFSourceReader_Release(next);}return S_OK;
}
static HRESULT STDMETHODCALLTYPE event(IMFSourceReaderCallback *i,DWORD stream,IMFMediaEvent *e){(void)i;(void)stream;(void)e;return S_OK;}
static HRESULT STDMETHODCALLTYPE flush(IMFSourceReaderCallback *i,DWORD stream){(void)i;(void)stream;return S_OK;}
static const IMFSourceReaderCallbackVtbl vtable={query,add,release,sample,flush,event};
static HRESULT open_camera(void){
    IMFAttributes *a=NULL;IMFActivate **devices=NULL;UINT32 count=0;IMFActivate *selected=NULL;int matches=0;HRESULT hr=MFCreateAttributes(&a,1);
    if(SUCCEEDED(hr))hr=IMFAttributes_SetGUID(a,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if(SUCCEEDED(hr))hr=MFEnumDeviceSources(a,&devices,&count);
    if(SUCCEEDED(hr))for(UINT32 k=0;k<count;k++){wchar_t *name=NULL;UINT32 n;if(SUCCEEDED(IMFActivate_GetAllocatedString(devices[k],&MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&n))){if(wcsstr(name,AWM_CAMERA_FRIENDLY)){matches++;selected=devices[k];IMFActivate_AddRef(selected);awm_wcopy(receiver.camera_name,256,name);}CoTaskMemFree(name);}IMFActivate_Release(devices[k]);}CoTaskMemFree(devices);if(a)IMFAttributes_Release(a);
    hr=matches==1?IMFActivate_ActivateObject(selected,&IID_IMFMediaSource,(void **)&receiver.source):MF_E_NOT_FOUND;if(selected)IMFActivate_Release(selected);
    if(FAILED(hr))return hr;hr=MFCreateAttributes(&a,3);if(SUCCEEDED(hr))hr=IMFAttributes_SetUnknown(a,&MF_SOURCE_READER_ASYNC_CALLBACK,(IUnknown *)&receiver.cb.iface);if(SUCCEEDED(hr))hr=MFCreateSourceReaderFromMediaSource(receiver.source,a,&receiver.reader);if(a)IMFAttributes_Release(a);
    /* Receive the source's native NV12; do not insert Windows' RGB conversion
     * MFT, which failed to start concurrently with Discord on this machine. */
    IMFMediaType *type=NULL;if(SUCCEEDED(hr))hr=MFCreateMediaType(&type);if(SUCCEEDED(hr)){IMFMediaType_SetGUID(type,&MF_MT_MAJOR_TYPE,&MFMediaType_Video);IMFMediaType_SetGUID(type,&MF_MT_SUBTYPE,&MFVideoFormat_NV12);if(receiver.request_width){IMFMediaType_SetUINT64(type,&MF_MT_FRAME_SIZE,((UINT64)receiver.request_width<<32)|receiver.request_height);IMFMediaType_SetUINT64(type,&MF_MT_FRAME_RATE,((UINT64)receiver.request_fps<<32)|1);}hr=IMFSourceReader_SetCurrentMediaType(receiver.reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM,NULL,type);}if(type)IMFMediaType_Release(type);type=NULL;
    if(SUCCEEDED(hr))hr=IMFSourceReader_GetCurrentMediaType(receiver.reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM,&type);UINT64 size=0;if(SUCCEEDED(hr))hr=IMFMediaType_GetUINT64(type,&MF_MT_FRAME_SIZE,&size);if(SUCCEEDED(hr)){UINT32 stride=0;if(SUCCEEDED(IMFMediaType_GetUINT32(type,&MF_MT_DEFAULT_STRIDE,&stride)))receiver.stride=(LONG)stride;else receiver.stride=(LONG)(size>>32);}if(type)IMFMediaType_Release(type);
    receiver.width=(UINT32)(size>>32);receiver.height=(UINT32)size;if(SUCCEEDED(hr)&&(receiver.width>3840||receiver.height>3840||(uint64_t)receiver.width*receiver.height>3840u*2160u))hr=MF_E_INVALIDMEDIATYPE;
    if(SUCCEEDED(hr)){receiver.bytes=receiver.width*receiver.height*3/2;receiver.pixels=calloc(receiver.bytes,1);receiver.snapshot=malloc(receiver.bytes);receiver.display=malloc((size_t)receiver.width*receiver.height*4);if(!receiver.pixels||!receiver.snapshot||!receiver.display)hr=E_OUTOFMEMORY;}
    if(SUCCEEDED(hr)){receiver.running=true;hr=IMFSourceReader_ReadSample(receiver.reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,NULL,NULL,NULL,NULL);}return hr;
}
static bool open_microphone(void){AudioEndpoints *endpoints=calloc(1,sizeof(*endpoints));char err[512];wchar_t id[1024]=L"";int matches=0;if(!endpoints)return false;
    if(audio_enumerate(endpoints,err,sizeof(err)))for(int k=0;k<endpoints->count;k++){AudioEndpoint *ep=&endpoints->items[k];if(ep->flow==eCapture&&ep->state==DEVICE_STATE_ACTIVE&&wcsstr(ep->name,L"Android Webcam Microphone")){matches++;awm_wcopy(id,1024,ep->id);awm_wcopy(receiver.mic_name,256,ep->name);}}free(endpoints);return matches==1&&audio_meter_open(&receiver.meter,id,err,sizeof(err));}
static void close_inputs(void){EnterCriticalSection(&receiver.lock);receiver.running=false;IMFSourceReader *reader=receiver.reader;IMFMediaSource *source=receiver.source;receiver.reader=NULL;receiver.source=NULL;LeaveCriticalSection(&receiver.lock);if(reader)IMFSourceReader_Flush(reader,MF_SOURCE_READER_ALL_STREAMS);if(source)IMFMediaSource_Shutdown(source);if(reader)IMFSourceReader_Release(reader);if(source)IMFMediaSource_Release(source);audio_meter_close(&receiver.meter);}
static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_SIZE&&receiver.preview){RECT r;GetClientRect(h,&r);MoveWindow(receiver.preview,0,0,r.right,r.bottom>110?r.bottom-110:1,TRUE);receiver.drawn=0;request_frame();return 0;}
    if(msg==RECEIVER_FRAME){render_frame();return 0;}
    if(msg==WM_TIMER){if(receiver.mic){float p;if(audio_meter_tick(&receiver.meter,&p)){receiver.peak=p;if(p>receiver.max_peak)receiver.max_peak=p;}}
        RECT r;GetClientRect(h,&r);r.top=r.bottom>110?r.bottom-110:0;InvalidateRect(h,&r,FALSE);if(GetTickCount64()-receiver.started>=receiver.seconds*1000u)PostMessageW(h,WM_CLOSE,0,0);return 0;}
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC screen=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);HDC dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom);HGDIOBJ old=SelectObject(dc,bitmap);FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));SetTextColor(dc,RGB(230,235,244));SetBkMode(dc,TRANSPARENT);wchar_t t[1600];EnterCriticalSection(&receiver.lock);
        swprintf(t,1600,L"Camera: %s · %llu received frames · %llu changing samples\nMicrophone: %s · peak %.4f\nPhone speak/tap + visible clap required for source and sync confirmation.\nLocal only. Camera HRESULT 0x%08lX · %s",receiver.camera_name[0]?receiver.camera_name:L"OUR CAMERA NOT AVAILABLE",(unsigned long long)receiver.frames,(unsigned long long)receiver.changed,receiver.mic?receiver.mic_name:L"OUR MICROPHONE NOT AVAILABLE",receiver.peak,(unsigned long)receiver.error,receiver.render_failed?L"GPU RENDER FAILED":L"D3D11 / EWA Lanczos sharp");LeaveCriticalSection(&receiver.lock);RECT tr={14,r.bottom-103,r.right-14,r.bottom};DrawTextW(dc,t,-1,&tr,DT_WORDBREAK);BitBlt(screen,0,r.bottom-110,r.right,110,dc,0,r.bottom-110,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);EndPaint(h,&ps);return 0;}
    if(msg==WM_CLOSE){KillTimer(h,1);close_inputs();native_preview_destroy(receiver.gpu);receiver.gpu=NULL;native_preview_destroy(receiver.converter);receiver.converter=NULL;DestroyWindow(h);return 0;}if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,msg,wp,lp);}
static int compare_gap(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return x<y?-1:x>y?1:0;}
int wmain(int argc,wchar_t **argv){
    receiver.seconds=20;receiver.request_fps=30;
    for(int i=1;i<argc;i++){if(!wcscmp(argv[i],L"--duration")&&i+1<argc)receiver.seconds=(unsigned)_wtoi(argv[++i]);else if(!wcscmp(argv[i],L"--video-only"))receiver.video_only=true;else if(!wcscmp(argv[i],L"--width")&&i+1<argc)receiver.request_width=(unsigned)_wtoi(argv[++i]);else if(!wcscmp(argv[i],L"--height")&&i+1<argc)receiver.request_height=(unsigned)_wtoi(argv[++i]);else if(!wcscmp(argv[i],L"--fps")&&i+1<argc)receiver.request_fps=(unsigned)_wtoi(argv[++i]);else return 2;}
    if(receiver.seconds<1||receiver.seconds>300||(receiver.request_width&&(!receiver.request_height||receiver.request_width>3840||receiver.request_height>2160||receiver.request_width%2||receiver.request_height%2))||(receiver.request_fps!=30&&receiver.request_fps!=60))return 2;QueryPerformanceFrequency(&receiver.clock_frequency);
    platform_init();CoInitializeEx(NULL,COINIT_MULTITHREADED);MFStartup(MF_VERSION,MFSTARTUP_FULL);InitializeCriticalSection(&receiver.lock);receiver.cb.iface.lpVtbl=&vtable;receiver.cb.refs=1;receiver.trace=awm_csv_open(L"AWM_RECEIVER_TRACE_CSV","frame,callback_ms");receiver.trace_flush=awm_clock_ms()+5000;
    WNDCLASSW c={0};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(NULL);c.lpszClassName=L"AWMExplicitNativeReceiver";c.hCursor=LoadCursorW(NULL,IDC_ARROW);RegisterClassW(&c);receiver.window=CreateWindowW(c.lpszClassName,L"WinDroid Capture — explicit local receiver",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,960,700,NULL,NULL,c.hInstance,NULL);receiver.preview=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_BLACKRECT,0,0,1,1,receiver.window,NULL,c.hInstance,NULL);RECT r;GetClientRect(receiver.window,&r);MoveWindow(receiver.preview,0,0,r.right,r.bottom-110,TRUE);
    receiver.error=open_camera();receiver.mic=receiver.video_only?false:open_microphone();receiver.started=GetTickCount64();SetTimer(receiver.window,1,50,NULL);
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    printf("LOCAL RECEIVER: camera_hr=0x%08lX camera_frames=%llu presented=%llu changed_samples=%llu output=%ux%u microphone_open=%d microphone_peak=%.4f\n",(unsigned long)receiver.error,(unsigned long long)receiver.frames,(unsigned long long)receiver.presented,(unsigned long long)receiver.changed,receiver.width,receiver.height,receiver.mic,receiver.max_peak);
    if(receiver.gap_count){qsort(receiver.gaps,receiver.gap_count,sizeof(double),compare_gap);printf("CAMERA CALLBACK GAPS: p50=%.3f ms p95=%.3f ms max=%.3f ms count=%u; receiver callback timing, not phone latency\n",receiver.gaps[receiver.gap_count/2],receiver.gaps[(receiver.gap_count-1)*95/100],receiver.gaps[receiver.gap_count-1],receiver.gap_count);}
    puts(receiver.mic?"Own microphone endpoint received; phone-specific sound and sync still require observation.":"MICROPHONE NOT RUN: own driver endpoint unavailable; no physical microphone or cable fallback.");
    MFShutdown();if(receiver.trace)fclose(receiver.trace);free(receiver.pixels);free(receiver.snapshot);free(receiver.display);DeleteCriticalSection(&receiver.lock);CoUninitialize();platform_shutdown();return FAILED(receiver.error)||!receiver.frames||(!receiver.video_only&&!receiver.mic)||receiver.render_failed?1:0;
}
