/* Component tests exercise the actual COM source, not Windows registration. */
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <stdio.h>
#include "../camera/camera_ids.h"
#include "../camera/pacer.h"
#define CHECK(x) do{HRESULT h=(x);if(FAILED(h)){printf("FAIL line %d HRESULT %08lX\n",__LINE__,(unsigned long)h);return 1;}}while(0)
static int test_pacer(void){
    HANDLE timer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE),cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!timer||!cancel)return 1;
    for(unsigned fps=30;fps<=60;fps+=30){LONGLONG start=awm_pacer_now(),previous=start;double max_gap=0,total_error=0;unsigned n=30;
        for(unsigned k=1;k<=n;k++){LONGLONG due=start+(LONGLONG)k*10000000/fps;if(awm_pacer_wait(timer,cancel,due)!=1)return 1;LONGLONG now=awm_pacer_now();double gap=(now-previous)/10000.0,error=(now-due)/10000.0;previous=now;if(gap>max_gap)max_gap=gap;total_error+=error;if(now<due-10000)return 1;}
        double elapsed=(previous-start)/10000.0;if(elapsed<1000.0*n/fps-1||elapsed>1000.0*n/fps+100)return 1;
        printf("SYNTHETIC CAMERA PACER %u FPS: mean deadline wake error=%.3f ms max interval=%.3f ms; no camera or receiver evidence\n",fps,total_error/n,max_gap);
    }
    SetEvent(cancel);LONGLONG begin=awm_pacer_now();if(awm_pacer_wait(timer,cancel,begin+10000000)!=0||awm_pacer_now()-begin>1000000)return 1;CloseHandle(timer);CloseHandle(cancel);return 0;
}
int wmain(int argc,wchar_t **argv){
    if(argc!=2)return 2;if(test_pacer())return 10;CoInitializeEx(NULL,COINIT_MULTITHREADED);CHECK(MFStartup(MF_VERSION,MFSTARTUP_FULL));
    HMODULE dll=LoadLibraryW(argv[1]);if(!dll)return 3;
    HRESULT (WINAPI *get)(REFCLSID,REFIID,void **)=(void *)GetProcAddress(dll,"DllGetClassObject");
    HRESULT (WINAPI *unload)(void)=(void *)GetProcAddress(dll,"DllCanUnloadNow");
    IClassFactory *factory=NULL;IMFActivate *activation=NULL;IMFMediaSource *source=NULL;IMFPresentationDescriptor *pd=NULL;IMFMediaEvent *event=NULL;IMFMediaStream *stream=NULL;
    CHECK(get(&CLSID_AwmCamera,&IID_IClassFactory,(void **)&factory));CHECK(IClassFactory_CreateInstance(factory,NULL,&IID_IMFActivate,(void **)&activation));CHECK(IMFActivate_ActivateObject(activation,&IID_IMFMediaSource,(void **)&source));
    IMFActivate_Release(activation);activation=NULL; /* Match Frame Server's activation lifetime. */
    CHECK(IMFMediaSource_CreatePresentationDescriptor(source,&pd));BOOL selected;IMFStreamDescriptor *sd=NULL;IMFMediaTypeHandler *handler=NULL;IMFMediaType *rgb=NULL;UINT32 range=0;
    CHECK(IMFPresentationDescriptor_GetStreamDescriptorByIndex(pd,0,&selected,&sd));CHECK(IMFStreamDescriptor_GetMediaTypeHandler(sd,&handler));CHECK(IMFMediaTypeHandler_GetMediaTypeByIndex(handler,1,&rgb));CHECK(IMFMediaType_GetUINT32(rgb,&MF_MT_VIDEO_NOMINAL_RANGE,&range));if(range!=MFNominalRange_0_255)return 9;IMFMediaType_Release(rgb);IMFMediaTypeHandler_Release(handler);IMFStreamDescriptor_Release(sd);
    PROPVARIANT position;PropVariantInit(&position);CHECK(IMFMediaSource_Start(source,pd,&GUID_NULL,&position));
    CHECK(IMFMediaSource_GetEvent(source,0,&event));MediaEventType type;CHECK(IMFMediaEvent_GetType(event,&type));if(type!=MENewStream)return 4;PROPVARIANT value;PropVariantInit(&value);CHECK(IMFMediaEvent_GetValue(event,&value));CHECK(IUnknown_QueryInterface(value.punkVal,&IID_IMFMediaStream,(void **)&stream));PropVariantClear(&value);IMFMediaEvent_Release(event);
    CHECK(IMFMediaStream_RequestSample(stream,NULL));unsigned samples=0;ULONGLONG deadline=GetTickCount64()+5000;
    while(GetTickCount64()<deadline&&!samples){HRESULT hr=IMFMediaStream_GetEvent(stream,MF_EVENT_FLAG_NO_WAIT,&event);if(hr==MF_E_NO_EVENTS_AVAILABLE){Sleep(10);continue;}CHECK(hr);CHECK(IMFMediaEvent_GetType(event,&type));if(type==MEMediaSample){PropVariantInit(&value);CHECK(IMFMediaEvent_GetValue(event,&value));IMFSample *sample=NULL;CHECK(IUnknown_QueryInterface(value.punkVal,&IID_IMFSample,(void **)&sample));DWORD bytes=0;CHECK(IMFSample_GetTotalLength(sample,&bytes));if(bytes!=1920*1080*3/2)return 5;samples++;IMFSample_Release(sample);PropVariantClear(&value);}IMFMediaEvent_Release(event);}
    if(!samples)return 6;CHECK(IMFMediaSource_Stop(source));CHECK(IMFMediaSource_Shutdown(source));if(IMFMediaStream_RequestSample(stream,NULL)!=MF_E_SHUTDOWN)return 7;IMFMediaStream_Release(stream);IMFPresentationDescriptor_Release(pd);IMFMediaSource_Release(source);IClassFactory_Release(factory);
    if(unload()!=S_OK)return 8;FreeLibrary(dll);MFShutdown();CoUninitialize();puts("PASS actual COM activation, event delivery, stopped image, stop, shutdown, unload; registration/phone delivery not tested");return 0;
}
