#ifndef AWM_FRAME_HUB_H
#define AWM_FRAME_HUB_H
#include <windows.h>
#include <stdbool.h>
#include "protocol.h"
/* Advisory coalesced wakeup for the single integrated preview consumer. Camera
   consumers still pull independently. No change to the shared frame/pipe ABI. */
typedef struct AwmHub { HANDLE mapping; AwmFrameHub *view; HANDLE ready; } AwmHub;
typedef struct AwmFrameInfo {uint32_t width,height,epoch;uint64_t number,heartbeat_ms;int64_t pts_us;} AwmFrameInfo;
bool awm_hub_open(AwmHub *hub,bool create);
bool awm_hub_open_named(AwmHub *hub,bool create,const wchar_t *name);
bool awm_hub_live(const AwmHub *hub,uint64_t now);
void awm_hub_reconfigure(AwmHub *hub);
void awm_hub_close(AwmHub *hub);
bool awm_hub_publish(AwmHub *hub,const uint8_t *pixels,uint32_t width,uint32_t height,int64_t pts_us);
bool awm_hub_copy(AwmHub *hub,uint8_t *pixels,size_t capacity,uint32_t *width,uint32_t *height,int64_t *pts_us);
bool awm_hub_copy_frame(AwmHub *hub,uint8_t *pixels,size_t capacity,AwmFrameInfo *info);
void awm_hub_stop(AwmHub *hub);
bool awm_frame_shape(uint32_t width,uint32_t height);
bool awm_packet_valid(const AwmPacketHeader *header);
bool awm_pipe_io(HANDLE pipe,void *data,DWORD bytes,bool write,HANDLE cancel,DWORD timeout_ms);
bool awm_user_security(SECURITY_ATTRIBUTES *attributes,PSECURITY_DESCRIPTOR *descriptor,bool frameserver);
void awm_scale_bgra(const uint8_t *src,uint32_t sw,uint32_t sh,uint8_t *dst,uint32_t dw,uint32_t dh);
void awm_bgra_to_nv12(const uint8_t *src,uint32_t width,uint32_t height,uint8_t *dst);
#endif
