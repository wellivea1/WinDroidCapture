#ifndef AWM_VIDEO_QUEUE_H
#define AWM_VIDEO_QUEUE_H
#include "frame_hub.h"
typedef struct AwmVideoQueue AwmVideoQueue;
typedef struct AwmVideoTiming {double packet_ms,decode_start_ms,decode_end_ms,convert_end_ms,source_gap_ms;} AwmVideoTiming;
typedef struct AwmVideoQueueStats {uint64_t published,dropped,late;double delay_ms;bool mmcss_active;} AwmVideoQueueStats;
/* Immediate mode has no queue/thread. Smooth mode maps relative source PTS to
   QPC, adding one requested frame interval with at most two pending images. */
AwmVideoQueue *awm_video_queue_create(AwmHub *hub,unsigned fps,bool smooth,uint64_t session);
bool awm_video_queue_push(AwmVideoQueue *q,const BYTE *pixels,uint32_t width,uint32_t height,int64_t pts,const AwmVideoTiming *timing);
void awm_video_queue_reconfigure(AwmVideoQueue *q);
void awm_video_queue_stats(AwmVideoQueue *q,AwmVideoQueueStats *stats);
void awm_video_queue_destroy(AwmVideoQueue *q);
#endif
