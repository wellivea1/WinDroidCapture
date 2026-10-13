#ifndef AWM_CORE_H
#define AWM_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#define AWM_DEVICES 64
#define AWM_CAMERAS 32
#define AWM_SIZES 128
#define AWM_ENCODERS 96
#define AWM_ARGS 40
#define AWM_PATH 1024
#define AWM_TEXT 65536
typedef enum { DEV_UNKNOWN, DEV_AUTHORIZED, DEV_UNAUTHORIZED, DEV_OFFLINE, DEV_DISCONNECTED, DEV_UNSUPPORTED } DeviceState;
typedef enum { TRANSPORT_UNKNOWN, TRANSPORT_USB, TRANSPORT_WIRELESS, TRANSPORT_EMULATOR } Transport;
typedef struct {
    char serial[256], model[256], transport_id[32], raw_state[80], android[64];
    int sdk;
    DeviceState state;
    Transport transport;
} Device;
typedef struct { Device items[AWM_DEVICES]; int count; bool truncated; } DeviceList;
typedef struct { int width, height; } CameraSize;
typedef struct {
    char id[80], facing[32];
    int width, height, fps[32], fps_count;
    float zoom_min, zoom_max;
    float focal_mm, sensor_width_mm;
    bool physical;
    char logical_id[80], physical_id[80];
    CameraSize sizes[AWM_SIZES];
    int size_count;
} Camera;
typedef struct { char name[256], codec[32]; bool hardware; } Encoder;
typedef struct { Camera cameras[AWM_CAMERAS]; int camera_count; Encoder encoders[AWM_ENCODERS]; int encoder_count; unsigned camera_api; bool camera_truncated; } Capabilities;
typedef struct {
    bool camera, list_cameras, list_sizes, list_encoders, camera_fps, require_audio;
    bool no_downsize, display_orientation, window_size, zoom, mic_sources, hwdec;
    bool adb_pair, adb_mdns;
    bool list_displays, max_fps, capture_orientation;
} ToolFeatures;
typedef struct {
    char camera_id[80], camera_facing[32], codec[32], encoder[256], audio_source[64];
    int width, height, fps, bitrate_mbps, rotation, max_size;
    char camera_ar[32];
    bool mirror, audio, reconnect;
    float zoom;
} Profile;
typedef struct { wchar_t values[AWM_ARGS][AWM_PATH]; int count; } Arguments;
typedef enum { SESSION_IDLE, SESSION_STARTING, SESSION_PREVIEW, SESSION_OUTPUT, SESSION_FAILED, SESSION_RETRY } SessionPhase;
typedef struct { SessionPhase phase; uint32_t generation; bool desired; unsigned retries; } SessionState;
void awm_copy(char *dst, size_t size, const char *src);
void awm_wcopy(wchar_t *dst, size_t size, const wchar_t *src);
const char *device_state_name(DeviceState state);
const char *transport_name(Transport t);
bool parse_adb_devices(const char *text, DeviceList *out);
void merge_devices(DeviceList *current, const DeviceList *snapshot);
int find_device(const DeviceList *list, const char *serial, const char *transport_id);
void parse_capabilities(const char *text, Capabilities *out);
void parse_features(const char *scrcpy_help, const char *adb_help, ToolFeatures *out);
void profile_default(Profile *p, int preset);
bool validate_profile(const Profile *p, const ToolFeatures *f, char *error, size_t cap);
bool profile_declared(const Profile *p, const Capabilities *c, char *note, size_t cap);
bool build_scrcpy_args(const wchar_t *exe, const Device *d, const Profile *p, const ToolFeatures *f,
                       const wchar_t *title, bool microphone, Arguments *out, char *error, size_t cap);
bool args_add(Arguments *a, const wchar_t *v);
bool quote_windows_args(const Arguments *a, wchar_t *out, size_t cap);
bool valid_wireless_address(const wchar_t *address);
bool valid_pairing_code(const wchar_t *code);
uint32_t session_start(SessionState *s);
void session_stop(SessionState *s);
bool session_complete(SessionState *s, uint32_t generation, bool output);
bool session_failure(SessionState *s, uint32_t generation, bool reconnect);
void redact_text(char *text, const DeviceList *devices);
#endif
