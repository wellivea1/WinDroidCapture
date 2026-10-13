#ifndef AWM_OBS_H
#define AWM_OBS_H
#include "platform.h"
#include "json.h"
typedef struct {
    SOCKET socket;
    unsigned sequence, port;
    int last_status_code;
    HANDLE cancel;
    char version[64], websocket_version[64], available[AWM_TEXT];
    char scene[128], input[128], previous_scene[256];
    int item_id;
    bool connected, scene_created, input_created, switched, virtual_owned, cleanup_user_control;
    int base_width, base_height, output_width, output_height, fps_num, fps_den;
} Obs;
bool obs_connect(Obs *o,unsigned port,const wchar_t *password,HANDLE cancel,char *err,size_t cap);
void obs_disconnect(Obs *o);
bool obs_request(Obs *o,const char *type,const char *data,char *response,size_t cap,char *err,size_t errcap);
bool obs_preflight(Obs *o,char *err,size_t cap);
bool obs_start_output(Obs *o,const wchar_t *window_title,const wchar_t *exe_name,bool consent,char *err,size_t cap);
bool obs_poll_output(Obs *o,int *source_width,int *source_height,char *err,size_t cap);
bool obs_blank(Obs *o,char *err,size_t cap);
bool obs_stop_output(Obs *o,char *err,size_t cap);
bool obs_auth_string(const char *password,const char *salt,const char *challenge,char *out,size_t cap);
#endif
