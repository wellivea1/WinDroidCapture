#ifndef AWM_NATIVE_MANAGER_H
#define AWM_NATIVE_MANAGER_H
#include "settings.h"
#include "frame_hub.h"
#include "../audio.h"
#define NATIVE_UPDATE (WM_APP+12)
typedef enum { N_IDLE,N_STARTING,N_RUNNING,N_STOPPING,N_RECONNECTING,N_ERROR } NativePhase;
typedef enum { N_REFRESH,N_PROBE,N_START,N_STOP,N_SETTINGS,N_PAIR,N_CONNECT,N_DISCONNECT,N_MDNS,N_REGISTER,N_PROFILE,N_SCALERS } NativeAction;
typedef struct NativeCommand {NativeAction action;Device device;Profile profile;NativeCaptureProfile capture;bool use_capture;NativeSettings settings;wchar_t address[320],code[16];LONG epoch;} NativeCommand;
typedef struct NativeView {
    NativeSettings settings;DeviceList devices;Capabilities capabilities;ToolFeatures features;
    DisplayList displays;CaptureKind capabilities_kind,active_kind;NativePhase phase;
    char capabilities_transport[32],capabilities_version[256],capture_version[256];
    wchar_t mapping_name[200],capture_client[AWM_PATH];
    char capabilities_serial[256],active_serial[256],active_transport[32],adb_version[256],scrcpy_version[256];
    char notice[2048],diagnostics[AWM_TEXT],camera_status[512],mic_status[512],output_status[512];
    wchar_t camera_name[256],microphone_name[256],microphone_id[1024],command[32768];
    bool busy,active,camera_registered,microphone_installed,receiver_checked;float peak;uint32_t width,height;uint64_t frames,audio_samples;
    bool outputs_checked,source_live,microphone_forwarding;
    bool profile_saved,settings_writable;uint64_t content_age_ms,camera_requests,repeated_samples;uint32_t video_epoch,request_width,request_height,request_format,probe_revision;
} NativeView;
typedef struct NativeManager NativeManager;
NativeManager *native_manager_create(HWND window);
/* Explicit injection for isolated fake-process tests; never used by the UI. */
NativeManager *native_manager_create_at(HWND window,const wchar_t *settings,const wchar_t *components);
void native_manager_destroy(NativeManager *m);
void native_manager_begin_close(NativeManager *m);
bool native_manager_closed(NativeManager *m);
bool native_manager_send(NativeManager *m,const NativeCommand *c);
void native_manager_snapshot(NativeManager *m,NativeView *view);
#endif
