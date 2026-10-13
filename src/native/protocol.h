#ifndef AWM_NATIVE_PROTOCOL_H
#define AWM_NATIVE_PROTOCOL_H
#include <stdint.h>
#define AWM_EXPORT_MAGIC 0x32505741u
#define AWM_EXPORT_VERSION 3u
#define AWM_HUB_VERSION 3u
#define AWM_CAMERA_VERSION 2u
#define AWM_PROTOCOL_VERSION AWM_EXPORT_VERSION
#define AWM_PACKET_MAX (16u*1024u*1024u)
#define AWM_STREAM_VIDEO 0u
#define AWM_STREAM_AUDIO 1u
#define AWM_MSG_OPEN 1u
#define AWM_MSG_PACKET 2u
#define AWM_MSG_CLOSE 3u
#define AWM_MSG_DISABLED 4u
#define AWM_MSG_HEARTBEAT 5u
#define AWM_MSG_SESSION 6u
#define AWM_FLAG_CONFIG 0x80000000u
/* Explicit little-endian, fixed-width local ABI; no pointers or AV structs. */
typedef struct AwmPacketHeader {
    uint32_t magic,version,type,stream,codec,width,height,sample_rate,channels,flags,size,sequence;
    int64_t pts_us;
} AwmPacketHeader;
#define AWM_CAMERA_PIPE L"\\\\.\\pipe\\AWM.Camera.v2"
#define AWM_FRAME_MAPPING L"Local\\AWM.Camera.Frames.v2"
#define AWM_OWNER_MUTEX L"Local\\AWM.Capture.Owner.v3"
#define AWM_MAX_PIXELS (3840u*2160u)
#define AWM_FRAME_BYTES (AWM_MAX_PIXELS*4u)
#define AWM_FRAME_MAGIC 0x32465741u
typedef struct AwmFrameSlot {
    volatile long sequence;
    uint32_t width,height,generation;
    uint32_t epoch;uint64_t frame_number;
    uint64_t heartbeat_ms;
    int64_t pts_us;
    uint8_t pixels[AWM_FRAME_BYTES];
} AwmFrameSlot;
typedef struct AwmFrameHub {
    uint32_t magic,version;
    volatile long published,live,microphone_live,microphone_driver;
    volatile long stop,muted;
    uint32_t generation,fps;
    uint32_t source_kind,video_epoch,framing;
    uint64_t session_heartbeat_ms,camera_requests,repeated_samples;
    uint32_t request_width,request_height,request_format;
    uint64_t video_frames,audio_frames,heartbeat_ms;
    float microphone_peak;
    uint32_t error_code;
    char status[512];
    AwmFrameSlot slots[2];
} AwmFrameHub;
typedef struct AwmCameraRequest { uint32_t magic,version,width,height,format; } AwmCameraRequest;
typedef struct AwmCameraReply { uint32_t magic,version,width,height,format,bytes,live,generation; int64_t pts_us; } AwmCameraReply;
#define AWM_CAMERA_RGB32 1u
#define AWM_CAMERA_NV12 2u
#endif
