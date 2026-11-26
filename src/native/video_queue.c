#include "video_queue.h"
#include "timing.h"
#include "thread_class.h"
#include <stdlib.h>
#include <string.h>
typedef struct QueuedFrame {BYTE *pixels;uint32_t width,height,epoch;int64_t pts;double due_ms;AwmVideoTiming timing;} QueuedFrame;
struct AwmVideoQueue {
    AwmHub *hub;CRITICAL_SECTION lock,publish_lock;HANDLE wake,cancel,timer,thread;QueuedFrame slots[2];BYTE *scratch,*staging;
    unsigned head,count;bool smooth,clock_set,failed;double period,anchor_ms;int64_t anchor_pts,last_pts;
    FILE *csv;double flush_at;uint64_t session;AwmVideoQueueStats stats;
};
/* Returns 1 if published, 0 if skipped due to cancellation/stale epoch, -1 on hub failure. */
static int publish(AwmVideoQueue *q,const BYTE *pixels,const QueuedFrame *frame){
    if(WaitForSingleObject(q->cancel,0)==WAIT_OBJECT_0||frame->epoch!=q->hub->view->video_epoch)return 0;
    if(!awm_hub_publish(q->hub,pixels,frame->width,frame->height,frame->pts))return q->hub->view->stop?0:-1;
    double now=awm_clock_ms();
    EnterCriticalSection(&q->lock);
    q->stats.published++;if(now-frame->due_ms>q->period)q->stats.late++;
    uint64_t dropped=q->stats.dropped,late=q->stats.late;
    LeaveCriticalSection(&q->lock);
    if(q->csv){const AwmVideoTiming *t=&frame->timing;fprintf(q->csv,"%016llx,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%llu\n",(unsigned long long)q->session,(unsigned long long)q->hub->view->video_frames,t->packet_ms,t->decode_start_ms,t->decode_end_ms,t->convert_end_ms,now,t->source_gap_ms,frame->due_ms,(unsigned long long)dropped,(unsigned long long)late);if(now>=q->flush_at){fflush(q->csv);q->flush_at=now+5000;}}
    return 1;
}
static DWORD WINAPI worker(void *data){
    AwmVideoQueue *q=data;HANDLE multimedia=awm_multimedia_begin(L"Capture");EnterCriticalSection(&q->lock);q->stats.mmcss_active=multimedia!=NULL;LeaveCriticalSection(&q->lock);HANDLE waits[]={q->cancel,q->wake,q->timer};
    while(WaitForSingleObject(q->cancel,0)!=WAIT_OBJECT_0){
        EnterCriticalSection(&q->lock);
        if(!q->count){LeaveCriticalSection(&q->lock);WaitForMultipleObjects(2,waits,FALSE,100);continue;}
        QueuedFrame frame=q->slots[q->head];double delay=frame.due_ms-awm_clock_ms();
        if(delay>0){LARGE_INTEGER due;due.QuadPart=-(LONGLONG)(delay*10000.0)-1;bool ok=SetWaitableTimerEx(q->timer,&due,0,NULL,NULL,NULL,0)!=0;if(!ok)q->failed=true;LeaveCriticalSection(&q->lock);if(!ok)break;WaitForMultipleObjects(3,waits,FALSE,100);continue;}
        /* Swap buffer pointers in O(1) instead of copying 8-33 MB under lock. */
        BYTE *pub_pixels=frame.pixels;q->slots[q->head].pixels=q->scratch;q->scratch=pub_pixels;
        q->head=(q->head+1)%2;q->count--;
        LeaveCriticalSection(&q->lock);
        /* Publication and reconfiguration share publish_lock, preventing an old
           orientation/epoch from reappearing without blocking push() copies. */
        EnterCriticalSection(&q->publish_lock);
        int res=publish(q,pub_pixels,&frame);
        LeaveCriticalSection(&q->publish_lock);
        if(res<0){EnterCriticalSection(&q->lock);q->failed=true;LeaveCriticalSection(&q->lock);}
    }
    awm_multimedia_end(multimedia);return 0;
}
AwmVideoQueue *awm_video_queue_create(AwmHub *hub,unsigned fps,bool smooth,uint64_t session){
    if(!hub||!hub->view||fps<1||fps>240)return NULL;AwmVideoQueue *q=calloc(1,sizeof(*q));if(!q)return NULL;
    q->hub=hub;q->smooth=smooth;q->period=1000.0/fps;q->stats.delay_ms=smooth?q->period:0;q->session=session;InitializeCriticalSection(&q->lock);InitializeCriticalSection(&q->publish_lock);
    q->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);q->wake=CreateEventW(NULL,FALSE,FALSE,NULL);
    q->csv=awm_csv_open(L"AWM_CAPTURE_TRACE_CSV","session,frame,packet_ms,decode_start_ms,decode_end_ms,convert_end_ms,publish_ms,source_gap_ms,scheduled_ms,queue_dropped,queue_late");q->flush_at=awm_clock_ms()+5000;
    if(!q->cancel||!q->wake)goto fail;
    if(smooth){q->timer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);q->scratch=malloc(AWM_FRAME_BYTES);q->staging=malloc(AWM_FRAME_BYTES);for(unsigned i=0;i<2;i++)q->slots[i].pixels=malloc(AWM_FRAME_BYTES);if(!q->timer||!q->scratch||!q->staging||!q->slots[0].pixels||!q->slots[1].pixels)goto fail;q->thread=CreateThread(NULL,0,worker,q,0,NULL);if(!q->thread)goto fail;}
    return q;
fail:awm_video_queue_destroy(q);return NULL;
}
static bool push_internal(AwmVideoQueue *q,const BYTE *src_pixels,BYTE **swap_inout,uint32_t w,uint32_t h,int64_t pts,const AwmVideoTiming *timing){
    if(!q||(!src_pixels&&(!swap_inout||!*swap_inout))||!timing||pts<0||!awm_frame_shape(w,h))return false;
    const BYTE *imm_pixels=swap_inout?*swap_inout:src_pixels;
    if(!q->smooth){
        EnterCriticalSection(&q->publish_lock);
        EnterCriticalSection(&q->lock);
        bool dead=q->failed||q->hub->view->stop||WaitForSingleObject(q->cancel,0)==WAIT_OBJECT_0;
        uint32_t epoch=q->hub->view->video_epoch;
        LeaveCriticalSection(&q->lock);
        if(dead){LeaveCriticalSection(&q->publish_lock);return false;}
        double now=awm_clock_ms();QueuedFrame frame={NULL,w,h,epoch,pts,now,*timing};
        bool ok=publish(q,imm_pixels,&frame)>0;
        LeaveCriticalSection(&q->publish_lock);return ok;
    }
    /* Copy outside q->lock when caller retains ownership so worker timer wakes
       never stall behind an 8-33 MB memcpy. */
    if(!swap_inout)memcpy(q->staging,src_pixels,(size_t)w*h*4);
    EnterCriticalSection(&q->lock);
    if(q->failed||q->hub->view->stop||WaitForSingleObject(q->cancel,0)==WAIT_OBJECT_0){LeaveCriticalSection(&q->lock);return false;}
    double now=awm_clock_ms();QueuedFrame frame={NULL,w,h,q->hub->view->video_epoch,pts,now,*timing};
    /* Unchanged screen content is variable rate. Re-anchor after a pause or
       discontinuous PTS instead of attempting to catch up old deadlines. */
    double base_ms=isfinite(timing->packet_ms)?timing->packet_ms:now;
    if(!q->clock_set||pts<=q->last_pts||pts-q->last_pts>250000){q->anchor_pts=pts;q->anchor_ms=base_ms;q->clock_set=true;}
    /* Gently track both startup offset and bidirectional clock drift without
       collapsing the buffer to early jitter outliers. Relative clocks only. */
    if(isfinite(timing->packet_ms)){
        double candidate=timing->packet_ms-(pts-q->anchor_pts)/1000.0;
        double delta=(candidate-q->anchor_ms)*0.05;
        if(delta>0.25)delta=0.25;else if(delta<-0.25)delta=-0.25;
        q->anchor_ms+=delta;
    }
    q->last_pts=pts;frame.due_ms=q->anchor_ms+(pts-q->anchor_pts)/1000.0+q->period;
    /* GPU startup or a transport stall must not leave a lasting clock offset
       or a backlog. Publish an already-late frame immediately instead of adding
       another full period of artificial delay. */
    if(frame.due_ms<now-q->period){q->stats.dropped+=q->count;q->count=q->head=0;q->anchor_pts=pts;q->anchor_ms=base_ms;frame.due_ms=now;}
    else if(frame.due_ms>now+q->period*3){q->stats.dropped+=q->count;q->count=q->head=0;q->anchor_pts=pts;q->anchor_ms=base_ms;frame.due_ms=base_ms+q->period;if(frame.due_ms<now)frame.due_ms=now;}
    if(q->count==2){q->head=(q->head+1)%2;q->count--;q->stats.dropped++;}
    unsigned tail=(q->head+q->count)%2;
    BYTE *old_slot=q->slots[tail].pixels;
    if(swap_inout){frame.pixels=*swap_inout;*swap_inout=old_slot;}
    else{frame.pixels=q->staging;q->staging=old_slot;}
    q->slots[tail]=frame;q->count++;
    bool wake_worker=q->count==1||frame.due_ms<=now;
    LeaveCriticalSection(&q->lock);
    if(wake_worker)SetEvent(q->wake);
    return true;
}
bool awm_video_queue_push(AwmVideoQueue *q,const BYTE *pixels,uint32_t w,uint32_t h,int64_t pts,const AwmVideoTiming *timing){return push_internal(q,pixels,NULL,w,h,pts,timing);}
bool awm_video_queue_push_swap(AwmVideoQueue *q,BYTE **pixels_inout,uint32_t w,uint32_t h,int64_t pts,const AwmVideoTiming *timing){return push_internal(q,NULL,pixels_inout,w,h,pts,timing);}
void awm_video_queue_reconfigure(AwmVideoQueue *q){if(q){EnterCriticalSection(&q->publish_lock);EnterCriticalSection(&q->lock);q->count=q->head=0;q->clock_set=false;awm_hub_reconfigure(q->hub);LeaveCriticalSection(&q->lock);LeaveCriticalSection(&q->publish_lock);SetEvent(q->wake);}}
void awm_video_queue_stats(AwmVideoQueue *q,AwmVideoQueueStats *stats){memset(stats,0,sizeof(*stats));if(q){EnterCriticalSection(&q->lock);*stats=q->stats;LeaveCriticalSection(&q->lock);}}
void awm_video_queue_destroy(AwmVideoQueue *q){if(!q)return;if(q->cancel)SetEvent(q->cancel);if(q->wake)SetEvent(q->wake);if(q->thread){WaitForSingleObject(q->thread,INFINITE);CloseHandle(q->thread);}if(q->csv)fclose(q->csv);if(q->timer)CloseHandle(q->timer);if(q->wake)CloseHandle(q->wake);if(q->cancel)CloseHandle(q->cancel);for(unsigned i=0;i<2;i++)free(q->slots[i].pixels);free(q->staging);free(q->scratch);DeleteCriticalSection(&q->publish_lock);DeleteCriticalSection(&q->lock);free(q);}
