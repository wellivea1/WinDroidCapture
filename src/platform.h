#ifndef AWM_PLATFORM_H
#define AWM_PLATFORM_H
#include <winsock2.h>
#include <windows.h>
#include "core.h"
#define AWM_PROFILES 64
typedef struct { char serial[256]; Profile profile; } SavedProfile;
typedef struct {
    uint32_t magic, version;
    wchar_t adb[AWM_PATH], scrcpy[AWM_PATH], obs[AWM_PATH];
    wchar_t playback_id[1024], recording_id[1024], obs_password[256];
    unsigned obs_port;
    SavedProfile profiles[AWM_PROFILES]; int profile_count;
} Configuration;
typedef struct { HANDLE process, pipe, job; DWORD pid, exit_code; bool exited; } Process;
bool platform_init(void);
void platform_shutdown(void);
void utf8_to_wide(const char *s,wchar_t *out,size_t cap);
void wide_to_utf8(const wchar_t *s,char *out,size_t cap);
void win_error(DWORD code,char *out,size_t cap);
bool path_is_file(const wchar_t *p);
void config_defaults(Configuration *c);
void detect_dependencies(Configuration *c);
bool config_path(wchar_t *path,size_t cap);
bool config_save(const wchar_t *path,const Configuration *c,char *err,size_t cap);
bool config_load(const wchar_t *path,Configuration *c,char *err,size_t cap);
bool config_profile_get(const Configuration *c,const char *serial,Profile *p);
void config_profile_set(Configuration *c,const char *serial,const Profile *p);
bool process_start(Process *p,const Arguments *args,const wchar_t *adb_override,const char *stdin_text,
                   bool capture,char *err,size_t cap);
size_t process_read(Process *p,char *out,size_t cap);
bool process_alive(Process *p);
void process_stop(Process *p);
void process_close(Process *p);
bool process_run(const Arguments *args,const wchar_t *adb_override,const char *input,HANDLE cancel,
                 DWORD timeout,char *out,size_t cap,DWORD *exit_code,char *err,size_t errcap);
bool adb_server_compatible(int expected,char *err,size_t cap);
bool file_version(const wchar_t *path,char *out,size_t cap);
bool webcam_device_names(char *out,size_t cap);
#endif
