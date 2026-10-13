/* Local Windows receiver, in C. Explicit OBS camera and cable selection only.
 * DirectShow is used for OBS's registered DirectShow virtual camera; the
 * controller's video bridge remains OBS/WGC. No samples are written to disk.
 * The old qedit interfaces are declared here because current SDKs omit qedit.h.
 * ABI reference: Microsoft ISampleGrabber / ISampleGrabberCB documentation. */
#include "audio.h"
#include <dshow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Grabber Grabber;
typedef struct Callback Callback;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(Grabber *,REFIID,void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(Grabber *);
    ULONG (STDMETHODCALLTYPE *Release)(Grabber *);
    HRESULT (STDMETHODCALLTYPE *SetOneShot)(Grabber *,BOOL);
    HRESULT (STDMETHODCALLTYPE *SetMediaType)(Grabber *,const AM_MEDIA_TYPE *);
    HRESULT (STDMETHODCALLTYPE *GetConnectedMediaType)(Grabber *,AM_MEDIA_TYPE *);
    HRESULT (STDMETHODCALLTYPE *SetBufferSamples)(Grabber *,BOOL);
    HRESULT (STDMETHODCALLTYPE *GetCurrentBuffer)(Grabber *,long *,long *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentSample)(Grabber *,IMediaSample **);
    HRESULT (STDMETHODCALLTYPE *SetCallback)(Grabber *,Callback *,long);
} GrabberVtbl;
struct Grabber { const GrabberVtbl *lpVtbl; };
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(Callback *,REFIID,void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(Callback *);
    ULONG (STDMETHODCALLTYPE *Release)(Callback *);
    HRESULT (STDMETHODCALLTYPE *SampleCB)(Callback *,double,IMediaSample *);
    HRESULT (STDMETHODCALLTYPE *BufferCB)(Callback *,double,BYTE *,long);
} CallbackVtbl;
struct Callback { const CallbackVtbl *lpVtbl; volatile LONG references; };
static const GUID grabber_class={0xc1f400a0,0x3f08,0x11d3,{0x9f,0x0b,0,0x60,0x08,0x03,0x9e,0x37}};
static const GUID grabber_iid={0x6b652fff,0x11fe,0x4fce,{0x92,0xad,0x02,0x66,0xb5,0xd7,0xc7,0x8f}};
static const GUID callback_iid={0x0579154a,0x2b53,0x4994,{0xb0,0xd0,0xe7,0x73,0x14,0x8e,0xff,0x85}};
static const GUID null_renderer_class={0xc1f400a4,0x3f08,0x11d3,{0x9f,0x0b,0,0x60,0x08,0x03,0x9e,0x37}};
static struct {
    HWND window,status; CRITICAL_SECTION lock;
    IGraphBuilder *graph;ICaptureGraphBuilder2 *builder;IMediaControl *control;IMediaEvent *events;
    IBaseFilter *camera,*grabber_filter,*sink;Grabber *grabber;Callback callback;
    AudioMeter meter;BITMAPINFO bitmap;BYTE *pixels;size_t bytes,input_bytes;
    volatile LONG frames;ULONGLONG started,last_copy;int duration,width,height;
    uint64_t hash;unsigned changed;float maximum_peak,last_peak;
    bool running,failed;
} receiver;

static ULONG STDMETHODCALLTYPE callback_addref(Callback *c){return (ULONG)InterlockedIncrement(&c->references);}
static ULONG STDMETHODCALLTYPE callback_release(Callback *c){return (ULONG)InterlockedDecrement(&c->references);}
static HRESULT STDMETHODCALLTYPE callback_query(Callback *c,REFIID iid,void **out){
    if(!out)return E_POINTER;*out=NULL;
    if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&callback_iid)){*out=c;callback_addref(c);return S_OK;}return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE sample_callback(Callback *c,double time,IMediaSample *sample){(void)c;(void)time;(void)sample;return S_OK;}
static BYTE component(int value){return (BYTE)(value<0?0:value>255?255:value);}
static HRESULT STDMETHODCALLTYPE buffer_callback(Callback *c,double time,BYTE *buffer,long bytes){
    (void)c;(void)time;InterlockedIncrement(&receiver.frames);
    ULONGLONG now=GetTickCount64();if(now-receiver.last_copy<100||bytes<0||(size_t)bytes<receiver.input_bytes)return S_OK;
    EnterCriticalSection(&receiver.lock);if(receiver.pixels){
        size_t stride=(size_t)((receiver.width*3+3)&~3);
        for(int row=0;row<receiver.height;row++)for(int x=0;x<receiver.width;x+=2){
            const BYTE *p=buffer+((size_t)row*receiver.width+x)*2;int u=p[1]-128,v=p[3]-128;
            for(int k=0;k<2;k++){int y=p[k*2]-16;BYTE *q=receiver.pixels+(size_t)row*stride+(x+k)*3;
                q[0]=component((298*y+516*u+128)>>8);q[1]=component((298*y-100*u-208*v+128)>>8);q[2]=component((298*y+409*v+128)>>8);}
        }
        uint64_t hash=1469598103934665603ull;
        for(size_t i=0;i<receiver.input_bytes;i+=257){hash^=buffer[i];hash*=1099511628211ull;}
        if(receiver.hash&&receiver.hash!=hash)receiver.changed++;receiver.hash=hash;receiver.last_copy=now;
    }LeaveCriticalSection(&receiver.lock);return S_OK;
}
static const CallbackVtbl callback_vtable={callback_query,callback_addref,callback_release,sample_callback,buffer_callback};

static HRESULT select_camera(const wchar_t *name,IBaseFilter **out){
    ICreateDevEnum *devices=NULL;IEnumMoniker *list=NULL;IMoniker *selected=NULL,*moniker=NULL;int matches=0;
    HRESULT hr=CoCreateInstance(&CLSID_SystemDeviceEnum,NULL,CLSCTX_INPROC_SERVER,&IID_ICreateDevEnum,(void **)&devices);
    if(SUCCEEDED(hr))hr=ICreateDevEnum_CreateClassEnumerator(devices,&CLSID_VideoInputDeviceCategory,&list,0);
    if(hr==S_OK&&list)while(IEnumMoniker_Next(list,1,&moniker,NULL)==S_OK){
        IPropertyBag *bag=NULL;VARIANT value;VariantInit(&value);
        if(SUCCEEDED(IMoniker_BindToStorage(moniker,NULL,NULL,&IID_IPropertyBag,(void **)&bag))){
            if(SUCCEEDED(IPropertyBag_Read(bag,L"FriendlyName",&value,NULL))&&value.vt==VT_BSTR&&!wcscmp(value.bstrVal,name)){
                matches++;if(!selected){selected=moniker;IMoniker_AddRef(selected);}
            }VariantClear(&value);IPropertyBag_Release(bag);
        }IMoniker_Release(moniker);
    }
    hr=matches==1?IMoniker_BindToObject(selected,NULL,NULL,&IID_IBaseFilter,(void **)out):VFW_E_NOT_FOUND;
    if(selected)IMoniker_Release(selected);if(list)IEnumMoniker_Release(list);if(devices)ICreateDevEnum_Release(devices);return hr;
}
static void close_receiver(void){
    if(receiver.control)IMediaControl_Stop(receiver.control);
    if(receiver.grabber)receiver.grabber->lpVtbl->SetCallback(receiver.grabber,NULL,1);
    audio_meter_close(&receiver.meter);
    if(receiver.control)IMediaControl_Release(receiver.control);
    if(receiver.events)IMediaEvent_Release(receiver.events);
    if(receiver.grabber)receiver.grabber->lpVtbl->Release(receiver.grabber);
    if(receiver.camera)IBaseFilter_Release(receiver.camera);
    if(receiver.grabber_filter)IBaseFilter_Release(receiver.grabber_filter);
    if(receiver.sink)IBaseFilter_Release(receiver.sink);
    if(receiver.builder)ICaptureGraphBuilder2_Release(receiver.builder);
    if(receiver.graph)IGraphBuilder_Release(receiver.graph);
    receiver.control=NULL;receiver.events=NULL;receiver.grabber=NULL;receiver.camera=NULL;receiver.grabber_filter=NULL;
    receiver.sink=NULL;receiver.builder=NULL;receiver.graph=NULL;receiver.running=false;
}
static void free_media(AM_MEDIA_TYPE *m){if(!m)return;if(m->cbFormat)CoTaskMemFree(m->pbFormat);if(m->pUnk)IUnknown_Release(m->pUnk);CoTaskMemFree(m);}
static HRESULT select_yuy2_format(void){
    IAMStreamConfig *config=NULL;HRESULT hr=ICaptureGraphBuilder2_FindInterface(receiver.builder,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,receiver.camera,&IID_IAMStreamConfig,(void **)&config);
    if(FAILED(hr))return hr;AM_MEDIA_TYPE *current=NULL;int width=0,height=0;
    if(SUCCEEDED(IAMStreamConfig_GetFormat(config,&current))&&current->cbFormat>=sizeof(VIDEOINFOHEADER)&&IsEqualGUID(&current->formattype,&FORMAT_VideoInfo)){VIDEOINFOHEADER *v=(VIDEOINFOHEADER *)current->pbFormat;width=v->bmiHeader.biWidth;height=abs(v->bmiHeader.biHeight);}free_media(current);
    int count=0,size=0;hr=IAMStreamConfig_GetNumberOfCapabilities(config,&count,&size);
    if(FAILED(hr)||count<1||count>512||size<1||size>65536){IAMStreamConfig_Release(config);return VFW_E_INVALIDMEDIATYPE;}
    BYTE *capability=malloc((size_t)size);hr=VFW_E_INVALIDMEDIATYPE;
    if(capability)for(int i=0;i<count;i++){AM_MEDIA_TYPE *format=NULL;
        if(SUCCEEDED(IAMStreamConfig_GetStreamCaps(config,i,&format,capability))&&format&&IsEqualGUID(&format->subtype,&MEDIASUBTYPE_YUY2)&&IsEqualGUID(&format->formattype,&FORMAT_VideoInfo)&&format->cbFormat>=sizeof(VIDEOINFOHEADER)){
            VIDEOINFOHEADER *v=(VIDEOINFOHEADER *)format->pbFormat;if(v->bmiHeader.biWidth==width&&abs(v->bmiHeader.biHeight)==height){hr=IAMStreamConfig_SetFormat(config,format);free_media(format);break;}
        }free_media(format);
    }
    free(capability);IAMStreamConfig_Release(config);return hr;
}
static HRESULT open_receiver(const wchar_t *camera_name,const wchar_t *audio_name){
    AudioEndpoints *endpoints=calloc(1,sizeof(*endpoints));char error[1024]={0};int selected=-1,matches=0;
    if(endpoints&&audio_enumerate(endpoints,error,sizeof(error)))for(int i=0;i<endpoints->count;i++){
        AudioEndpoint *ep=&endpoints->items[i];if(ep->flow==eCapture&&ep->state==DEVICE_STATE_ACTIVE&&!wcscmp(ep->name,audio_name)){selected=i;matches++;}
    }
    if(matches!=1){puts("Explicit recording endpoint is absent or ambiguous. No default microphone was opened.");free(endpoints);return E_INVALIDARG;}
    bool audio_ok=audio_meter_open(&receiver.meter,endpoints->items[selected].id,error,sizeof(error));free(endpoints);
    if(!audio_ok){puts(error);return E_FAIL;}
    HRESULT hr=select_camera(camera_name,&receiver.camera);
    if(SUCCEEDED(hr))hr=CoCreateInstance(&CLSID_FilterGraph,NULL,CLSCTX_INPROC_SERVER,&IID_IGraphBuilder,(void **)&receiver.graph);
    if(SUCCEEDED(hr))hr=CoCreateInstance(&CLSID_CaptureGraphBuilder2,NULL,CLSCTX_INPROC_SERVER,&IID_ICaptureGraphBuilder2,(void **)&receiver.builder);
    if(SUCCEEDED(hr))hr=ICaptureGraphBuilder2_SetFiltergraph(receiver.builder,receiver.graph);
    if(SUCCEEDED(hr))hr=IGraphBuilder_AddFilter(receiver.graph,receiver.camera,L"Explicit OBS Virtual Camera");
    if(SUCCEEDED(hr))hr=select_yuy2_format();
    if(SUCCEEDED(hr))hr=CoCreateInstance(&grabber_class,NULL,CLSCTX_INPROC_SERVER,&IID_IBaseFilter,(void **)&receiver.grabber_filter);
    if(SUCCEEDED(hr))hr=IBaseFilter_QueryInterface(receiver.grabber_filter,&grabber_iid,(void **)&receiver.grabber);
    /* Request a format advertised by OBS itself, without a third-party decoder or converter. */
    AM_MEDIA_TYPE wanted={0};wanted.majortype=MEDIATYPE_Video;wanted.subtype=MEDIASUBTYPE_YUY2;wanted.formattype=FORMAT_VideoInfo;
    if(SUCCEEDED(hr))hr=receiver.grabber->lpVtbl->SetMediaType(receiver.grabber,&wanted);
    if(SUCCEEDED(hr))hr=IGraphBuilder_AddFilter(receiver.graph,receiver.grabber_filter,L"In-memory receiver samples");
    if(SUCCEEDED(hr))hr=CoCreateInstance(&null_renderer_class,NULL,CLSCTX_INPROC_SERVER,&IID_IBaseFilter,(void **)&receiver.sink);
    if(SUCCEEDED(hr))hr=IGraphBuilder_AddFilter(receiver.graph,receiver.sink,L"No recording or playback output");
    if(SUCCEEDED(hr))hr=ICaptureGraphBuilder2_RenderStream(receiver.builder,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,(IUnknown *)receiver.camera,receiver.grabber_filter,receiver.sink);
    AM_MEDIA_TYPE actual={0};
    if(SUCCEEDED(hr))hr=receiver.grabber->lpVtbl->GetConnectedMediaType(receiver.grabber,&actual);
    if(SUCCEEDED(hr)){
        if(!IsEqualGUID(&actual.formattype,&FORMAT_VideoInfo)||actual.cbFormat<sizeof(VIDEOINFOHEADER))hr=VFW_E_INVALIDMEDIATYPE;
        else{VIDEOINFOHEADER *v=(VIDEOINFOHEADER *)actual.pbFormat;receiver.width=v->bmiHeader.biWidth;receiver.height=abs(v->bmiHeader.biHeight);
            if(receiver.width<1||receiver.width>7680||(receiver.width&1)||receiver.height<1||receiver.height>4320||!IsEqualGUID(&actual.subtype,&MEDIASUBTYPE_YUY2))hr=VFW_E_INVALIDMEDIATYPE;
            else{receiver.bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);receiver.bitmap.bmiHeader.biWidth=receiver.width;receiver.bitmap.bmiHeader.biHeight=-receiver.height;receiver.bitmap.bmiHeader.biPlanes=1;receiver.bitmap.bmiHeader.biBitCount=24;receiver.bitmap.bmiHeader.biCompression=BI_RGB;
                receiver.input_bytes=(size_t)receiver.width*receiver.height*2;receiver.bytes=(size_t)((receiver.width*3+3)&~3)*receiver.height;receiver.pixels=calloc(receiver.bytes,1);if(!receiver.pixels)hr=E_OUTOFMEMORY;
                printf("Receiver negotiated: %dx%d YUY2; advertised frame interval %.3f ms. This is a receiver format, not proof of camera sensor detail.\n",receiver.width,receiver.height,v->AvgTimePerFrame/10000.0);}
        }
    }
    if(actual.cbFormat)CoTaskMemFree(actual.pbFormat);if(actual.pUnk)IUnknown_Release(actual.pUnk);
    receiver.callback.lpVtbl=&callback_vtable;receiver.callback.references=1;
    if(SUCCEEDED(hr))hr=receiver.grabber->lpVtbl->SetOneShot(receiver.grabber,FALSE);
    if(SUCCEEDED(hr))hr=receiver.grabber->lpVtbl->SetCallback(receiver.grabber,&receiver.callback,1);
    if(SUCCEEDED(hr))hr=IGraphBuilder_QueryInterface(receiver.graph,&IID_IMediaControl,(void **)&receiver.control);
    if(SUCCEEDED(hr))hr=IGraphBuilder_QueryInterface(receiver.graph,&IID_IMediaEvent,(void **)&receiver.events);
    /* OBS's source paces its own thread. Avoid a null renderer waiting on an
     * absolute timestamp from before the graph's Run transition. */
    if(SUCCEEDED(hr)){IMediaFilter *filter=NULL;hr=IGraphBuilder_QueryInterface(receiver.graph,&IID_IMediaFilter,(void **)&filter);if(SUCCEEDED(hr)){hr=IMediaFilter_SetSyncSource(filter,NULL);IMediaFilter_Release(filter);}}
    if(SUCCEEDED(hr))hr=IMediaControl_Pause(receiver.control);
    if(SUCCEEDED(hr)){receiver.started=GetTickCount64();hr=IMediaControl_Run(receiver.control);receiver.running=SUCCEEDED(hr);}
    return hr;
}
static LRESULT CALLBACK receiver_proc(HWND window,UINT msg,WPARAM w,LPARAM l){
    (void)l;switch(msg){
    case WM_CREATE:receiver.window=window;receiver.status=CreateWindowW(L"STATIC",L"Opening the explicit local endpoints...",WS_CHILD|WS_VISIBLE,16,400,790,145,window,NULL,NULL,NULL);CreateWindowW(L"BUTTON",L"Stop local receiver",WS_CHILD|WS_VISIBLE|WS_TABSTOP,16,552,210,30,window,(HMENU)1,NULL,NULL);return 0;
    case WM_TIMER:{long event=0;LONG_PTR a=0,b=0;while(receiver.events&&IMediaEvent_GetEvent(receiver.events,&event,&a,&b,0)==S_OK){IMediaEvent_FreeEventParams(receiver.events,event,a,b);if(event==EC_ERRORABORT){printf("Receiver graph reported EC_ERRORABORT: 0x%08lX\n",(unsigned long)a);receiver.failed=true;DestroyWindow(window);return 0;}}
        float peak=0;if(!audio_meter_tick(&receiver.meter,&peak)){receiver.failed=true;DestroyWindow(window);return 0;}receiver.last_peak=peak;if(peak>receiver.maximum_peak)receiver.maximum_peak=peak;
        double elapsed=(GetTickCount64()-receiver.started)/1000.0;LONG frames=InterlockedCompareExchange(&receiver.frames,0,0);wchar_t status[1024];
        swprintf(status,1024,L"EXPLICIT OBS Virtual Camera + selected cable recording endpoint\nNegotiated receiver: %dx%d. Frames received: %ld; mean %.2f FPS over %.1f s.\nCurrent cable peak %.4f; maximum %.4f. No speaker monitoring or recordings.\nPhone source and clap synchronization require a person at the phone.\nLeave this open while pressing Stop in the manager to inspect output cleanup.",receiver.width,receiver.height,frames,frames/(elapsed>0?elapsed:1),elapsed,peak,receiver.maximum_peak);
        SetWindowTextW(receiver.status,status);InvalidateRect(window,NULL,FALSE);if(elapsed>=receiver.duration)DestroyWindow(window);return 0;}
    case WM_PAINT:{PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);RECT area={16,16,806,385};FillRect(dc,&area,(HBRUSH)GetStockObject(BLACK_BRUSH));EnterCriticalSection(&receiver.lock);
        if(receiver.pixels&&receiver.width){double scale=790.0/receiver.width;if(receiver.height*scale>369)scale=369.0/receiver.height;int width=(int)(receiver.width*scale),height=(int)(receiver.height*scale);SetStretchBltMode(dc,HALFTONE);StretchDIBits(dc,16+(790-width)/2,16+(369-height)/2,width,height,0,0,receiver.width,receiver.height,receiver.pixels,&receiver.bitmap,DIB_RGB_COLORS,SRCCOPY);}
        LeaveCriticalSection(&receiver.lock);EndPaint(window,&paint);return 0;}
    case WM_COMMAND:if(LOWORD(w)==1){DestroyWindow(window);return 0;}break;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);close_receiver();PostQuitMessage(0);return 0;
    }return DefWindowProcW(window,msg,w,l);
}
int wmain(int argc,wchar_t **argv){
    wchar_t camera[256]={0},microphone[256]={0};receiver.duration=60;
    for(int i=1;i<argc;i++){
        if(i+1<argc&&!wcscmp(argv[i],L"--camera"))awm_wcopy(camera,256,argv[++i]);
        else if(i+1<argc&&!wcscmp(argv[i],L"--microphone"))awm_wcopy(microphone,256,argv[++i]);
        else if(i+1<argc&&!wcscmp(argv[i],L"--duration"))receiver.duration=_wtoi(argv[++i]);else{puts("Invalid receiver arguments.");return 2;}
    }
    if(wcscmp(camera,L"OBS Virtual Camera")||!microphone[0]||receiver.duration<1||receiver.duration>3600){puts("Usage: awm-receiver --camera \"OBS Virtual Camera\" --microphone \"EXACT cable recording friendly name\" [--duration 1..3600]\nNo default or PC physical device fallback. No calls, recordings, or speaker playback.");return 2;}
    CoInitializeEx(NULL,COINIT_MULTITHREADED);InitializeCriticalSection(&receiver.lock);
    wchar_t description[768];swprintf(description,768,L"Explicit selection: %s / %s\n",camera,microphone);char text[2304];wide_to_utf8(description,text,sizeof(text));puts(text);fflush(stdout);
    HINSTANCE instance=GetModuleHandleW(NULL);WNDCLASSW cls={0};cls.hInstance=instance;cls.lpfnWndProc=receiver_proc;cls.lpszClassName=L"AWMLocalReceiver";cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"WinDroid Capture - explicit local receiver",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,40,40,840,640,NULL,NULL,instance,NULL);
    HRESULT hr=window?open_receiver(camera,microphone):E_FAIL;
    if(FAILED(hr)){printf("Explicit receiver failed: HRESULT 0x%08lX. No fallback was used.\n",(unsigned long)hr);receiver.failed=true;if(window)DestroyWindow(window);else close_receiver();}
    else{printf("Receiver graph Run returned 0x%08lX.\n",(unsigned long)hr);fflush(stdout);ShowWindow(window,SW_SHOW);SetTimer(window,1,100,NULL);MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
    double elapsed=receiver.started?(GetTickCount64()-receiver.started)/1000.0:0;LONG frames=InterlockedCompareExchange(&receiver.frames,0,0);
    printf("Local receiving application: %ld frame callbacks over %.2f s; mean %.2f FPS; %u changed in-memory image samples.\nSelected recording endpoint maximum peak: %.4f. Phone-specific source and synchronization: require human verification.\nSelected receiver stopped. No media was saved.\n",frames,elapsed,frames/(elapsed>0?elapsed:1),receiver.changed,receiver.maximum_peak);
    free(receiver.pixels);DeleteCriticalSection(&receiver.lock);CoUninitialize();return receiver.failed||!frames?1:0;
}
