/* Actual D3D11 preview worker, synthetic moving pixels. No phone or receiver
   proof. High-resolution producer timing does not change global timer policy. */
#include "../src/native/preview_worker.h"
#include "../src/native/frame_hub.h"
#include "../src/native/timing.h"
#include <string.h>
static struct {AwmHub hub;HANDLE cancel;unsigned fps;BYTE *pixels;volatile LONG produced;} test;
static DWORD WINAPI produce(void *unused){
    (void)unused;HANDLE timer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);if(!timer)return 1;
    double start=awm_clock_ms();unsigned n=0;
    while(WaitForSingleObject(test.cancel,0)!=WAIT_OBJECT_0){
        LARGE_INTEGER due;double remaining=start+n*1000.0/test.fps-awm_clock_ms();due.QuadPart=-(LONGLONG)(remaining>0?remaining*10000:1);
        if(!SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE))break;HANDLE waits[]={test.cancel,timer};if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0+1)break;
        unsigned row=(n*12)%900;memset(test.pixels,22,1920u*1080*4);for(unsigned y=row;y<row+80;y++)memset(test.pixels+(size_t)y*1920*4,210,1920*4);
        if(!awm_hub_publish(&test.hub,test.pixels,1920,1080,(int64_t)n*1000000/test.fps))break;InterlockedIncrement(&test.produced);n++;
    }
    CloseHandle(timer);return 0;
}
static LRESULT CALLBACK window_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}if(m==WM_CLOSE){SetEvent(test.cancel);return 0;}return DefWindowProcW(h,m,w,l);
}
int wmain(int argc,wchar_t **argv){
    bool report_only=argc==2&&!wcscmp(argv[1],L"--report-only");if(argc>1&&!report_only)return 2;int failures=0;
    WNDCLASSW c={0};c.lpfnWndProc=window_proc;c.hInstance=GetModuleHandleW(NULL);c.lpszClassName=L"AWMSyntheticPacing";RegisterClassW(&c);
    test.cancel=CreateEventW(NULL,TRUE,FALSE,NULL);test.pixels=malloc(1920u*1080*4);if(!test.cancel||!test.pixels)return 2;
    HWND window=CreateWindowW(c.lpszClassName,L"AWM synthetic preview pacing — no phone",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,960,600,NULL,NULL,c.hInstance,NULL);if(!window)return 2;
    for(unsigned pass=0;pass<2;pass++){
        test.fps=pass?60:30;test.produced=0;ResetEvent(test.cancel);wchar_t name[200];swprintf(name,200,L"Local\\AWM.Camera.Frames.Pacing.%lu.%u",GetCurrentProcessId(),pass);
        if(!awm_hub_open_named(&test.hub,true,name))return 3;test.hub.view->source_kind=1;
        PreviewWorker *worker=preview_worker_create(window,NULL);if(!worker)return 3;preview_worker_update(worker,name,true,AWM_SCALE_EWA_LANCZOS,0);
        HANDLE producer=CreateThread(NULL,0,produce,NULL,0,NULL);PreviewStats warm={0},end={0};double begin=awm_clock_ms();bool warmed=false;
        while(awm_clock_ms()-begin<6000&&WaitForSingleObject(test.cancel,0)!=WAIT_OBJECT_0){
            MSG msg;while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(!warmed&&awm_clock_ms()-begin>=1500){preview_worker_stats(worker,&warm);warmed=true;}MsgWaitForMultipleObjects(1,&test.cancel,FALSE,10,QS_ALLINPUT);
        }
        /* Compare source identities at the same locked presentation snapshots:
           two independently sampled producer counters admit a +/-1 boundary. */
        preview_worker_stats(worker,&end);uint64_t produced=end.last_source_frame-warm.last_source_frame,shown=end.presented-warm.presented,skipped=end.skipped-warm.skipped;
        bool ok=warmed&&produced>test.fps*3&&shown<=produced&&shown*100>=produced*85&&!preview_worker_failed(worker);
        printf("SYNTHETIC PREVIEW %u FPS: warm source_frame_span=%llu presented=%llu skipped=%llu copy_misses=%llu keep=%.2f%% %s; Present submissions, not measured display refresh or phone latency\n",test.fps,(unsigned long long)produced,(unsigned long long)shown,(unsigned long long)skipped,(unsigned long long)(end.copy_misses-warm.copy_misses),produced?100.0*shown/produced:0,ok?"PASS":"FAIL");fflush(stdout);if(!ok)failures++;
        SetEvent(test.cancel);WaitForSingleObject(producer,2000);CloseHandle(producer);
        /* Simulated incomplete shared-frame write during a requested redraw.
           A healthy prior preview must survive, while Stop must invalidate it. */
        int index=test.hub.view->published;InterlockedIncrement(&test.hub.view->slots[index].sequence);preview_worker_redraw(worker);double deadline=awm_clock_ms()+150;
        while(awm_clock_ms()<deadline){MSG msg;while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}MsgWaitForMultipleObjects(0,NULL,FALSE,5,QS_ALLINPUT);}
        PreviewStats contended;preview_worker_stats(worker,&contended);bool retained=preview_worker_live(worker)&&contended.copy_misses>end.copy_misses;printf("SIMULATED contention: retained healthy preview=%d\n",retained);if(!retained)failures++;InterlockedIncrement(&test.hub.view->slots[index].sequence);
        awm_hub_stop(&test.hub);deadline=awm_clock_ms()+300;while(preview_worker_live(worker)&&awm_clock_ms()<deadline){MSG msg;while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}MsgWaitForMultipleObjects(0,NULL,FALSE,5,QS_ALLINPUT);}bool stopped=!preview_worker_live(worker);printf("SIMULATED Stop: preview invalidated=%d\n",stopped);if(!stopped)failures++;preview_worker_destroy(worker);awm_hub_close(&test.hub);
    }
    DestroyWindow(window);CloseHandle(test.cancel);free(test.pixels);return report_only?0:failures?1:0;
}
