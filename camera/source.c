#define CONST_VTABLE
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
/* ksproxy.h omits inherited IUnknown slots in its C declaration. Keep the
 * documented COM ABI explicitly in C, including all three inherited slots. */
typedef struct IKsControl IKsControl;
typedef struct IKsControlVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IKsControl *,REFIID,void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IKsControl *);
    ULONG (STDMETHODCALLTYPE *Release)(IKsControl *);
    HRESULT (STDMETHODCALLTYPE *KsProperty)(IKsControl *,PKSPROPERTY,ULONG,void *,ULONG,ULONG *);
    HRESULT (STDMETHODCALLTYPE *KsMethod)(IKsControl *,PKSMETHOD,ULONG,void *,ULONG,ULONG *);
    HRESULT (STDMETHODCALLTYPE *KsEvent)(IKsControl *,PKSEVENT,ULONG,void *,ULONG,ULONG *);
} IKsControlVtbl;
struct IKsControl {const IKsControlVtbl *lpVtbl;};
static const GUID IID_IKsControl={0x28f54685,0x06fd,0x11d2,{0xb2,0x7a,0,0xa0,0xc9,0x22,0x31,0x96}};
#include <propvarutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "camera_ids.h"
#include "pacer.h"
#include "../src/native/frame_hub.h"
#define RELEASE(p) do{if(p){IUnknown_Release((IUnknown *)(p));(p)=NULL;}}while(0)
static LONG objects;
static HMODULE module;
/* Local API diagnostics only; no pixels, audio, device IDs or credentials. */
static void trace(const char *name,HRESULT hr){wchar_t p[1024];DWORD n=GetEnvironmentVariableW(L"ProgramData",p,900);if(!n||n>=900)return;wcscat_s(p,1024,L"\\AndroidWebcamManager\\camera-events.log");HANDLE f=CreateFileW(p,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,0,NULL);if(f==INVALID_HANDLE_VALUE)return;char b[256];int len=snprintf(b,sizeof(b),"%llu pid=%lu %s hr=%08lX\r\n",(unsigned long long)GetTickCount64(),GetCurrentProcessId(),name,(unsigned long)hr);DWORD wrote;WriteFile(f,b,len,&wrote,NULL);CloseHandle(f);}
static const GUID awm_capture_pin={STATIC_PINNAME_VIDEO_CAPTURE};
static const GUID awm_legacy_profile={STATIC_KSCAMERAPROFILE_Legacy};
typedef struct Source Source;
typedef struct Stream {
    IMFMediaStream2 iface;LONG refs;Source *parent;CRITICAL_SECTION lock;
    IMFMediaEventQueue *events;IMFStreamDescriptor *descriptor;IMFAttributes *attributes;
    IMFVideoSampleAllocator *allocator;MF_STREAM_STATE state;BOOL shutdown;
    HANDLE wake,cancel,timer,thread,pipe;IUnknown *tokens[32];unsigned head,count;
    UINT32 width,height,fps,format;BOOL selected;LONGLONG next_time;
} Stream;
struct Source {
    IMFMediaSourceEx iface;IMFGetService services;IKsControl ks;IMFSampleAllocatorControl allocator_control;
    LONG refs;CRITICAL_SECTION lock;BOOL shutdown,started;
    IMFMediaEventQueue *events;IMFPresentationDescriptor *presentation;IMFAttributes *attributes;Stream *stream;
};
typedef struct Activate {IMFActivate iface;LONG refs;CRITICAL_SECTION lock;IMFAttributes *attributes;Source *source;} Activate;
typedef struct Factory {IClassFactory iface;LONG refs;} Factory;
static HRESULT source_create(IMFAttributes *attributes,Source **out);
static HRESULT STDMETHODCALLTYPE source_shutdown(IMFMediaSourceEx *iface);
static ULONG STDMETHODCALLTYPE source_add(IMFMediaSourceEx *i){return (ULONG)InterlockedIncrement(&((Source *)i)->refs);}
static ULONG STDMETHODCALLTYPE source_release(IMFMediaSourceEx *i){
    Source *s=(Source *)i;LONG n=InterlockedDecrement(&s->refs);if(!n){if(!s->shutdown)source_shutdown(i);RELEASE(s->events);RELEASE(s->presentation);RELEASE(s->attributes);DeleteCriticalSection(&s->lock);InterlockedDecrement(&objects);free(s);}return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE source_query(IMFMediaSourceEx *i,REFIID id,void **out){
    if(!out)return E_POINTER;*out=NULL;Source *s=(Source *)i;
    if(IsEqualIID(id,&IID_IUnknown)||IsEqualIID(id,&IID_IMFMediaEventGenerator)||IsEqualIID(id,&IID_IMFMediaSource)||IsEqualIID(id,&IID_IMFMediaSourceEx))*out=i;
    else if(IsEqualIID(id,&IID_IMFGetService))*out=&s->services;
    else if(IsEqualIID(id,&IID_IKsControl))*out=&s->ks;
    else if(IsEqualIID(id,&IID_IMFSampleAllocatorControl))*out=&s->allocator_control;
    else return E_NOINTERFACE;source_add(i);return S_OK;
}
static ULONG STDMETHODCALLTYPE stream_add(IMFMediaStream2 *i){return (ULONG)InterlockedIncrement(&((Stream *)i)->refs);}
static ULONG STDMETHODCALLTYPE stream_release(IMFMediaStream2 *i){
    Stream *s=(Stream *)i;LONG n=InterlockedDecrement(&s->refs);if(!n){RELEASE(s->events);RELEASE(s->descriptor);RELEASE(s->attributes);RELEASE(s->allocator);if(s->pipe)CloseHandle(s->pipe);if(s->wake)CloseHandle(s->wake);if(s->cancel)CloseHandle(s->cancel);if(s->timer)CloseHandle(s->timer);DeleteCriticalSection(&s->lock);free(s);InterlockedDecrement(&objects);}return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE stream_query(IMFMediaStream2 *i,REFIID id,void **out){
    if(!out)return E_POINTER;*out=NULL;if(!IsEqualIID(id,&IID_IUnknown)&&!IsEqualIID(id,&IID_IMFMediaEventGenerator)&&!IsEqualIID(id,&IID_IMFMediaStream)&&!IsEqualIID(id,&IID_IMFMediaStream2))return E_NOINTERFACE;*out=i;stream_add(i);return S_OK;
}
#define EVENT_METHODS(prefix,Type,cast) \
static HRESULT STDMETHODCALLTYPE prefix##_get(Type *i,DWORD f,IMFMediaEvent **e){cast *s=(cast *)i;return s->shutdown?MF_E_SHUTDOWN:IMFMediaEventQueue_GetEvent(s->events,f,e);} \
static HRESULT STDMETHODCALLTYPE prefix##_begin(Type *i,IMFAsyncCallback *c,IUnknown *v){cast *s=(cast *)i;return s->shutdown?MF_E_SHUTDOWN:IMFMediaEventQueue_BeginGetEvent(s->events,c,v);} \
static HRESULT STDMETHODCALLTYPE prefix##_end(Type *i,IMFAsyncResult *r,IMFMediaEvent **e){cast *s=(cast *)i;return s->shutdown?MF_E_SHUTDOWN:IMFMediaEventQueue_EndGetEvent(s->events,r,e);} \
static HRESULT STDMETHODCALLTYPE prefix##_queue(Type *i,MediaEventType t,REFGUID g,HRESULT h,const PROPVARIANT *v){cast *s=(cast *)i;return s->shutdown?MF_E_SHUTDOWN:IMFMediaEventQueue_QueueEventParamVar(s->events,t,g,h,v);}
EVENT_METHODS(source,IMFMediaSourceEx,Source)
EVENT_METHODS(stream,IMFMediaStream2,Stream)
static void stopped_rgb(BYTE *p,UINT32 w,UINT32 h){
    for(size_t k=0;k<(size_t)w*h;k++){p[k*4]=38;p[k*4+1]=32;p[k*4+2]=28;p[k*4+3]=255;}
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=(LONG)w;info.bmiHeader.biHeight=-(LONG)h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void *bits=NULL;
    HDC dc=CreateCompatibleDC(NULL);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,NULL,0);if(bitmap&&bits){memcpy(bits,p,(size_t)w*h*4);HGDIOBJ old=SelectObject(dc,bitmap);HFONT font=CreateFontW(-(int)(h/22),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");HGDIOBJ oldfont=SelectObject(dc,font);
        SetTextColor(dc,RGB(210,218,226));SetBkMode(dc,TRANSPARENT);RECT r={0,(LONG)(h/2-40),(LONG)w,(LONG)h};DrawTextW(dc,L"Android camera stopped\nOpen WinDroid Capture and start streaming",-1,&r,DT_CENTER|DT_TOP);GdiFlush();memcpy(p,bits,(size_t)w*h*4);SelectObject(dc,oldfont);SelectObject(dc,old);DeleteObject(font);}if(bitmap)DeleteObject(bitmap);DeleteDC(dc);
}
static bool read_pixels(Stream *s,BYTE *pixels,DWORD bytes){
    if(!s->pipe){s->pipe=CreateFileW(AWM_CAMERA_PIPE,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);if(s->pipe==INVALID_HANDLE_VALUE){s->pipe=NULL;return false;}}
    AwmCameraRequest q={AWM_FRAME_MAGIC,2,s->width,s->height,s->format};AwmCameraReply r;
    bool ok=awm_pipe_io(s->pipe,&q,sizeof(q),true,s->cancel,250)&&awm_pipe_io(s->pipe,&r,sizeof(r),false,s->cancel,250);
    if(ok)ok=r.magic==AWM_FRAME_MAGIC&&r.version==2&&r.width==s->width&&r.height==s->height&&r.format==s->format&&r.bytes==bytes;
    if(ok)ok=awm_pipe_io(s->pipe,pixels,bytes,false,s->cancel,250);
    if(!ok){CancelIoEx(s->pipe,NULL);CloseHandle(s->pipe);s->pipe=NULL;return false;}return r.live!=0;
}
static HRESULT make_sample(Stream *s,IUnknown *token){
    DWORD bytes=s->width*s->height*(s->format==AWM_CAMERA_RGB32?4:3)/ (s->format==AWM_CAMERA_RGB32?1:2);
    BYTE *pixels=malloc(bytes);if(!pixels)return E_OUTOFMEMORY;
    if(!read_pixels(s,pixels,bytes)){if(s->format==AWM_CAMERA_RGB32)stopped_rgb(pixels,s->width,s->height);else{BYTE *rgb=malloc((size_t)s->width*s->height*4);if(!rgb){free(pixels);return E_OUTOFMEMORY;}stopped_rgb(rgb,s->width,s->height);awm_bgra_to_nv12(rgb,s->width,s->height,pixels);free(rgb);}}
    IMFSample *sample=NULL;IMFMediaBuffer *buffer=NULL;HRESULT hr;
    if(s->allocator)hr=IMFVideoSampleAllocator_AllocateSample(s->allocator,&sample);
    else{hr=MFCreateSample(&sample);if(SUCCEEDED(hr)){hr=MFCreateMemoryBuffer(bytes,&buffer);if(SUCCEEDED(hr))hr=IMFSample_AddBuffer(sample,buffer);RELEASE(buffer);}}
    if(SUCCEEDED(hr))hr=IMFSample_GetBufferByIndex(sample,0,&buffer);
    IMF2DBuffer *two=NULL;
    if(SUCCEEDED(hr)&&SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer,&IID_IMF2DBuffer,(void **)&two))){
        BYTE *scan=NULL;LONG stride=0;hr=IMF2DBuffer_Lock2D(two,&scan,&stride);
        if(SUCCEEDED(hr)){UINT32 row_bytes=s->width*(s->format==1?4:1),rows=s->format==1?s->height:s->height*3/2;
            if((uint64_t)(stride<0?-(int64_t)stride:stride)<row_bytes)hr=MF_E_BUFFERTOOSMALL;
            else for(UINT32 y=0;y<rows;y++)memcpy(scan+(ptrdiff_t)y*stride,pixels+(size_t)y*row_bytes,row_bytes);
            IMF2DBuffer_Unlock2D(two);
        }
    }else if(SUCCEEDED(hr)){BYTE *p=NULL;DWORD max=0;hr=IMFMediaBuffer_Lock(buffer,&p,&max,NULL);if(SUCCEEDED(hr)){if(max<bytes)hr=MF_E_BUFFERTOOSMALL;else memcpy(p,pixels,bytes);IMFMediaBuffer_Unlock(buffer);}}
    if(SUCCEEDED(hr))hr=IMFMediaBuffer_SetCurrentLength(buffer,bytes);
    if(SUCCEEDED(hr))hr=IMFSample_SetSampleTime(sample,MFGetSystemTime());
    if(SUCCEEDED(hr))hr=IMFSample_SetSampleDuration(sample,10000000/s->fps);
    if(SUCCEEDED(hr)&&token)hr=IMFSample_SetUnknown(sample,&MFSampleExtension_Token,token);
    if(SUCCEEDED(hr))hr=IMFMediaEventQueue_QueueEventParamUnk(s->events,MEMediaSample,&GUID_NULL,S_OK,(IUnknown *)sample);
    RELEASE(two);RELEASE(buffer);RELEASE(sample);free(pixels);return hr;
}
static DWORD WINAPI stream_worker(void *context){
    Stream *s=context;CoInitializeEx(NULL,COINIT_MULTITHREADED);
    while(WaitForSingleObject(s->cancel,0)!=WAIT_OBJECT_0){
        HANDLE waits[]={s->cancel,s->wake};if(WaitForMultipleObjects(2,waits,FALSE,1000)==WAIT_OBJECT_0)break;
        for(;;){EnterCriticalSection(&s->lock);if(s->shutdown||s->state!=MF_STREAM_STATE_RUNNING||!s->count){LeaveCriticalSection(&s->lock);break;}
            IUnknown *token=s->tokens[s->head];s->tokens[s->head]=NULL;s->head=(s->head+1)%32;s->count--;LONGLONG now=awm_pacer_now(),due=s->next_time;if(due<now-10000000/s->fps)due=now;s->next_time=due+10000000/s->fps;LeaveCriticalSection(&s->lock);
            int paced=awm_pacer_wait(s->timer,s->cancel,due);if(paced<=0){RELEASE(token);if(paced<0)IMFMediaEventQueue_QueueEventParamVar(s->events,MEError,&GUID_NULL,HRESULT_FROM_WIN32(GetLastError()),NULL);break;}
            EnterCriticalSection(&s->lock);HRESULT hr=s->shutdown||s->state!=MF_STREAM_STATE_RUNNING?MF_E_INVALIDREQUEST:make_sample(s,token);LeaveCriticalSection(&s->lock);RELEASE(token);
            if(FAILED(hr)&&hr!=MF_E_INVALIDREQUEST&&hr!=MF_E_SHUTDOWN)IMFMediaEventQueue_QueueEventParamVar(s->events,MEError,&GUID_NULL,hr,NULL);
        }
    }CoUninitialize();return 0;
}
static HRESULT STDMETHODCALLTYPE stream_parent(IMFMediaStream2 *i,IMFMediaSource **out){if(!out)return E_POINTER;Stream *s=(Stream *)i;EnterCriticalSection(&s->lock);HRESULT hr=s->shutdown||!s->parent?MF_E_SHUTDOWN:source_query(&s->parent->iface,&IID_IMFMediaSource,(void **)out);LeaveCriticalSection(&s->lock);return hr;}
static HRESULT STDMETHODCALLTYPE stream_descriptor(IMFMediaStream2 *i,IMFStreamDescriptor **out){Stream *s=(Stream *)i;if(!out)return E_POINTER;if(s->shutdown)return MF_E_SHUTDOWN;*out=s->descriptor;IMFStreamDescriptor_AddRef(*out);return S_OK;}
static HRESULT STDMETHODCALLTYPE stream_request(IMFMediaStream2 *i,IUnknown *token){Stream *s=(Stream *)i;EnterCriticalSection(&s->lock);HRESULT hr=S_OK;
    if(s->shutdown)hr=MF_E_SHUTDOWN;else if(s->state!=MF_STREAM_STATE_RUNNING)hr=MF_E_INVALIDREQUEST;else if(s->count==32)hr=MF_E_NOTACCEPTING;else{unsigned tail=(s->head+s->count)%32;s->tokens[tail]=token;if(token)IUnknown_AddRef(token);s->count++;SetEvent(s->wake);}LeaveCriticalSection(&s->lock);return hr;}
static void clear_tokens(Stream *s){while(s->count){IUnknown *t=s->tokens[s->head];s->tokens[s->head]=NULL;s->head=(s->head+1)%32;s->count--;RELEASE(t);}}
static HRESULT STDMETHODCALLTYPE stream_set_state(IMFMediaStream2 *i,MF_STREAM_STATE state){Stream *s=(Stream *)i;if(state!=MF_STREAM_STATE_RUNNING&&state!=MF_STREAM_STATE_STOPPED&&state!=MF_STREAM_STATE_PAUSED)return E_INVALIDARG;EnterCriticalSection(&s->lock);HRESULT hr=s->shutdown?MF_E_SHUTDOWN:S_OK;if(SUCCEEDED(hr)){s->state=state;if(state!=MF_STREAM_STATE_RUNNING)clear_tokens(s);}LeaveCriticalSection(&s->lock);return hr;}
static HRESULT STDMETHODCALLTYPE stream_get_state(IMFMediaStream2 *i,MF_STREAM_STATE *state){if(!state)return E_POINTER;Stream *s=(Stream *)i;*state=s->state;return s->shutdown?MF_E_SHUTDOWN:S_OK;}
static const IMFMediaStream2Vtbl stream_vtable={stream_query,stream_add,stream_release,stream_get,stream_begin,stream_end,stream_queue,stream_parent,stream_descriptor,stream_request,stream_set_state,stream_get_state};
static HRESULT STDMETHODCALLTYPE source_characteristics(IMFMediaSourceEx *i,DWORD *out){if(!out)return E_POINTER;if(((Source *)i)->shutdown)return MF_E_SHUTDOWN;*out=MFMEDIASOURCE_IS_LIVE;return S_OK;}
static HRESULT STDMETHODCALLTYPE source_presentation(IMFMediaSourceEx *i,IMFPresentationDescriptor **out){Source *s=(Source *)i;if(!out)return E_POINTER;return s->shutdown?MF_E_SHUTDOWN:IMFPresentationDescriptor_Clone(s->presentation,out);}
static HRESULT configure_stream(Stream *s,IMFMediaType *type){
    UINT64 size=0,rate=0;GUID subtype;HRESULT hr=IMFMediaType_GetUINT64(type,&MF_MT_FRAME_SIZE,&size);if(SUCCEEDED(hr))hr=IMFMediaType_GetUINT64(type,&MF_MT_FRAME_RATE,&rate);if(SUCCEEDED(hr))hr=IMFMediaType_GetGUID(type,&MF_MT_SUBTYPE,&subtype);
    UINT32 w=(UINT32)(size>>32),h=(UINT32)size,fps=(UINT32)(rate>>32),den=(UINT32)rate;if(FAILED(hr)||!awm_frame_shape(w,h)||den!=1||(fps!=30&&fps!=60)||(!IsEqualGUID(&subtype,&MFVideoFormat_NV12)&&!IsEqualGUID(&subtype,&MFVideoFormat_RGB32)))return MF_E_INVALIDMEDIATYPE;
    s->width=w;s->height=h;s->fps=fps;s->format=IsEqualGUID(&subtype,&MFVideoFormat_NV12)?2:1;
    if(s->allocator){IMFVideoSampleAllocator_UninitializeSampleAllocator(s->allocator);hr=IMFVideoSampleAllocator_InitializeSampleAllocator(s->allocator,8,type);}return hr;
}
static HRESULT STDMETHODCALLTYPE source_start(IMFMediaSourceEx *i,IMFPresentationDescriptor *presentation,const GUID *time,const PROPVARIANT *position){
    Source *s=(Source *)i;if(!presentation||!position)return E_POINTER;if(time&&!IsEqualGUID(time,&GUID_NULL))return MF_E_UNSUPPORTED_TIME_FORMAT;if(position->vt!=VT_EMPTY&&(position->vt!=VT_I8||position->hVal.QuadPart!=0))return MF_E_INVALIDREQUEST;
    EnterCriticalSection(&s->lock);if(s->shutdown){LeaveCriticalSection(&s->lock);return MF_E_SHUTDOWN;}DWORD count=0;HRESULT hr=IMFPresentationDescriptor_GetStreamDescriptorCount(presentation,&count);BOOL selected=FALSE;IMFStreamDescriptor *descriptor=NULL;IMFMediaTypeHandler *handler=NULL;IMFMediaType *type=NULL;
    if(SUCCEEDED(hr)&&count!=1)hr=E_INVALIDARG;if(SUCCEEDED(hr))hr=IMFPresentationDescriptor_GetStreamDescriptorByIndex(presentation,0,&selected,&descriptor);
    DWORD id=1;if(SUCCEEDED(hr))hr=IMFStreamDescriptor_GetStreamIdentifier(descriptor,&id);if(SUCCEEDED(hr)&&id!=0)hr=E_INVALIDARG;
    if(SUCCEEDED(hr)&&selected)hr=IMFStreamDescriptor_GetMediaTypeHandler(descriptor,&handler);if(SUCCEEDED(hr)&&selected)hr=IMFMediaTypeHandler_GetCurrentMediaType(handler,&type);
    Stream *stream=s->stream;EnterCriticalSection(&stream->lock);
    if(SUCCEEDED(hr)&&selected){hr=configure_stream(stream,type);if(SUCCEEDED(hr)){IMFMediaTypeHandler *own=NULL;hr=IMFStreamDescriptor_GetMediaTypeHandler(stream->descriptor,&own);if(SUCCEEDED(hr))hr=IMFMediaTypeHandler_SetCurrentMediaType(own,type);RELEASE(own);}}
    if(SUCCEEDED(hr)){stream->selected=selected;stream->state=selected?MF_STREAM_STATE_RUNNING:MF_STREAM_STATE_STOPPED;stream->next_time=awm_pacer_now();clear_tokens(stream);}
    LeaveCriticalSection(&stream->lock);if(SUCCEEDED(hr)&&selected){IMFMediaEventQueue_QueueEventParamUnk(s->events,s->started?MEUpdatedStream:MENewStream,&GUID_NULL,S_OK,(IUnknown *)&stream->iface);IMFMediaEventQueue_QueueEventParamVar(stream->events,MEStreamStarted,&GUID_NULL,S_OK,position);}
    if(SUCCEEDED(hr)){s->started=TRUE;IMFMediaEventQueue_QueueEventParamVar(s->events,MESourceStarted,&GUID_NULL,S_OK,position);}
    RELEASE(type);RELEASE(handler);RELEASE(descriptor);LeaveCriticalSection(&s->lock);return hr;
}
static HRESULT STDMETHODCALLTYPE source_stop(IMFMediaSourceEx *i){Source *s=(Source *)i;EnterCriticalSection(&s->lock);if(s->shutdown){LeaveCriticalSection(&s->lock);return MF_E_SHUTDOWN;}stream_set_state(&s->stream->iface,MF_STREAM_STATE_STOPPED);IMFMediaEventQueue_QueueEventParamVar(s->stream->events,MEStreamStopped,&GUID_NULL,S_OK,NULL);IMFMediaEventQueue_QueueEventParamVar(s->events,MESourceStopped,&GUID_NULL,S_OK,NULL);s->started=FALSE;LeaveCriticalSection(&s->lock);return S_OK;}
static HRESULT STDMETHODCALLTYPE source_pause(IMFMediaSourceEx *i){return ((Source *)i)->shutdown?MF_E_SHUTDOWN:MF_E_INVALID_STATE_TRANSITION;}
static HRESULT STDMETHODCALLTYPE source_shutdown(IMFMediaSourceEx *i){
    trace("source.Shutdown",S_OK);
    Source *s=(Source *)i;EnterCriticalSection(&s->lock);if(s->shutdown){LeaveCriticalSection(&s->lock);return S_OK;}s->shutdown=TRUE;Stream *stream=s->stream;
    if(stream){EnterCriticalSection(&stream->lock);stream->shutdown=TRUE;stream->state=MF_STREAM_STATE_STOPPED;clear_tokens(stream);SetEvent(stream->cancel);if(stream->pipe)CancelIoEx(stream->pipe,NULL);LeaveCriticalSection(&stream->lock);}
    if(s->events)IMFMediaEventQueue_Shutdown(s->events);LeaveCriticalSection(&s->lock);
    if(stream){if(stream->thread){WaitForSingleObject(stream->thread,INFINITE);CloseHandle(stream->thread);stream->thread=NULL;}IMFMediaEventQueue_Shutdown(stream->events);stream->parent=NULL;s->stream=NULL;stream_release(&stream->iface);source_release(i);}return S_OK;
}
static HRESULT STDMETHODCALLTYPE source_attributes(IMFMediaSourceEx *i,IMFAttributes **out){Source *s=(Source *)i;if(!out)return E_POINTER;if(s->shutdown)return MF_E_SHUTDOWN;*out=s->attributes;IMFAttributes_AddRef(*out);return S_OK;}
static HRESULT STDMETHODCALLTYPE source_stream_attributes(IMFMediaSourceEx *i,DWORD id,IMFAttributes **out){Source *s=(Source *)i;if(!out)return E_POINTER;if(s->shutdown)return MF_E_SHUTDOWN;if(id)return MF_E_INVALIDSTREAMNUMBER;*out=s->stream->attributes;IMFAttributes_AddRef(*out);return S_OK;}
static HRESULT STDMETHODCALLTYPE source_d3d(IMFMediaSourceEx *i,IUnknown *manager){(void)manager;return ((Source *)i)->shutdown?MF_E_SHUTDOWN:E_NOTIMPL;}
static const IMFMediaSourceExVtbl source_vtable={source_query,source_add,source_release,source_get,source_begin,source_end,source_queue,source_characteristics,source_presentation,source_start,source_stop,source_pause,source_shutdown,source_attributes,source_stream_attributes,source_d3d};
#define SUBUNKNOWN(prefix,Type,member) \
static HRESULT STDMETHODCALLTYPE prefix##_query(Type *i,REFIID id,void **out){return source_query(&CONTAINING_RECORD(i,Source,member)->iface,id,out);} \
static ULONG STDMETHODCALLTYPE prefix##_add(Type *i){return source_add(&CONTAINING_RECORD(i,Source,member)->iface);} \
static ULONG STDMETHODCALLTYPE prefix##_release(Type *i){return source_release(&CONTAINING_RECORD(i,Source,member)->iface);}
SUBUNKNOWN(service,IMFGetService,services)
SUBUNKNOWN(ks,IKsControl,ks)
SUBUNKNOWN(alloc,IMFSampleAllocatorControl,allocator_control)
static HRESULT STDMETHODCALLTYPE service_get(IMFGetService *i,REFGUID service,REFIID id,void **out){(void)i;(void)service;(void)id;if(!out)return E_POINTER;*out=NULL;return MF_E_UNSUPPORTED_SERVICE;}
static const IMFGetServiceVtbl service_vtable={service_query,service_add,service_release,service_get};
static HRESULT STDMETHODCALLTYPE ks_property(IKsControl *i,PKSPROPERTY p,ULONG n,void *data,ULONG bytes,ULONG *returned){(void)i;(void)data;(void)bytes;if(!returned)return E_POINTER;*returned=0;if(!p||n<sizeof(KSPROPERTY))return E_INVALIDARG;return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);}
static HRESULT STDMETHODCALLTYPE ks_method(IKsControl *i,PKSMETHOD p,ULONG n,void *data,ULONG bytes,ULONG *returned){return ks_property(i,(PKSPROPERTY)p,n,data,bytes,returned);}
static HRESULT STDMETHODCALLTYPE ks_event(IKsControl *i,PKSEVENT p,ULONG n,void *data,ULONG bytes,ULONG *returned){(void)i;(void)p;(void)n;(void)data;(void)bytes;if(returned)*returned=0;return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);}
static const IKsControlVtbl ks_vtable={ks_query,ks_add,ks_release,ks_property,ks_method,ks_event};
static HRESULT STDMETHODCALLTYPE allocator_set(IMFSampleAllocatorControl *i,DWORD id,IUnknown *value){
    Source *s=CONTAINING_RECORD(i,Source,allocator_control);if(!value)return E_POINTER;if(id)return MF_E_INVALIDSTREAMNUMBER;EnterCriticalSection(&s->lock);HRESULT hr=s->shutdown?MF_E_SHUTDOWN:S_OK;
    if(SUCCEEDED(hr)){IMFVideoSampleAllocator *a=NULL;hr=IUnknown_QueryInterface(value,&IID_IMFVideoSampleAllocator,(void **)&a);if(SUCCEEDED(hr)){Stream *stream=s->stream;EnterCriticalSection(&stream->lock);RELEASE(stream->allocator);stream->allocator=a;LeaveCriticalSection(&stream->lock);}}LeaveCriticalSection(&s->lock);return hr;
}
static HRESULT STDMETHODCALLTYPE allocator_usage(IMFSampleAllocatorControl *i,DWORD id,DWORD *input,MFSampleAllocatorUsage *usage){Source *s=CONTAINING_RECORD(i,Source,allocator_control);if(!input||!usage)return E_POINTER;if(s->shutdown)return MF_E_SHUTDOWN;if(id)return MF_E_INVALIDSTREAMNUMBER;*input=0;*usage=MFSampleAllocatorUsage_UsesProvidedAllocator;return S_OK;}
static const IMFSampleAllocatorControlVtbl allocator_vtable={alloc_query,alloc_add,alloc_release,allocator_set,allocator_usage};
static HRESULT media_type(UINT32 w,UINT32 h,UINT32 fps,REFGUID subtype,IMFMediaType **out){
    HRESULT hr=MFCreateMediaType(out);if(FAILED(hr))return hr;IMFMediaType *t=*out;IMFMediaType_SetGUID(t,&MF_MT_MAJOR_TYPE,&MFMediaType_Video);IMFMediaType_SetGUID(t,&MF_MT_SUBTYPE,subtype);IMFMediaType_SetUINT32(t,&MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);IMFMediaType_SetUINT32(t,&MF_MT_ALL_SAMPLES_INDEPENDENT,TRUE);IMFMediaType_SetUINT64(t,&MF_MT_FRAME_SIZE,((UINT64)w<<32)|h);IMFMediaType_SetUINT64(t,&MF_MT_FRAME_RATE,((UINT64)fps<<32)|1);IMFMediaType_SetUINT64(t,&MF_MT_PIXEL_ASPECT_RATIO,((UINT64)1<<32)|1);IMFMediaType_SetUINT32(t,&MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT709);IMFMediaType_SetUINT32(t,&MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT709);IMFMediaType_SetUINT32(t,&MF_MT_VIDEO_NOMINAL_RANGE,IsEqualGUID(subtype,&MFVideoFormat_NV12)?MFNominalRange_16_235:MFNominalRange_0_255);IMFMediaType_SetUINT32(t,&MF_MT_DEFAULT_STRIDE,IsEqualGUID(subtype,&MFVideoFormat_NV12)?w:w*4);return S_OK;
}
static HRESULT source_create(IMFAttributes *attributes,Source **out){
    trace("source.Create",S_OK);
    *out=NULL;Source *s=calloc(1,sizeof(*s));Stream *st=calloc(1,sizeof(*st));if(!s||!st){free(s);free(st);return E_OUTOFMEMORY;}
    InterlockedAdd(&objects,2);s->iface.lpVtbl=&source_vtable;s->services.lpVtbl=&service_vtable;s->ks.lpVtbl=&ks_vtable;s->allocator_control.lpVtbl=&allocator_vtable;s->refs=1;InitializeCriticalSection(&s->lock);
    st->iface.lpVtbl=&stream_vtable;st->refs=1;st->parent=s;source_add(&s->iface);InitializeCriticalSection(&st->lock);st->state=MF_STREAM_STATE_STOPPED;st->width=1920;st->height=1080;st->fps=30;st->format=2;s->stream=st;st->wake=CreateEventW(NULL,FALSE,FALSE,NULL);st->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    HRESULT hr=MFCreateEventQueue(&s->events);if(SUCCEEDED(hr))hr=MFCreateEventQueue(&st->events);if(SUCCEEDED(hr))hr=MFCreateAttributes(&s->attributes,8);if(SUCCEEDED(hr)&&attributes)hr=IMFAttributes_CopyAllItems(attributes,s->attributes);if(SUCCEEDED(hr))hr=MFCreateAttributes(&st->attributes,6);
    IMFSensorProfileCollection *profiles=NULL;IMFSensorProfile *legacy=NULL;
    if(SUCCEEDED(hr))hr=MFCreateSensorProfileCollection(&profiles);
    if(SUCCEEDED(hr))hr=MFCreateSensorProfile(&awm_legacy_profile,0,NULL,&legacy);
    if(SUCCEEDED(hr))hr=IMFSensorProfile_AddProfileFilter(legacy,0,L"((RES==;FRT<=60,1;SUT==))");
    if(SUCCEEDED(hr))hr=IMFSensorProfileCollection_AddProfile(profiles,legacy);
    if(SUCCEEDED(hr))hr=IMFAttributes_SetUnknown(s->attributes,&MF_DEVICEMFT_SENSORPROFILE_COLLECTION,(IUnknown *)profiles);
    RELEASE(legacy);RELEASE(profiles);
    IMFMediaType *types[8]={0};const UINT32 sizes[4][3]={{1920,1080,30},{1280,720,30},{3840,2160,30},{1920,1080,60}};
    if(SUCCEEDED(hr)){IMFAttributes_SetGUID(st->attributes,&MF_DEVICESTREAM_STREAM_CATEGORY,&awm_capture_pin);IMFAttributes_SetUINT32(st->attributes,&MF_DEVICESTREAM_STREAM_ID,0);IMFAttributes_SetUINT32(st->attributes,&MF_DEVICESTREAM_FRAMESERVER_SHARED,1);IMFAttributes_SetUINT32(st->attributes,&MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,MFFrameSourceTypes_Color);
        for(unsigned k=0;k<4&&SUCCEEDED(hr);k++){hr=media_type(sizes[k][0],sizes[k][1],sizes[k][2],&MFVideoFormat_NV12,&types[k*2]);if(SUCCEEDED(hr))hr=media_type(sizes[k][0],sizes[k][1],sizes[k][2],&MFVideoFormat_RGB32,&types[k*2+1]);}}
    if(SUCCEEDED(hr))hr=MFCreateStreamDescriptor(0,8,types,&st->descriptor);
    if(SUCCEEDED(hr)){IMFMediaTypeHandler *handler=NULL;hr=IMFStreamDescriptor_GetMediaTypeHandler(st->descriptor,&handler);if(SUCCEEDED(hr))hr=IMFMediaTypeHandler_SetCurrentMediaType(handler,types[0]);RELEASE(handler);IMFAttributes_CopyAllItems(st->attributes,(IMFAttributes *)st->descriptor);}
    if(SUCCEEDED(hr))hr=MFCreatePresentationDescriptor(1,&st->descriptor,&s->presentation);if(SUCCEEDED(hr))hr=IMFPresentationDescriptor_SelectStream(s->presentation,0);for(unsigned k=0;k<8;k++)RELEASE(types[k]);
    if(SUCCEEDED(hr)&&(!st->wake||!st->cancel))hr=E_OUTOFMEMORY;
    if(SUCCEEDED(hr)){st->timer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);if(!st->timer)hr=HRESULT_FROM_WIN32(GetLastError());}
    if(SUCCEEDED(hr)){st->thread=CreateThread(NULL,0,stream_worker,st,0,NULL);if(!st->thread)hr=HRESULT_FROM_WIN32(GetLastError());}
    trace("source.Create result",hr);if(FAILED(hr)){source_shutdown(&s->iface);source_release(&s->iface);return hr;}*out=s;return S_OK;
}
static Activate *act(IMFActivate *i){return (Activate *)i;}
static ULONG STDMETHODCALLTYPE act_add(IMFActivate *i){return (ULONG)InterlockedIncrement(&act(i)->refs);}
static HRESULT STDMETHODCALLTYPE act_shutdown(IMFActivate *i){Activate *a=act(i);EnterCriticalSection(&a->lock);if(a->source){source_shutdown(&a->source->iface);source_release(&a->source->iface);a->source=NULL;}LeaveCriticalSection(&a->lock);return S_OK;}
static ULONG STDMETHODCALLTYPE act_release(IMFActivate *i){Activate *a=act(i);LONG n=InterlockedDecrement(&a->refs);if(!n){/* Releasing activation must not shut down the separately referenced source.
 * Frame Server drops this object immediately after ActivateObject. */
    if(a->source){if(InterlockedCompareExchange(&a->source->refs,0,0)==2)source_shutdown(&a->source->iface);source_release(&a->source->iface);a->source=NULL;}RELEASE(a->attributes);DeleteCriticalSection(&a->lock);free(a);InterlockedDecrement(&objects);}return (ULONG)n;}
static HRESULT STDMETHODCALLTYPE act_query(IMFActivate *i,REFIID id,void **out){if(!out)return E_POINTER;*out=NULL;if(!IsEqualIID(id,&IID_IUnknown)&&!IsEqualIID(id,&IID_IMFAttributes)&&!IsEqualIID(id,&IID_IMFActivate))return E_NOINTERFACE;*out=i;act_add(i);return S_OK;}
#define ATTR(name,parameters,arguments) static HRESULT STDMETHODCALLTYPE act_##name parameters {return act(i)->attributes->lpVtbl->name arguments;}
ATTR(GetItem,(IMFActivate *i,REFGUID k,PROPVARIANT *v),(act(i)->attributes,k,v))
ATTR(GetItemType,(IMFActivate *i,REFGUID k,MF_ATTRIBUTE_TYPE *v),(act(i)->attributes,k,v))
ATTR(CompareItem,(IMFActivate *i,REFGUID k,REFPROPVARIANT v,BOOL *r),(act(i)->attributes,k,v,r))
ATTR(Compare,(IMFActivate *i,IMFAttributes *a,MF_ATTRIBUTES_MATCH_TYPE t,BOOL *r),(act(i)->attributes,a,t,r))
ATTR(GetUINT32,(IMFActivate *i,REFGUID k,UINT32 *v),(act(i)->attributes,k,v))
ATTR(GetUINT64,(IMFActivate *i,REFGUID k,UINT64 *v),(act(i)->attributes,k,v))
ATTR(GetDouble,(IMFActivate *i,REFGUID k,double *v),(act(i)->attributes,k,v))
ATTR(GetGUID,(IMFActivate *i,REFGUID k,GUID *v),(act(i)->attributes,k,v))
ATTR(GetStringLength,(IMFActivate *i,REFGUID k,UINT32 *v),(act(i)->attributes,k,v))
ATTR(GetString,(IMFActivate *i,REFGUID k,LPWSTR v,UINT32 n,UINT32 *r),(act(i)->attributes,k,v,n,r))
ATTR(GetAllocatedString,(IMFActivate *i,REFGUID k,LPWSTR *v,UINT32 *r),(act(i)->attributes,k,v,r))
ATTR(GetBlobSize,(IMFActivate *i,REFGUID k,UINT32 *n),(act(i)->attributes,k,n))
ATTR(GetBlob,(IMFActivate *i,REFGUID k,UINT8 *v,UINT32 n,UINT32 *r),(act(i)->attributes,k,v,n,r))
ATTR(GetAllocatedBlob,(IMFActivate *i,REFGUID k,UINT8 **v,UINT32 *n),(act(i)->attributes,k,v,n))
ATTR(GetUnknown,(IMFActivate *i,REFGUID k,REFIID id,void **v),(act(i)->attributes,k,id,v))
ATTR(SetItem,(IMFActivate *i,REFGUID k,REFPROPVARIANT v),(act(i)->attributes,k,v))
ATTR(DeleteItem,(IMFActivate *i,REFGUID k),(act(i)->attributes,k))
ATTR(DeleteAllItems,(IMFActivate *i),(act(i)->attributes))
ATTR(SetUINT32,(IMFActivate *i,REFGUID k,UINT32 v),(act(i)->attributes,k,v))
ATTR(SetUINT64,(IMFActivate *i,REFGUID k,UINT64 v),(act(i)->attributes,k,v))
ATTR(SetDouble,(IMFActivate *i,REFGUID k,double v),(act(i)->attributes,k,v))
ATTR(SetGUID,(IMFActivate *i,REFGUID k,REFGUID v),(act(i)->attributes,k,v))
ATTR(SetString,(IMFActivate *i,REFGUID k,LPCWSTR v),(act(i)->attributes,k,v))
ATTR(SetBlob,(IMFActivate *i,REFGUID k,const UINT8 *v,UINT32 n),(act(i)->attributes,k,v,n))
ATTR(SetUnknown,(IMFActivate *i,REFGUID k,IUnknown *v),(act(i)->attributes,k,v))
ATTR(LockStore,(IMFActivate *i),(act(i)->attributes))
ATTR(UnlockStore,(IMFActivate *i),(act(i)->attributes))
ATTR(GetCount,(IMFActivate *i,UINT32 *n),(act(i)->attributes,n))
ATTR(GetItemByIndex,(IMFActivate *i,UINT32 n,GUID *k,PROPVARIANT *v),(act(i)->attributes,n,k,v))
ATTR(CopyAllItems,(IMFActivate *i,IMFAttributes *v),(act(i)->attributes,v))
static HRESULT STDMETHODCALLTYPE act_activate(IMFActivate *i,REFIID id,void **out){if(!out)return E_POINTER;*out=NULL;Activate *a=act(i);EnterCriticalSection(&a->lock);if(a->source&&a->source->shutdown){source_release(&a->source->iface);a->source=NULL;}HRESULT hr=a->source?S_OK:source_create(a->attributes,&a->source);if(SUCCEEDED(hr))hr=source_query(&a->source->iface,id,out);LeaveCriticalSection(&a->lock);return hr;}
static HRESULT STDMETHODCALLTYPE act_detach(IMFActivate *i){Activate *a=act(i);EnterCriticalSection(&a->lock);if(a->source){source_release(&a->source->iface);a->source=NULL;}LeaveCriticalSection(&a->lock);return S_OK;}
static const IMFActivateVtbl act_vtable={act_query,act_add,act_release,act_GetItem,act_GetItemType,act_CompareItem,act_Compare,act_GetUINT32,act_GetUINT64,act_GetDouble,act_GetGUID,act_GetStringLength,act_GetString,act_GetAllocatedString,act_GetBlobSize,act_GetBlob,act_GetAllocatedBlob,act_GetUnknown,act_SetItem,act_DeleteItem,act_DeleteAllItems,act_SetUINT32,act_SetUINT64,act_SetDouble,act_SetGUID,act_SetString,act_SetBlob,act_SetUnknown,act_LockStore,act_UnlockStore,act_GetCount,act_GetItemByIndex,act_CopyAllItems,act_activate,act_shutdown,act_detach};
static ULONG STDMETHODCALLTYPE factory_add(IClassFactory *i){return (ULONG)InterlockedIncrement(&((Factory *)i)->refs);}
static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *i){Factory *f=(Factory *)i;LONG n=InterlockedDecrement(&f->refs);if(!n){free(f);InterlockedDecrement(&objects);}return (ULONG)n;}
static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *i,REFIID id,void **out){if(!out)return E_POINTER;*out=NULL;if(!IsEqualIID(id,&IID_IUnknown)&&!IsEqualIID(id,&IID_IClassFactory))return E_NOINTERFACE;*out=i;factory_add(i);return S_OK;}
static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *i,IUnknown *outer,REFIID id,void **out){(void)i;if(!out)return E_POINTER;*out=NULL;if(outer)return CLASS_E_NOAGGREGATION;Activate *a=calloc(1,sizeof(*a));if(!a)return E_OUTOFMEMORY;a->iface.lpVtbl=&act_vtable;a->refs=1;InitializeCriticalSection(&a->lock);InterlockedIncrement(&objects);HRESULT hr=MFCreateAttributes(&a->attributes,8);if(SUCCEEDED(hr))hr=act_query(&a->iface,id,out);act_release(&a->iface);return hr;}
static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *i,BOOL lock){(void)i;InterlockedAdd(&objects,lock?1:-1);return S_OK;}
static const IClassFactoryVtbl factory_vtable={factory_query,factory_add,factory_release,factory_create,factory_lock};
__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID id,REFIID iid,void **out){if(!out)return E_POINTER;*out=NULL;if(!IsEqualCLSID(id,&CLSID_AwmCamera))return CLASS_E_CLASSNOTAVAILABLE;Factory *f=calloc(1,sizeof(*f));if(!f)return E_OUTOFMEMORY;f->iface.lpVtbl=&factory_vtable;f->refs=1;InterlockedIncrement(&objects);HRESULT hr=factory_query(&f->iface,iid,out);factory_release(&f->iface);return hr;}
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void){return InterlockedCompareExchange(&objects,0,0)?S_FALSE:S_OK;}
__declspec(dllexport) HRESULT WINAPI DllRegisterServer(void){
    wchar_t path[32768];if(!GetModuleFileNameW(module,path,32768))return HRESULT_FROM_WIN32(GetLastError());HKEY key=NULL;LONG r=RegCreateKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID\\" AWM_CAMERA_CLSID L"\\InprocServer32",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);
    if(r!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(r);r=RegSetValueExW(key,NULL,0,REG_SZ,(BYTE *)path,(DWORD)((wcslen(path)+1)*2));if(r==ERROR_SUCCESS)r=RegSetValueExW(key,L"ThreadingModel",0,REG_SZ,(BYTE *)L"Both",10);RegCloseKey(key);return HRESULT_FROM_WIN32(r);
}
__declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void){LONG r=RegDeleteTreeW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID\\" AWM_CAMERA_CLSID);return r==ERROR_FILE_NOT_FOUND?S_OK:HRESULT_FROM_WIN32(r);}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved){(void)reserved;if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);}return TRUE;}
