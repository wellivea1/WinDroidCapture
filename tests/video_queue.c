/* Synthetic timing/epoch/cancellation test. No physical phone or receiver. */
#include "../src/native/video_queue.h"
#include "../src/native/timing.h"
#include "../camera/pacer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Observer {AwmHub *hub;HANDLE cancel,thread;double gaps[256],last;unsigned count;uint64_t frames;} Observer;
static DWORD WINAPI observe(void *data){Observer *o=data;BYTE pixels[16*16*4];HANDLE waits[]={o->cancel,o->hub->ready};while(WaitForMultipleObjects(2,waits,FALSE,1000)!=WAIT_OBJECT_0){AwmFrameInfo frame;if(awm_hub_copy_frame(o->hub,pixels,sizeof(pixels),&frame)&&frame.number!=o->frames){double now=awm_clock_ms();if(o->last&&o->count<256)o->gaps[o->count++]=now-o->last;o->last=now;o->frames=frame.number;}}return 0;}
static double deviation(const double *p,unsigned n){double mean=0,total=0;for(unsigned i=0;i<n;i++)mean+=p[i];mean/=n;for(unsigned i=0;i<n;i++)total+=(p[i]-mean)*(p[i]-mean);return sqrt(total/n);}
static int paced(unsigned fps){
    AwmHub hub={0};if(!awm_hub_open(&hub,true))return 1;hub.view->source_kind=fps==60?1:0;AwmVideoQueue *q=awm_video_queue_create(&hub,fps,true,1);if(!q)return 1;
    Observer o={0};o.hub=&hub;o.cancel=CreateEventW(NULL,TRUE,FALSE,NULL);o.thread=CreateThread(NULL,0,observe,&o,0,NULL);HANDLE timer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);BYTE pixels[16*16*4]={0};
    LONGLONG start=awm_pacer_now();double input[100],last=0;unsigned n=90;AwmVideoTiming timing={0};
    for(unsigned i=0;i<n;i++){double jitter=i==0?0:(i%3==1?8:i%3==2?-8:0);if(fps==60)jitter*=.5;LONGLONG due=start+(LONGLONG)(i*10000000.0/fps+jitter*10000);if(awm_pacer_wait(timer,o.cancel,due)!=1)return 1;double now=awm_clock_ms();if(i)input[i-1]=now-last;last=now;pixels[0]=(BYTE)i;timing.packet_ms=timing.decode_start_ms=timing.decode_end_ms=timing.convert_end_ms=now;if(!awm_video_queue_push(q,pixels,16,16,(int64_t)(i*1000000.0/fps),&timing))return 1;}
    Sleep(120);SetEvent(o.cancel);WaitForSingleObject(o.thread,1000);AwmVideoQueueStats stats;awm_video_queue_stats(q,&stats);double before=deviation(input,n-1),after=deviation(o.gaps,o.count);bool pass=o.frames>=n-1&&!stats.dropped&&o.count&&after<before*.7;
    printf("SYNTHETIC SHARED VIDEO QUEUE %u FPS: input interval SD %.3f ms -> published SD %.3f ms, published=%llu dropped=%llu late=%llu intentional_delay=%.3f ms %s\n",fps,before,after,(unsigned long long)stats.published,(unsigned long long)stats.dropped,(unsigned long long)stats.late,stats.delay_ms,pass?"PASS":"FAIL");
    CloseHandle(timer);CloseHandle(o.thread);CloseHandle(o.cancel);awm_hub_stop(&hub);awm_video_queue_destroy(q);awm_hub_close(&hub);return pass?0:1;
}
int main(void){
    if(paced(30)||paced(60))return 1;
    AwmHub hub={0};if(!awm_hub_open(&hub,true))return 1;AwmVideoQueue *q=awm_video_queue_create(&hub,30,true,1);BYTE pixels[16*16*4]={0};AwmVideoTiming timing={0};
    if(!q||!awm_video_queue_push(q,pixels,16,16,0,&timing))return 1;awm_video_queue_reconfigure(q);Sleep(50);if(hub.view->live||hub.view->video_frames)return 1;
    /* Reconfigure must not poison the queue; subsequent frames must still publish. */
    double now=awm_clock_ms();timing.packet_ms=now;if(!awm_video_queue_push(q,pixels,16,16,33333,&timing))return 1;Sleep(80);if(!hub.view->live||hub.view->video_frames!=1)return 1;
    for(unsigned i=0;i<10;i++)if(!awm_video_queue_push(q,pixels,16,16,(i+2)*33333,&timing))return 1;AwmVideoQueueStats stats;awm_video_queue_stats(q,&stats);if(!stats.dropped)return 1;
    awm_hub_stop(&hub);uint64_t frames=hub.view->video_frames;awm_video_queue_destroy(q);Sleep(50);if(hub.view->live||hub.view->video_frames!=frames)return 1;
    /* Zero-copy push_swap swaps caller's buffer in O(1) and publishes exact content;
       late-frame resync publishes immediately without adding an extra full period. */
    awm_hub_close(&hub);if(!awm_hub_open(&hub,true))return 1;q=awm_video_queue_create(&hub,30,true,3);BYTE *swap_buf=malloc(AWM_FRAME_BYTES),*orig_buf=swap_buf;if(!q||!swap_buf)return 1;
    memset(swap_buf,0x5A,16*16*4);now=awm_clock_ms();timing.packet_ms=now;
    if(!awm_video_queue_push_swap(q,&swap_buf,16,16,0,&timing)||swap_buf==orig_buf||!swap_buf)return 1;
    Sleep(90);BYTE readback[16*16*4]={0};AwmFrameInfo info={0};if(!awm_hub_copy_frame(&hub,readback,sizeof(readback),&info)||info.number!=1||readback[0]!=0x5A)return 1;
    memset(swap_buf,0xA5,16*16*4);WaitForSingleObject(hub.ready,0);double late_at=awm_clock_ms();timing.packet_ms=late_at;
    if(!awm_video_queue_push_swap(q,&swap_buf,16,16,33333,&timing))return 1;
    bool got2=false;double pub_ms=0;
    while((pub_ms=awm_clock_ms()-late_at)<25.0){if(awm_hub_copy_frame(&hub,readback,sizeof(readback),&info)&&info.number==2){got2=true;break;}WaitForSingleObject(hub.ready,5);}
    if(!got2||readback[0]!=0xA5){printf("FAIL late resync / push_swap: got2=%d pub_ms=%.2f frame=%llu byte=0x%02x\n",got2,pub_ms,(unsigned long long)info.number,readback[0]);return 1;}
    awm_hub_stop(&hub);awm_video_queue_destroy(q);free(swap_buf);awm_hub_close(&hub);
    if(!awm_hub_open(&hub,true))return 1;q=awm_video_queue_create(&hub,30,false,2);if(!q||!awm_video_queue_push(q,pixels,16,16,1000,&timing)||hub.view->video_frames!=1)return 1;awm_video_queue_stats(q,&stats);if(stats.delay_ms||awm_video_queue_push(q,pixels,16,16,-1,&timing))return 1;awm_hub_stop(&hub);awm_video_queue_destroy(q);awm_hub_close(&hub);
    puts("PASS SYNTHETIC video queue: bounded overflow reported, epoch flush, Stop discards pending frames, zero-copy push_swap, immediate late-frame resync, immediate mode, rejected negative PTS. No hardware assertion.");return 0;
}
