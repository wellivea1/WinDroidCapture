/* Component tests exercise the actual COM source, not Windows registration. */
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <stdio.h>
#include <string.h>
#include "../camera/camera_ids.h"
#include "../camera/pacer.h"
#include "../src/native/protocol.h"
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
typedef struct MockV3Server {HANDLE pipe,ready,shm_file,shm_map;BYTE *shm_view;uint32_t shm_pid,shm_id;volatile LONG requests_seen,v3_ok;} MockV3Server;
static DWORD WINAPI mock_v3_proc(void *ctx){
    MockV3Server *s=ctx;SetEvent(s->ready);
    if(!ConnectNamedPipe(s->pipe,NULL)&&GetLastError()!=ERROR_PIPE_CONNECTED)return 0;
    for(int req=0;req<2;req++){
        AwmCameraRequest q={0};DWORD got=0;
        if(!ReadFile(s->pipe,&q,sizeof(q),&got,NULL)||got!=sizeof(q))break;
        InterlockedIncrement(&s->requests_seen);
        if(q.magic==AWM_FRAME_MAGIC&&q.version==AWM_CAMERA_VERSION_V3)InterlockedExchange(&s->v3_ok,1);
        uint32_t bytes=q.width*q.height*3/2;
        AwmCameraReplyV3 r3={0};r3.base.magic=AWM_FRAME_MAGIC;r3.base.version=AWM_CAMERA_VERSION_V3;r3.base.width=q.width;r3.base.height=q.height;r3.base.format=q.format;r3.base.generation=42;
        if(req==0&&s->shm_view){r3.base.live=1;r3.base.bytes=0;r3.shm_pid=s->shm_pid;r3.shm_id=s->shm_id;r3.shm_bytes=bytes;MemoryBarrier();}
        else{r3.base.live=0;r3.base.bytes=0;r3.shm_bytes=0;}
        DWORD wrote=0;if(!WriteFile(s->pipe,&r3,sizeof(r3),&wrote,NULL)||wrote!=sizeof(r3))break;
    }
    DisconnectNamedPipe(s->pipe);return 0;
}
static int pull_sample_bytes(IMFMediaStream *stream,BYTE *first_y,BYTE *first_u,DWORD *total_bytes){
    CHECK(IMFMediaStream_RequestSample(stream,NULL));
    ULONGLONG deadline=GetTickCount64()+5000;
    while(GetTickCount64()<deadline){
        IMFMediaEvent *event=NULL;HRESULT hr=IMFMediaStream_GetEvent(stream,MF_EVENT_FLAG_NO_WAIT,&event);
        if(hr==MF_E_NO_EVENTS_AVAILABLE){Sleep(10);continue;}
        CHECK(hr);MediaEventType type;CHECK(IMFMediaEvent_GetType(event,&type));
        if(type==MEMediaSample){
            PROPVARIANT value;PropVariantInit(&value);CHECK(IMFMediaEvent_GetValue(event,&value));
            IMFSample *sample=NULL;CHECK(IUnknown_QueryInterface(value.punkVal,&IID_IMFSample,(void **)&sample));
            CHECK(IMFSample_GetTotalLength(sample,total_bytes));
            IMFMediaBuffer *buf=NULL;CHECK(IMFSample_GetBufferByIndex(sample,0,&buf));
            BYTE *p=NULL;DWORD max=0,cur=0;CHECK(IMFMediaBuffer_Lock(buf,&p,&max,&cur));
            if(first_y)*first_y=p[0];
            if(first_u&&cur>=1920*1080+2)*first_u=p[1920*1080];
            IMFMediaBuffer_Unlock(buf);IMFMediaBuffer_Release(buf);IMFSample_Release(sample);PropVariantClear(&value);IMFMediaEvent_Release(event);
            return 0;
        }
        IMFMediaEvent_Release(event);
    }
    return 1;
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
    DWORD bytes=0;BYTE y0=0,u0=0;
    if(pull_sample_bytes(stream,&y0,&u0,&bytes)||bytes!=1920*1080*3/2)return 5;
    /* Re-Start with same media type (FrameServer shared client join) must send MEUpdatedStream and keep delivering cached stopped samples. */
    CHECK(IMFMediaSource_Start(source,pd,&GUID_NULL,&position));
    BOOL saw_updated=FALSE;ULONGLONG deadline=GetTickCount64()+2000;
    while(GetTickCount64()<deadline&&!saw_updated){HRESULT hr=IMFMediaSource_GetEvent(source,MF_EVENT_FLAG_NO_WAIT,&event);if(hr==MF_E_NO_EVENTS_AVAILABLE){Sleep(10);continue;}CHECK(hr);CHECK(IMFMediaEvent_GetType(event,&type));if(type==MEUpdatedStream)saw_updated=TRUE;IMFMediaEvent_Release(event);}
    if(!saw_updated)return 11;
    if(pull_sample_bytes(stream,&y0,&u0,&bytes)||bytes!=1920*1080*3/2)return 12;
    /* Verify v3 cross-session SHM zero-pipe-payload transfer and 0-byte stopped reply. */
    if(!WaitNamedPipeW(AWM_CAMERA_PIPE,1)){
        MockV3Server mock={0};wchar_t dir[MAX_PATH],path[MAX_PATH];
        if(GetEnvironmentVariableW(L"ProgramData",dir,MAX_PATH-80)){
            swprintf(path,MAX_PATH,L"%s\\AndroidWebcamManager",dir);CreateDirectoryW(path,NULL);
            uint32_t pid=GetCurrentProcessId();
            swprintf(path,MAX_PATH,L"%s\\AndroidWebcamManager\\awm-cam-%u-777.shm",dir,pid);
            mock.shm_file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
            if(mock.shm_file!=INVALID_HANDLE_VALUE){mock.shm_pid=pid;mock.shm_id=777;}
            else{swprintf(path,MAX_PATH,L"%s\\AndroidWebcamManager\\awm-cam-slot-3.shm",dir);mock.shm_file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,NULL);mock.shm_pid=0;mock.shm_id=3;}
            if(mock.shm_file!=INVALID_HANDLE_VALUE){
                mock.shm_map=CreateFileMappingW(mock.shm_file,NULL,PAGE_READWRITE,0,AWM_FRAME_BYTES,NULL);
                if(mock.shm_map)mock.shm_view=MapViewOfFile(mock.shm_map,FILE_MAP_ALL_ACCESS,0,0,AWM_FRAME_BYTES);
            }
            if(mock.shm_view){
                memset(mock.shm_view,0x91,1920u*1080);memset(mock.shm_view+1920u*1080,0x62,1920u*540);
                mock.pipe=CreateNamedPipeW(AWM_CAMERA_PIPE,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
                mock.ready=CreateEventW(NULL,TRUE,FALSE,NULL);
                if(mock.pipe!=INVALID_HANDLE_VALUE&&mock.ready){
                    HANDLE th=CreateThread(NULL,0,mock_v3_proc,&mock,0,NULL);
                    WaitForSingleObject(mock.ready,2000);
                    BYTE live_y=0,live_u=0,stop_y=0,stop_u=0;DWORD b1=0,b2=0;
                    if(pull_sample_bytes(stream,&live_y,&live_u,&b1)||b1!=1920*1080*3/2||live_y!=0x91||live_u!=0x62){printf("FAIL v3 SHM live sample y=%02x u=%02x\n",live_y,live_u);return 14;}
                    if(pull_sample_bytes(stream,&stop_y,&stop_u,&b2)||b2!=1920*1080*3/2||stop_y==0x91){printf("FAIL v3 0-byte stopped sample y=%02x\n",stop_y);return 15;}
                    WaitForSingleObject(th,3000);CloseHandle(th);
                    if(!mock.v3_ok||mock.requests_seen<2)return 16;
                    puts("PASS camera v3 SHM zero-pipe-payload transfer and 0-byte stopped reply");
                }
                if(mock.ready)CloseHandle(mock.ready);if(mock.pipe&&mock.pipe!=INVALID_HANDLE_VALUE)CloseHandle(mock.pipe);
                UnmapViewOfFile(mock.shm_view);
            }
            if(mock.shm_map)CloseHandle(mock.shm_map);if(mock.shm_file&&mock.shm_file!=INVALID_HANDLE_VALUE)CloseHandle(mock.shm_file);
        }
    }
    CHECK(IMFMediaSource_Stop(source));CHECK(IMFMediaSource_Shutdown(source));if(IMFMediaStream_RequestSample(stream,NULL)!=MF_E_SHUTDOWN)return 7;IMFMediaStream_Release(stream);IMFPresentationDescriptor_Release(pd);IMFMediaSource_Release(source);IClassFactory_Release(factory);
    if(unload()!=S_OK)return 8;FreeLibrary(dll);MFShutdown();CoUninitialize();puts("PASS actual COM activation, event delivery, stopped image, v3 SHM, stop, shutdown, unload; registration/phone delivery not tested");return 0;
}
