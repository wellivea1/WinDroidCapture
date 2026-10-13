#ifndef AWM_ENGINE_H
#define AWM_ENGINE_H
#include "obs.h"
#include "audio.h"
#define AWM_UPDATE (WM_APP+1)
typedef enum { CMD_CONFIG, CMD_REFRESH, CMD_PROBE, CMD_PREVIEW, CMD_MIC_SETUP, CMD_OUTPUT_PLAN, CMD_OUTPUT_COMMIT,
    CMD_STOP, CMD_PAIR, CMD_CONNECT, CMD_DISCONNECT, CMD_RECONNECT, CMD_MDNS, CMD_ADB_REPAIR, CMD_METER, CMD_MIC_CONFIRM, CMD_SAVE_PROFILE, CMD_OBS_CHECK } CommandType;
typedef struct {
    CommandType type; Device device; Profile profile; wchar_t address[320], pairing_code[16];
    Configuration *configuration;
    uint32_t generation,cancellation_epoch; bool flag;
} Command;
typedef struct {
    Configuration configuration; ToolFeatures features; DeviceList devices; Capabilities capabilities; AudioEndpoints endpoints;
    SessionState session;
    char adb_version[256],scrcpy_version[256],obs_version[128],webcams[2048];
    char adb_status[1024],camera_status[1024],mic_status[1024],obs_status[1024],route_status[1024],notice[2048];
    char diagnostics[AWM_TEXT],capability_serial[256],active_serial[256],active_transport[32];
    wchar_t effective_command[32768];
    bool output_plan, microphone, routed, mic_confirmed, busy;
    uint32_t plan_generation;
    char plan[4096];
    int observed_width,observed_height,last_fps,obs_width,obs_height,obs_fps_num,obs_fps_den;
    float peak;
} EngineView;
typedef struct Engine Engine;
Engine *engine_create(HWND notify_window);
/* Explicit isolated settings path for the simulated integration harness. */
Engine *engine_create_at_settings(HWND notify_window,const wchar_t *settings_path);
void engine_destroy(Engine *e);
bool engine_send(Engine *e,const Command *c);
void engine_snapshot(Engine *e,EngineView *out);
#endif
