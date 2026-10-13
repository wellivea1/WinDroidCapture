#ifndef AWM_NATIVE_SETTINGS_H
#define AWM_NATIVE_SETTINGS_H
#include "../platform.h"
#include "capture_profile.h"
typedef struct {char serial[256];NativeCaptureProfile camera,screen;CaptureKind last_kind;bool camera_saved,screen_saved;} SavedCapture;
typedef struct NativeSettings {
    uint32_t magic,version;wchar_t adb[AWM_PATH],scrcpy[AWM_PATH];
    SavedProfile profiles[AWM_PROFILES];int profile_count;unsigned preview_scaler,pipeline_scaler;
    unsigned output_width,output_height,output_fps;
    SavedCapture captures[AWM_PROFILES];int capture_count;
    unsigned ui_theme; /* 0 = System, 1 = Light, 2 = Dark; v4 prefix is preserved. */
    unsigned video_buffer; /* 0 = minimum latency, 1 = smooth priority; v6. */
    unsigned reserved_v6[2];
} NativeSettings;
#define AWM_SETTINGS_V5_BYTES ((offsetof(NativeSettings,video_buffer)+_Alignof(NativeSettings)-1)&~(_Alignof(NativeSettings)-1))
bool native_settings_path(wchar_t *path);
bool native_settings_load(NativeSettings *s,const wchar_t *path);
bool native_settings_save(const NativeSettings *s,const wchar_t *path);
void native_settings_defaults(NativeSettings *s);
bool native_profile_get(const NativeSettings *s,const char *serial,Profile *p);
void native_profile_set(NativeSettings *s,const char *serial,const Profile *p);
bool native_capture_get(const NativeSettings *s,const char *serial,CaptureKind kind,NativeCaptureProfile *p);
void native_capture_set(NativeSettings *s,const char *serial,const NativeCaptureProfile *p);
CaptureKind native_last_kind(const NativeSettings *s,const char *serial);
#endif
