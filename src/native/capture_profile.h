#ifndef AWM_CAPTURE_PROFILE_H
#define AWM_CAPTURE_PROFILE_H
#include "../core.h"
typedef enum { CAPTURE_CAMERA=0, CAPTURE_SCREEN=1 } CaptureKind;
typedef enum { FRAME_FIT=0, FRAME_FILL=1 } CaptureFraming;
typedef struct {
    char id[80], facing[32], aspect[32];
    int width,height,fps,max_size; float zoom;
} CameraSettings;
typedef struct { int display_id,max_size,fps_limit; bool lock_orientation; } ScreenSettings;
typedef struct { char codec[32],encoder[256]; int bitrate_mbps; } CaptureEncoding;
typedef struct {
    CaptureKind kind; CameraSettings camera; ScreenSettings screen; CaptureEncoding encoding;
    char audio_source[64]; int rotation; CaptureFraming framing;
    bool mirror,audio,reconnect;
} NativeCaptureProfile;
#define AWM_DISPLAYS 32
typedef struct { int id,width,height; bool size_known; } PhoneDisplay;
typedef struct { PhoneDisplay items[AWM_DISPLAYS]; int count; bool truncated,recognized; } DisplayList;
void native_capture_default(NativeCaptureProfile *p,CaptureKind kind,int preset);
void native_capture_from_legacy(NativeCaptureProfile *p,const Profile *old);
void native_capture_to_legacy(const NativeCaptureProfile *p,Profile *old);
bool native_capture_validate(const NativeCaptureProfile *p,const Device *d,const ToolFeatures *f,char *err,size_t cap);
bool native_capture_declared(const NativeCaptureProfile *p,const Capabilities *c,const DisplayList *ds,char *err,size_t cap);
bool native_parse_displays(const char *text,DisplayList *out);
const char *native_capture_name(CaptureKind kind);
#endif
