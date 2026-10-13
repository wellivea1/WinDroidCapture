#include "preview_worker.h"
#include "frame_hub.h"
#include "../core.h"
#include "timing.h"
#include "thread_class.h"
#include <stdlib.h>
#include <string.h>
struct PreviewWorker {
    HWND canvas,notify;HANDLE thread,wake,quit;CRITICAL_SECTION lock;
    wchar_t mapping[200];bool enabled;unsigned scaler,framing;LONG revision;volatile LONG live,failed;
    PreviewStats stats;
};
static void state(PreviewWorker *w,bool live,bool failed){
    bool changed=InterlockedExchange(&w->live,live)!=(LONG)live;changed|=InterlockedExchange(&w->failed,failed)!=(LONG)failed;
    if(changed&&w->notify)PostMessageW(w->notify,PREVIEW_UPDATE,0,0);
}
static DWORD WINAPI render(void *data){
    PreviewWorker *w=data;AwmHub hub={0};NativePreview *gpu=NULL;BYTE *pixels=malloc(AWM_FRAME_BYTES);wchar_t current[200]=L"";
    HANDLE multimedia=awm_multimedia_begin(L"Playback");
    uint64_t frame=0;uint32_t sw=0,sh=0;LONG revision=-1;RECT old={0};
    FILE *trace=awm_timing_open(L"AWM_PREVIEW_TIMING_LOG");AwmTiming copies={0},renders={0},gaps={0};double last_present=0,report_at=awm_clock_ms()+5000;
    FILE *csv=awm_csv_open(L"AWM_PREVIEW_TRACE_CSV","session,frame,copy_start_ms,copy_end_ms,present_start_ms,present_end_ms,ok");
    while(WaitForSingleObject(w->quit,0)!=WAIT_OBJECT_0){
        wchar_t mapping[200];bool enabled;unsigned scaler,framing;LONG update;
        EnterCriticalSection(&w->lock);awm_wcopy(mapping,200,w->mapping);enabled=w->enabled;scaler=w->scaler;framing=w->framing;update=w->revision;LeaveCriticalSection(&w->lock);
        if(wcscmp(current,mapping)){awm_hub_close(&hub);awm_wcopy(current,200,mapping);frame=0;revision=-1;last_present=0;EnterCriticalSection(&w->lock);memset(&w->stats,0,sizeof(w->stats));LeaveCriticalSection(&w->lock);}
        if(enabled&&mapping[0]&&!hub.view)awm_hub_open_named(&hub,false,mapping);
        bool live=enabled&&pixels&&awm_hub_live(&hub,GetTickCount64()),retry_copy=false;
        if(live){RECT r;GetClientRect(w->canvas,&r);bool dirty=update!=revision||r.right!=old.right||r.bottom!=old.bottom;
            uint64_t next=hub.view->video_frames;if(next!=frame||dirty){
                if(!gpu)gpu=native_preview_create(w->canvas);
                /* Wait for queue capacity before copying: retain the latest
                   source update rather than aging a frame in the GPU queue. */
                if(gpu&&!native_preview_wait(gpu,w->quit,100)){DWORD error=GetLastError();if(error!=ERROR_TIMEOUT&&error!=ERROR_CANCELLED)state(w,false,true);continue;}
                double copy_at=awm_clock_ms();
                AwmFrameInfo info;if(awm_hub_copy_frame(&hub,pixels,AWM_FRAME_BYTES,&info)){
                    sw=info.width;sh=info.height;next=info.number;
                    double copy_end=awm_clock_ms();awm_timing_add(&copies,copy_end-copy_at);double begin=awm_clock_ms();
                    native_preview_set_scaler(gpu,scaler);native_preview_set_framing(gpu,framing);
                    bool ok=native_preview_frame(gpu,pixels,sw,sh);double end=awm_clock_ms();awm_timing_add(&renders,end-begin);double gap=last_present?end-last_present:0;if(last_present)awm_timing_add(&gaps,gap);last_present=end;
                    if(csv)fprintf(csv,"%016llx,%llu,%.6f,%.6f,%.6f,%.6f,%d\n",(unsigned long long)awm_trace_session(current),(unsigned long long)next,copy_at,copy_end,begin,end,ok);
                    EnterCriticalSection(&w->lock);if(ok){w->stats.presented++;w->stats.last_source_frame=next;if(frame&&next>frame+1)w->stats.skipped+=next-frame-1;}w->stats.render_ms=end-begin;w->stats.last_gap_ms=gap;LeaveCriticalSection(&w->lock);
                    frame=next;revision=update;old=r;state(w,ok,!ok);
                }else{EnterCriticalSection(&w->lock);w->stats.copy_misses++;LeaveCriticalSection(&w->lock);retry_copy=true;/* Contention is not a disconnected source; keep the valid preview. */}
            }
        }else{
            /* Publish invalidation before driver/resource teardown, which
               can take longer than a health update on a busy GPU. */
            state(w,false,!pixels);if(gpu){native_preview_destroy(gpu);gpu=NULL;}frame=0;revision=-1;
        }
        if(awm_clock_ms()>=report_at){if(trace){PreviewStats stats;preview_worker_stats(w,&stats);fprintf(trace,"PREVIEW totals presented=%llu skipped=%llu copy_misses=%llu; present submission timing, not display or phone latency\n",(unsigned long long)stats.presented,(unsigned long long)stats.skipped,(unsigned long long)stats.copy_misses);awm_timing_print(trace,"copy",&copies);awm_timing_print(trace,"render_present",&renders);awm_timing_print(trace,"present_gap",&gaps);fflush(trace);}if(csv)fflush(csv);report_at=awm_clock_ms()+5000;}
        HANDLE waits[]={w->quit,w->wake,hub.ready};DWORD count=enabled&&hub.ready?3:2;
        /* The timeout checks health/static content, it does not pace video.
           Legacy hubs without an event use a bounded compatibility poll. */
        WaitForMultipleObjects(count,waits,FALSE,retry_copy?1:live&&!hub.ready?4:100);
    }
    if(csv)fclose(csv);if(trace){awm_timing_print(trace,"final_copy",&copies);awm_timing_print(trace,"final_render_present",&renders);awm_timing_print(trace,"final_present_gap",&gaps);fclose(trace);}native_preview_destroy(gpu);awm_hub_close(&hub);free(pixels);state(w,false,false);awm_multimedia_end(multimedia);return 0;
}
PreviewWorker *preview_worker_create(HWND canvas,HWND notify){
    PreviewWorker *w=calloc(1,sizeof(*w));if(!w)return NULL;w->canvas=canvas;w->notify=notify;InitializeCriticalSection(&w->lock);
    w->wake=CreateEventW(NULL,FALSE,FALSE,NULL);w->quit=CreateEventW(NULL,TRUE,FALSE,NULL);if(w->wake&&w->quit)w->thread=CreateThread(NULL,0,render,w,0,NULL);
    if(!w->wake||!w->quit||!w->thread){if(w->thread){SetEvent(w->quit);WaitForSingleObject(w->thread,INFINITE);CloseHandle(w->thread);}if(w->wake)CloseHandle(w->wake);if(w->quit)CloseHandle(w->quit);DeleteCriticalSection(&w->lock);free(w);return NULL;}return w;
}
void preview_worker_update(PreviewWorker *w,const wchar_t *mapping,bool enabled,unsigned scaler,unsigned framing){if(!w)return;
    EnterCriticalSection(&w->lock);if(wcscmp(w->mapping,mapping)||w->enabled!=enabled||w->scaler!=scaler||w->framing!=framing){awm_wcopy(w->mapping,200,mapping);w->enabled=enabled;w->scaler=scaler;w->framing=framing;w->revision++;SetEvent(w->wake);}LeaveCriticalSection(&w->lock);
}
void preview_worker_redraw(PreviewWorker *w){if(w){EnterCriticalSection(&w->lock);w->revision++;SetEvent(w->wake);LeaveCriticalSection(&w->lock);}}
bool preview_worker_live(PreviewWorker *w){return w&&InterlockedCompareExchange(&w->live,0,0)!=0;}
bool preview_worker_failed(PreviewWorker *w){return w&&InterlockedCompareExchange(&w->failed,0,0)!=0;}
void preview_worker_stats(PreviewWorker *w,PreviewStats *stats){memset(stats,0,sizeof(*stats));if(w){EnterCriticalSection(&w->lock);*stats=w->stats;LeaveCriticalSection(&w->lock);}}
void preview_worker_close(PreviewWorker *w){if(w){state(w,false,false);SetEvent(w->quit);SetEvent(w->wake);}}
bool preview_worker_closed(PreviewWorker *w){return !w||WaitForSingleObject(w->thread,0)==WAIT_OBJECT_0;}
void preview_worker_destroy(PreviewWorker *w){if(!w)return;preview_worker_close(w);WaitForSingleObject(w->thread,INFINITE);CloseHandle(w->thread);CloseHandle(w->wake);CloseHandle(w->quit);DeleteCriticalSection(&w->lock);free(w);}
