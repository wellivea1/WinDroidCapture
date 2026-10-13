#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
struct Engine {
    CRITICAL_SECTION lock;
    HWND window; HANDLE thread,wake,cancel;
    volatile LONG shutdown,stop_requested,stop_epoch;
    uint32_t operation_epoch;
    Command queue[16];int queue_count;
    EngineView view;
    Process capture; Obs obs; AudioMeter meter;
    Device selected; Profile active_profile;
    wchar_t window_title[128],settings_path[AWM_PATH];HWND capture_window;
    char capture_lines[4096];size_t lines_len;
    int adb_protocol;
    ULONGLONG started,output_started,next_discovery,next_endpoint_check,next_obs_check,next_retry;
    bool mic_test,output_desired,retry_microphone;
};
static void notify(Engine *e){if(e->window)PostMessageW(e->window,AWM_UPDATE,0,0);}
static void lock(Engine *e){EnterCriticalSection(&e->lock);}
static void unlock(Engine *e){LeaveCriticalSection(&e->lock);}
static void log_line(Engine *e,const char *text){
    char clean[8192];awm_copy(clean,sizeof(clean),text);lock(e);redact_text(clean,&e->view.devices);
    size_t a=strlen(e->view.diagnostics),b=strlen(clean);if(a+b+3>=AWM_TEXT){size_t drop=a>AWM_TEXT/2?a-AWM_TEXT/2:a;memmove(e->view.diagnostics,e->view.diagnostics+drop,a-drop+1);a-=drop;}
    snprintf(e->view.diagnostics+a,AWM_TEXT-a,"%s\r\n",clean);unlock(e);notify(e);
}
static void notice(Engine *e,const char *text){lock(e);awm_copy(e->view.notice,sizeof(e->view.notice),text);unlock(e);log_line(e,text);}
static bool run(Engine *e,Arguments *args,bool scrcpy,const char *input,char *out,size_t cap,DWORD timeout,char *error,size_t ec){
    DWORD code=0;bool ok=process_run(args,scrcpy?e->view.configuration.adb:NULL,input,e->cancel,timeout,out,cap,&code,error,ec);
    if(!ok)return false;if(code){snprintf(error,ec,"Subprocess exited with code %lu. %s",code,input?"Pairing code was not logged.":out);return false;}return true;
}
static bool adb_guard(Engine *e){char err[1024];if(e->adb_protocol<1){notice(e,"ADB version is unknown; select and verify the executable in Setup.");return false;}if(!adb_server_compatible(e->adb_protocol,err,sizeof(err))){lock(e);awm_copy(e->view.adb_status,sizeof(e->view.adb_status),err);unlock(e);notice(e,err);return false;}return true;}
static void adb_args(Engine *e,Arguments *a,const Device *d){memset(a,0,sizeof(*a));args_add(a,e->view.configuration.adb);if(d){args_add(a,L"-s");wchar_t id[256];utf8_to_wide(d->serial,id,256);args_add(a,id);}}
static bool save_settings(Engine *e){char err[1024];if(!config_save(e->settings_path,&e->view.configuration,err,sizeof(err))){notice(e,err);return false;}return true;}
static void enumerate_audio(Engine *e){AudioEndpoints *endpoints=calloc(1,sizeof(*endpoints));char err[512];if(!endpoints)return;if(audio_enumerate(endpoints,err,sizeof(err))){lock(e);e->view.endpoints=*endpoints;unlock(e);}else notice(e,err);free(endpoints);}
static void load_tools(Engine *e){
    char *out=malloc(AWM_TEXT),*help=calloc(AWM_TEXT,1),*adb_help=calloc(AWM_TEXT,1);char error[2048];Arguments *a=calloc(1,sizeof(*a));if(!out||!help||!adb_help||!a)goto end;
    lock(e);e->view.busy=true;unlock(e);notify(e);
    e->adb_protocol=0;lock(e);memset(&e->view.features,0,sizeof(e->view.features));unlock(e);
    args_add(a,e->view.configuration.adb);args_add(a,L"version");
    if(run(e,a,false,NULL,out,AWM_TEXT,5000,error,sizeof(error))){char version[256]={0};char *installed=strstr(out,"\nVersion ");if(installed){installed++;char *end=strpbrk(installed,"\r\n");if(end)*end=0;snprintf(version,sizeof(version),"%.120s",installed);}char *line=strpbrk(out,"\r\n");if(line)*line=0;lock(e);snprintf(e->view.adb_version,sizeof(e->view.adb_version),"%.120s%s%.120s",out,version[0]?" / ":"",version);unlock(e);int major,minor,protocol;if(sscanf(out,"Android Debug Bridge version %d.%d.%d",&major,&minor,&protocol)==3)e->adb_protocol=protocol;
        a->count=1;args_add(a,L"help");if(!run(e,a,false,NULL,adb_help,AWM_TEXT,5000,error,sizeof(error)))notice(e,error);
    }else{lock(e);awm_copy(e->view.adb_version,sizeof(e->view.adb_version),"Missing / failed");unlock(e);notice(e,error);}
    a->count=0;args_add(a,e->view.configuration.scrcpy);args_add(a,L"--version");
    if(run(e,a,false,NULL,out,AWM_TEXT,5000,error,sizeof(error))){char *line=strchr(out,'\n');if(line)*line=0;lock(e);awm_copy(e->view.scrcpy_version,sizeof(e->view.scrcpy_version),out);unlock(e);
        a->count=1;args_add(a,L"--help");if(!run(e,a,false,NULL,help,AWM_TEXT,5000,error,sizeof(error)))notice(e,error);
    }else{lock(e);awm_copy(e->view.scrcpy_version,sizeof(e->view.scrcpy_version),"Missing / failed");unlock(e);notice(e,error);}
    ToolFeatures features;parse_features(help,adb_help,&features);lock(e);e->view.features=features;unlock(e);
    char version[128]={0};file_version(e->view.configuration.obs,version,sizeof(version));lock(e);awm_copy(e->view.obs_version,sizeof(e->view.obs_version),version[0]?version:"Not found");unlock(e);
    char webcams[2048]={0};webcam_device_names(webcams,sizeof(webcams));lock(e);awm_copy(e->view.webcams,sizeof(e->view.webcams),webcams);unlock(e);enumerate_audio(e);
end:
    free(out);free(help);free(adb_help);free(a);lock(e);e->view.busy=false;unlock(e);notify(e);
}
static bool probe_android(Engine *e,Device *d){
    Arguments *a=calloc(1,sizeof(*a));if(!a)return false;char out[2048],err[2048];adb_args(e,a,d);args_add(a,L"shell");args_add(a,L"getprop");args_add(a,L"ro.build.version.release");
    bool ok=run(e,a,false,NULL,out,sizeof(out),2000,err,sizeof(err));if(ok){out[strcspn(out,"\r\n")]=0;awm_copy(d->android,sizeof(d->android),out);}
    a->count-=1;args_add(a,L"ro.build.version.sdk");if(ok&&run(e,a,false,NULL,out,sizeof(out),2000,err,sizeof(err))){d->sdk=atoi(out);if(d->sdk>0&&d->sdk<31)d->state=DEV_UNSUPPORTED;}free(a);return ok;
}
static void stop_capture(Engine *e,const char *why,bool failure){
    /* End phone forwarding first, even when OBS is slow or unreachable. */
    process_close(&e->capture);e->capture_window=NULL;audio_meter_close(&e->meter);e->lines_len=0;
    bool bridge_ok=true;char bridge_error[1024]={0};HANDLE saved_cancel=e->obs.cancel;e->obs.cancel=NULL;
    if(e->obs.connected)bridge_ok=obs_stop_output(&e->obs,bridge_error,sizeof(bridge_error));
    else if(e->obs.virtual_owned)bridge_ok=false;e->obs.cancel=saved_cancel;obs_disconnect(&e->obs);
    lock(e);e->view.output_plan=false;e->view.microphone=false;e->view.routed=false;e->view.mic_confirmed=false;e->view.peak=0;
    e->view.active_serial[0]=e->view.active_transport[0]=0;
    awm_copy(e->view.camera_status,sizeof(e->view.camera_status),why);awm_copy(e->view.mic_status,sizeof(e->view.mic_status),"Phone microphone stopped");awm_copy(e->view.route_status,sizeof(e->view.route_status),"No phone signal forwarded; cable may contain other applications");
    awm_copy(e->view.obs_status,sizeof(e->view.obs_status),bridge_ok?(e->obs.cleanup_user_control?"Owned source disabled / blanked; OBS scene and outputs left under user control after external activity":"Owned source disabled / blanked; owned Virtual Camera stopped when still under app control"):"OBS cleanup could not be confirmed. Phone stopped; inspect OBS for a stale image or running Virtual Camera.");
    if(failure)session_failure(&e->view.session,e->view.session.generation,e->active_profile.reconnect);
    else session_stop(&e->view.session);if(e->view.session.phase==SESSION_RETRY)e->next_retry=GetTickCount64()+2500;
    unlock(e);e->output_desired=false;notice(e,why);if(!bridge_ok)notice(e,bridge_error[0]?bridge_error:"OBS was unreachable during cleanup; blanking cannot be verified.");
}
static void discover(Engine *e){
    e->next_discovery=GetTickCount64()+3000;if(!adb_guard(e))return;
    Arguments *a=calloc(1,sizeof(*a));DeviceList *snapshot=calloc(1,sizeof(*snapshot));char *out=malloc(AWM_TEXT),err[2048]={0};if(!a||!snapshot||!out)goto end;
    adb_args(e,a,NULL);args_add(a,L"devices");args_add(a,L"-l");
    if(!run(e,a,false,NULL,out,AWM_TEXT,4000,err,sizeof(err))||!parse_adb_devices(out,snapshot)){
        lock(e);awm_copy(e->view.adb_status,sizeof(e->view.adb_status),"Discovery failed; connection health is unknown");unlock(e);
        if(e->capture.process)stop_capture(e,"ADB discovery failed; capture stopped to avoid presenting a stale session.",true);notice(e,err[0]?err:"ADB returned unrecognized device-list output; connection state remains unknown.");goto end;}
    int budget=8;
    for(int i=0;i<snapshot->count&&budget>0;i++){Device *d=&snapshot->items[i];int old=find_device(&e->view.devices,d->serial,"");if(d->state==DEV_AUTHORIZED&&(old<0||e->view.devices.items[old].sdk==0||e->view.devices.items[old].state!=DEV_AUTHORIZED)){probe_android(e,d);budget--;}}
    lock(e);merge_devices(&e->view.devices,snapshot);snprintf(e->view.adb_status,sizeof(e->view.adb_status),"Discovery succeeded: %d present transport(s). Select a device explicitly.%s",snapshot->count,snapshot->truncated?" List truncated; capacity is 64.":"");
    int active=find_device(&e->view.devices,e->selected.serial,e->selected.transport_id);bool lost=e->capture.process&&(active<0||e->view.devices.items[active].state!=DEV_AUTHORIZED);unlock(e);
    if(lost)stop_capture(e,"Selected device disconnected, changed authorization, or lost its transport; camera and microphone stopped.",true);notify(e);
end:free(a);free(snapshot);free(out);
}
static bool device_current(Engine *e,Device *d){int i=find_device(&e->view.devices,d->serial,d->transport_id);if(i<0||e->view.devices.items[i].state!=DEV_AUTHORIZED){notice(e,"The explicitly selected device is no longer authorized on that transport. Refresh and select it again.");return false;}*d=e->view.devices.items[i];if(d->sdk==0)probe_android(e,d);if(d->sdk<31||d->transport==TRANSPORT_EMULATOR){notice(e,"Camera mode requires an authorized physical Android 12+ phone.");return false;}return true;}
static void probe(Engine *e,Device d){
    if(e->capture.process){notice(e,"Stop the current capture before probing cameras; probing opens a separate scrcpy server.");return;}if(!adb_guard(e)||!device_current(e,&d))return;
    char *out=malloc(AWM_TEXT),err[2048];Arguments *a=calloc(1,sizeof(*a));Capabilities *c=calloc(1,sizeof(*c));if(!out||!a||!c)goto end;
    lock(e);e->view.busy=true;unlock(e);notify(e);args_add(a,e->view.configuration.scrcpy);args_add(a,L"--serial");wchar_t serial[256];utf8_to_wide(d.serial,serial,256);args_add(a,serial);
    int base=a->count;
    if(!e->view.features.list_cameras||!e->view.features.list_sizes||!e->view.features.list_encoders){notice(e,"Installed scrcpy lacks camera-size or encoder enumeration.");goto end;}
    args_add(a,L"--video-source=camera");args_add(a,L"--list-camera-sizes");
    if(run(e,a,true,NULL,out,AWM_TEXT,15000,err,sizeof(err)))parse_capabilities(out,c);else{notice(e,err);goto end;}
    a->count=base;args_add(a,L"--list-encoders");if(run(e,a,true,NULL,out,AWM_TEXT,15000,err,sizeof(err)))parse_capabilities(out,c);else{notice(e,err);goto end;}
    lock(e);e->view.capabilities=*c;awm_copy(e->view.capability_serial,sizeof(e->view.capability_serial),d.serial);unlock(e);notice(e,"Camera, size, frame-rate, zoom-range, and encoder enumeration completed. Declared modes still require a startup test.");
end:free(out);free(a);free(c);lock(e);e->view.busy=false;unlock(e);notify(e);
}
typedef struct { DWORD pid;const wchar_t *title;HWND window; } WindowSearch;
static BOOL CALLBACK find_capture_window(HWND w,LPARAM value){WindowSearch *search=(WindowSearch *)value;DWORD pid;GetWindowThreadProcessId(w,&pid);if(pid==search->pid){wchar_t title[256];GetWindowTextW(w,title,256);if(!wcscmp(title,search->title)){search->window=w;return FALSE;}}return TRUE;}
static void parse_capture_line(Engine *e,char *line){
    if(!line[0])return;log_line(e,line);char *p=strstr(line,"Texture: ");int w,h;
    if(p&&sscanf(p+9,"%dx%d",&w,&h)==2&&w>0&&h>0){lock(e);e->view.observed_width=w;e->view.observed_height=h;unlock(e);}
    p=strstr(line,"INFO: ");int fps;if(p&&sscanf(p+6,"%d fps",&fps)==1&&fps>0&&fps<1000){lock(e);e->view.last_fps=fps;unlock(e);}
}
static void read_capture(Engine *e){char data[4096];size_t n;
    while((n=process_read(&e->capture,data,sizeof(data)))>0)for(size_t i=0;i<n;i++){char c=data[i];if(c=='\n'){e->capture_lines[e->lines_len]=0;parse_capture_line(e,e->capture_lines);e->lines_len=0;}else if(c!='\r'){if(e->lines_len+1>=sizeof(e->capture_lines)){e->capture_lines[e->lines_len]=0;parse_capture_line(e,e->capture_lines);e->lines_len=0;}e->capture_lines[e->lines_len++]=c;}}
}
static bool window_native_size(Engine *e,bool resize,char *err,size_t cap){
    if(!e->capture_window){WindowSearch s={e->capture.pid,e->window_title,NULL};EnumWindows(find_capture_window,(LPARAM)&s);e->capture_window=s.window;}
    if(!e->capture_window||!IsWindow(e->capture_window)||IsIconic(e->capture_window)||!IsWindowVisible(e->capture_window)){awm_copy(err,cap,"Owned capture window is missing, minimized, or hidden. Keep it visible and unminimized for Windows Graphics Capture.");return false;}
    int w=e->view.observed_width,h=e->view.observed_height;if(e->active_profile.rotation==90||e->active_profile.rotation==270){int t=w;w=h;h=t;}
    if(w<1||h<1){awm_copy(err,cap,"Decoded stream dimensions have not been observed yet.");return false;}
    if(resize)SetWindowPos(e->capture_window,NULL,20,20,w,h,SWP_NOACTIVATE|SWP_NOZORDER);
    RECT r;GetClientRect(e->capture_window,&r);if(r.right!=w||r.bottom!=h){snprintf(err,cap,"Capture window client size is %ldx%ld; native output needs %dx%d. Webcam output is blocked to avoid downscaling and upscaling.",r.right,r.bottom,w,h);return false;}return true;
}
static bool start_capture(Engine *e,Device d,Profile p,bool mic,bool retry){
    if(InterlockedCompareExchange(&e->shutdown,0,0)||WaitForSingleObject(e->cancel,0)==WAIT_OBJECT_0||e->operation_epoch!=(uint32_t)InterlockedCompareExchange(&e->stop_epoch,0,0))return false;
    if(e->capture.process){notice(e,"One capture session is already active. Stop it before changing device or settings.");return false;}
    if(!adb_guard(e)||!device_current(e,&d))return false;
    if(strcmp(e->view.capability_serial,d.serial)){notice(e,"Probe this selected phone before starting camera capture.");return false;}
    char err[2048],declared[1024];if(!validate_profile(&p,&e->view.features,err,sizeof(err))){notice(e,err);return false;}
    bool is_declared=profile_declared(&p,&e->view.capabilities,declared,sizeof(declared));
    /* Unsupported declarations can be explicitly attempted from the editable settings; no automatic fallback. */
    if(!is_declared){const Camera *c=NULL;for(int i=0;i<e->view.capabilities.camera_count;i++)if(!strcmp(e->view.capabilities.cameras[i].id,p.camera_id))c=&e->view.capabilities.cameras[i];if(!c||(p.zoom!=1.0f&&(c->zoom_min<=0||p.zoom<c->zoom_min||p.zoom>c->zoom_max))||p.encoder[0]){notice(e,declared);return false;}notice(e,declared);}
    if(mic&&(!p.audio||!audio_endpoint_active(e->view.configuration.playback_id,eRender)||!audio_endpoint_active(e->view.configuration.recording_id,eCapture))){notice(e,"Microphone mode needs audio enabled and an explicitly selected active virtual-cable playback / recording pair in Audio setup.");return false;}
    GUID guid;wchar_t token[40];CoCreateGuid(&guid);StringFromGUID2(&guid,token,40);swprintf(e->window_title,128,L"AWM camera %s",token);
    Arguments *args=calloc(1,sizeof(*args));if(!args)return false;
    if(!build_scrcpy_args(e->view.configuration.scrcpy,&d,&p,&e->view.features,e->window_title,mic,args,err,sizeof(err))){free(args);notice(e,err);return false;}
    lock(e);if(!retry)session_start(&e->view.session);else e->view.session.phase=SESSION_STARTING;e->view.observed_width=e->view.observed_height=e->view.last_fps=0;e->view.microphone=mic;e->view.mic_confirmed=false;e->view.routed=false;e->view.peak=0;e->view.output_plan=false;
    quote_windows_args(args,e->view.effective_command,32768);awm_copy(e->view.active_serial,sizeof(e->view.active_serial),d.serial);awm_copy(e->view.active_transport,sizeof(e->view.active_transport),d.transport_id);
    awm_copy(e->view.camera_status,sizeof(e->view.camera_status),"Starting camera; discovery alone is not a capture session");awm_copy(e->view.mic_status,sizeof(e->view.mic_status),mic?"Microphone requested with --require-audio; awaiting playback and phone-specific test":"Phone microphone disabled for this preview");awm_copy(e->view.obs_status,sizeof(e->view.obs_status),"Preview only; OBS webcam output has not started");unlock(e);
    e->selected=d;e->active_profile=p;e->retry_microphone=mic;e->lines_len=0;e->started=GetTickCount64();e->next_endpoint_check=e->next_obs_check=0;
    if(WaitForSingleObject(e->cancel,0)==WAIT_OBJECT_0||e->operation_epoch!=(uint32_t)InterlockedCompareExchange(&e->stop_epoch,0,0)){free(args);stop_capture(e,"Start cancelled by Stop before process creation.",false);return false;}
    bool ok=process_start(&e->capture,args,e->view.configuration.adb,NULL,true,err,sizeof(err));free(args);
    if(e->operation_epoch!=(uint32_t)InterlockedCompareExchange(&e->stop_epoch,0,0)){stop_capture(e,"Start cancelled by Stop during process creation.",false);return false;}
    if(!ok){stop_capture(e,err,true);return false;}
    if(mic&&!audio_meter_open(&e->meter,e->view.configuration.recording_id,err,sizeof(err))){stop_capture(e,err,true);return false;}
    lock(e);config_profile_set(&e->view.configuration,d.serial,&p);unlock(e);save_settings(e);notify(e);return true;
}
static void make_output_plan(Engine *e,Command *c){
    if(!e->capture.process){if(!start_capture(e,c->device,c->profile,c->profile.audio,false))return;}
    else if(strcmp(e->selected.serial,c->device.serial)||memcmp(&e->active_profile,&c->profile,sizeof(Profile))){notice(e,"Stop the current session to apply the selected device or changed profile before starting webcam output.");return;}
    else if(e->view.microphone!=c->profile.audio&&e->view.session.phase!=SESSION_OUTPUT){stop_capture(e,"Restarting this owned preview with the requested microphone setting.",false);if(!start_capture(e,c->device,c->profile,c->profile.audio,false))return;}
    if(e->view.session.phase==SESSION_OUTPUT){notice(e,"Webcam output is already active.");return;}
    char window_error[2048];if(!e->view.observed_width||!window_native_size(e,true,window_error,sizeof(window_error))){notice(e,"Wait for camera startup, then press Start webcam again. Keep its clean window visible.");return;}
    if(e->view.observed_width!=e->active_profile.width||e->view.observed_height!=e->active_profile.height){notice(e,"Observed stream dimensions differ from the requested mode. Edit the profile to the actual dimensions before starting webcam; no fallback is relabeled.");return;}
    if(e->view.microphone&&!e->view.routed){notice(e,"Camera and mic capture are running for setup. Route scrcpy to the selected cable playback endpoint in Windows Volume Mixer, then press Start webcam again.");return;}
    char err[2048];obs_disconnect(&e->obs);
    if(!obs_connect(&e->obs,e->view.configuration.obs_port,e->view.configuration.obs_password,e->cancel,err,sizeof(err))||!obs_preflight(&e->obs,err,sizeof(err))){stop_capture(e,err,true);return;}
    lock(e);e->view.obs_width=e->obs.output_width;e->view.obs_height=e->obs.output_height;e->view.obs_fps_num=e->obs.fps_num;e->view.obs_fps_den=e->obs.fps_den;e->view.plan_generation=e->view.session.generation;e->view.output_plan=true;
    snprintf(e->view.plan,sizeof(e->view.plan),"OBS %s (WebSocket %s) has no active stream, recording, replay buffer, or Virtual Camera.\r\n\r\nThis session will create an owned scene and capture only the unique scrcpy camera window, switch OBS's program scene from \"%s\", and start OBS Virtual Camera. Stop will disable that source, stop the Virtual Camera we started if still under app control, and restore the previous scene.\r\n\r\nCamera requested/observed: %dx%d at requested %d FPS.\r\nOBS canvas: %dx%d. OBS output: %dx%d at %d/%d FPS. These OBS settings will be kept.\r\n\r\nIf Virtual Camera is configured in OBS to output a specific scene/source rather than Program, select Program manually. This cannot be inspected or changed through the standard WebSocket API. Receiving-application verification is still required.\r\n\r\nAllow this OBS program-scene change and Virtual Camera start?",e->obs.version,e->obs.websocket_version,e->obs.previous_scene,e->view.observed_width,e->view.observed_height,e->active_profile.fps,e->obs.base_width,e->obs.base_height,e->obs.output_width,e->obs.output_height,e->obs.fps_num,e->obs.fps_den);
    unlock(e);notify(e);
}
static void commit_output(Engine *e,Command *c){
    if(!e->view.output_plan||c->generation!=e->view.plan_generation||!c->flag){lock(e);e->view.output_plan=false;unlock(e);obs_disconnect(&e->obs);return;}
    lock(e);e->view.output_plan=false;unlock(e);char err[2048];wchar_t exe[AWM_PATH];awm_wcopy(exe,AWM_PATH,e->view.configuration.scrcpy);const wchar_t *base=PathFindFileNameW(exe);
    if(!window_native_size(e,false,err,sizeof(err))||!obs_start_output(&e->obs,e->window_title,base,true,err,sizeof(err))){stop_capture(e,err,true);return;}
    lock(e);session_complete(&e->view.session,e->view.session.generation,true);awm_copy(e->view.obs_status,sizeof(e->view.obs_status),"OBS Virtual Camera started. Capture dimensions and receiving-app delivery still need verification.");unlock(e);e->output_desired=true;e->output_started=GetTickCount64();e->next_obs_check=e->output_started+800;notify(e);
}
static void wireless(Engine *e,Command *c){
    if(!adb_guard(e))return;Arguments *a=calloc(1,sizeof(*a));char out[8192],err[2048],input[32]={0};if(!a)return;
    if(c->type==CMD_PAIR||c->type==CMD_CONNECT||c->type==CMD_DISCONNECT){if(!valid_wireless_address(c->address)){notice(e,"Enter a valid hostname/IPv4 or [IPv6] address and explicit port (1..65535). Pairing and connection ports can differ.");goto end;}}
    if(c->type==CMD_PAIR){if(!e->view.features.adb_pair||!valid_pairing_code(c->pairing_code)){notice(e,"Modern pairing is unavailable or the code is not six digits. Select a newer ADB and enter the current phone code.");goto end;}
        adb_args(e,a,NULL);args_add(a,L"pair");args_add(a,c->address);wide_to_utf8(c->pairing_code,input,sizeof(input));strcat(input,"\n");
    }else if(c->type==CMD_CONNECT||c->type==CMD_DISCONNECT){adb_args(e,a,NULL);args_add(a,c->type==CMD_CONNECT?L"connect":L"disconnect");args_add(a,c->address);
    }else if(c->type==CMD_RECONNECT){Device d=c->device;if(!device_current(e,&d))goto end;adb_args(e,a,&d);args_add(a,L"reconnect");
    }else{if(!e->view.features.adb_mdns){notice(e,"This ADB lacks mDNS services listing. Use manual address and separate pairing / connection ports.");goto end;}adb_args(e,a,NULL);args_add(a,L"mdns");args_add(a,L"services");}
    bool ok=run(e,a,false,c->type==CMD_PAIR?input:NULL,out,sizeof(out),15000,err,sizeof(err));
    if(c->type==CMD_PAIR){notice(e,ok&&strstr(out,"Successfully paired")?"Wireless pairing succeeded. Enter the separate connection address/port if ADB does not auto-connect.":"Wireless pairing failed, was cancelled, or timed out. Pairing codes are never persisted or logged.");}
    else if(ok)notice(e,out);else notice(e,err);
    e->next_discovery=0;
end:SecureZeroMemory(input,sizeof(input));SecureZeroMemory(c->pairing_code,sizeof(c->pairing_code));free(a);
}
static void command(Engine *e,Command *c){
    switch(c->type){
    case CMD_CONFIG:
        if(e->capture.process){notice(e,"Stop capture before changing dependency, OBS, or endpoint setup.");break;}
        if(c->configuration){lock(e);e->view.configuration=*c->configuration;unlock(e);save_settings(e);load_tools(e);e->next_discovery=0;}break;
    case CMD_REFRESH:enumerate_audio(e);discover(e);break;
    case CMD_PROBE:probe(e,c->device);break;
    case CMD_PREVIEW:start_capture(e,c->device,c->profile,false,false);break;
    case CMD_MIC_SETUP:start_capture(e,c->device,c->profile,true,false);break;
    case CMD_OUTPUT_PLAN:make_output_plan(e,c);break;
    case CMD_OUTPUT_COMMIT:commit_output(e,c);break;
    case CMD_STOP:stop_capture(e,"Stopped by user; camera and phone microphone delivery ended.",false);break;
    case CMD_PAIR:case CMD_CONNECT:case CMD_DISCONNECT:case CMD_RECONNECT:case CMD_MDNS:wireless(e,c);break;
    case CMD_ADB_REPAIR:{
        if(!c->flag)break;if(e->capture.process)stop_capture(e,"Explicit ADB repair requested; capture stopped.",false);
        Arguments *a=calloc(1,sizeof(*a));char out[4096],err[2048];if(!a)break;adb_args(e,a,NULL);args_add(a,L"kill-server");if(run(e,a,false,NULL,out,sizeof(out),5000,err,sizeof(err))){a->count=1;args_add(a,L"start-server");if(!run(e,a,false,NULL,out,sizeof(out),5000,err,sizeof(err)))notice(e,err);else notice(e,"Shared ADB server explicitly restarted. Other ADB users may need to reconnect.");}else notice(e,err);free(a);e->next_discovery=0;break;}
    case CMD_METER:{if(e->view.microphone)break;audio_meter_close(&e->meter);char err[1024];if(c->flag&&!audio_meter_open(&e->meter,e->view.configuration.recording_id,err,sizeof(err)))notice(e,err);else notice(e,c->flag?"Cable signal meter active. Signal alone does not identify the phone; use the phone-specific test.":"Signal meter stopped.");break;}
    case CMD_MIC_CONFIRM:
        if(e->view.microphone&&e->view.routed){lock(e);e->view.mic_confirmed=c->flag;unlock(e);notice(e,"Phone-specific speak/tap test marked confirmed by the user for this session. A moving shared-cable meter alone is not evidence.");}else notice(e,"Start microphone setup and route this scrcpy process to the chosen cable before confirming its source.");break;
    case CMD_SAVE_PROFILE:lock(e);config_profile_set(&e->view.configuration,c->device.serial,&c->profile);unlock(e);save_settings(e);notice(e,"Per-device profile saved. No capture starts on launch.");break;
    case CMD_OBS_CHECK:{
        if(e->capture.process){notice(e,"Stop capture before testing a new OBS connection.");break;}
        Obs *test=calloc(1,sizeof(*test));char err[2048];if(!test)break;
        if(obs_connect(test,e->view.configuration.obs_port,e->view.configuration.obs_password,e->cancel,err,sizeof(err))){char text[512];snprintf(text,sizeof(text),"Connected and authenticated to OBS %s / WebSocket %s / RPC 1. No OBS settings or outputs were changed.",test->version,test->websocket_version);lock(e);awm_copy(e->view.obs_status,sizeof(e->view.obs_status),text);unlock(e);notice(e,text);obs_disconnect(test);}else notice(e,err);free(test);break;}
    }
    if(c->configuration){SecureZeroMemory(c->configuration,sizeof(*c->configuration));free(c->configuration);c->configuration=NULL;}
}
static void tick(Engine *e){
    ULONGLONG now=GetTickCount64();
    if(e->meter.capture){float peak;if(audio_meter_tick(&e->meter,&peak)){lock(e);e->view.peak=peak;unlock(e);}else{audio_meter_close(&e->meter);if(e->capture.process&&e->view.microphone){stop_capture(e,"Selected recording endpoint failed or was removed; phone forwarding stopped.",true);return;}notice(e,"Recording endpoint meter unavailable.");}}
    if(e->capture.process){
        read_capture(e);if(!process_alive(&e->capture)){char reason[1024];snprintf(reason,sizeof(reason),"scrcpy exited (code %lu). Check local diagnostics for unplugging, wireless loss, camera-in-use, audio failure, or unsupported mode.",e->capture.exit_code);stop_capture(e,reason,true);return;}
        if(e->view.session.phase==SESSION_STARTING){char error[2048];if(e->view.observed_width&&window_native_size(e,true,error,sizeof(error))){lock(e);session_complete(&e->view.session,e->view.session.generation,false);unlock(e);}
            else if(now-e->started>15000){stop_capture(e,"Camera startup timed out without a decoded stream and owned window. No output is considered ready.",true);return;}}
        if(e->view.observed_width){lock(e);snprintf(e->view.camera_status,sizeof(e->view.camera_status),"Observed decoded stream: %dx%d; last reported sample %d FPS. Requested: %dx%d at %d FPS.%s",e->view.observed_width,e->view.observed_height,e->view.last_fps,e->active_profile.width,e->active_profile.height,e->active_profile.fps,(e->view.observed_width==e->active_profile.width&&e->view.observed_height==e->active_profile.height)?"":" DIMENSIONS DIFFER; webcam output blocked.");unlock(e);}
        if(now>=e->next_endpoint_check&&e->view.microphone){e->next_endpoint_check=now+1000;
            if(!audio_endpoint_active(e->view.configuration.playback_id,eRender)||!audio_endpoint_active(e->view.configuration.recording_id,eCapture)){stop_capture(e,"Selected cable endpoint disappeared; phone camera and microphone forwarding stopped.",true);return;}
            int other=0;bool routed=audio_process_routed(e->view.configuration.playback_id,e->capture.pid,&other);lock(e);e->view.routed=routed;if(!routed)e->view.mic_confirmed=false;
            snprintf(e->view.route_status,sizeof(e->view.route_status),routed?"This scrcpy PID is active on the selected playback endpoint; %d other active session(s). Paired recording endpoint must be confirmed by a phone-specific test.":"This scrcpy PID is not observed on the chosen cable. Open Windows Volume Mixer and set its Output device to the selected cable playback endpoint.",other);
            awm_copy(e->view.mic_status,sizeof(e->view.mic_status),routed?(e->view.mic_confirmed?"Required microphone capture + intended playback session observed; phone speak/tap test USER CONFIRMED":"Required microphone capture + intended playback session observed; phone source NOT VERIFIED"):"Microphone required by scrcpy; virtual-cable route is incomplete");bool output_lost=!routed&&e->view.session.phase==SESSION_OUTPUT;unlock(e);if(output_lost){stop_capture(e,"Phone playback left the selected cable; output session stopped.",true);return;}}
        if(e->view.session.phase==SESSION_OUTPUT&&now>=e->next_obs_check){e->next_obs_check=now+1500;char err[2048];int w=0,h=0;
            if(!window_native_size(e,false,err,sizeof(err))||!obs_poll_output(&e->obs,&w,&h,err,sizeof(err))){stop_capture(e,err,true);return;}
            int expected_w=e->view.observed_width,expected_h=e->view.observed_height;if(e->active_profile.rotation==90||e->active_profile.rotation==270){int tmp=expected_w;expected_w=expected_h;expected_h=tmp;}
            if(w!=expected_w||h!=expected_h){if(now-e->output_started>10000){stop_capture(e,"OBS source dimensions do not match the native camera window. Output stopped; no upscaled-resolution claim is made.",true);return;}}
            lock(e);snprintf(e->view.obs_status,sizeof(e->view.obs_status),"Virtual Camera running; owned WGC source %dx%d, OBS output %dx%d at %d/%d FPS. Select OBS Virtual Camera in the receiver. Receiver delivery remains NOT VERIFIED.",w,h,e->obs.output_width,e->obs.output_height,e->obs.fps_num,e->obs.fps_den);unlock(e);}
    }
    if(now>=e->next_discovery)discover(e);
    if(e->view.session.phase==SESSION_RETRY&&now>=e->next_retry){
        e->next_retry=now+2500;int i=find_device(&e->view.devices,e->selected.serial,"");if(i>=0&&e->view.devices.items[i].state==DEV_AUTHORIZED){bool mic=e->retry_microphone;if(start_capture(e,e->view.devices.items[i],e->active_profile,mic,true))notice(e,"Opt-in reconnection applies only to the same selected serial. Capture restarted; OBS output needs a fresh explicit approval.");else{lock(e);session_failure(&e->view.session,e->view.session.generation,true);unlock(e);}}
        else{lock(e);session_failure(&e->view.session,e->view.session.generation,true);bool exhausted=!e->view.session.desired;unlock(e);if(exhausted)notice(e,"Automatic reconnection exhausted its three bounded attempts. Press Start to try again.");}}
    notify(e);
}
static DWORD WINAPI worker(LPVOID arg){
    Engine *e=arg;CoInitializeEx(NULL,COINIT_MULTITHREADED);platform_init();e->obs.socket=INVALID_SOCKET;
    Configuration *initial=calloc(1,sizeof(*initial));if(!initial){CoUninitialize();return 1;}config_defaults(initial);if(!e->settings_path[0])config_path(e->settings_path,AWM_PATH);char error[1024];config_load(e->settings_path,initial,error,sizeof(error));detect_dependencies(initial);lock(e);e->view.configuration=*initial;unlock(e);SecureZeroMemory(initial,sizeof(*initial));free(initial);
    lock(e);awm_copy(e->view.adb_status,sizeof(e->view.adb_status),"Waiting for asynchronous discovery");awm_copy(e->view.camera_status,sizeof(e->view.camera_status),"Stopped");awm_copy(e->view.mic_status,sizeof(e->view.mic_status),"Stopped");awm_copy(e->view.obs_status,sizeof(e->view.obs_status),"Not connected; no webcam output started");awm_copy(e->view.route_status,sizeof(e->view.route_status),"Not tested; choose the virtual-cable pair in Audio setup");unlock(e);
    load_tools(e);save_settings(e);notice(e,"Select the exact device, probe capabilities, and start a preview. Setup and discovery never start camera or microphone capture.");
    while(!InterlockedCompareExchange(&e->shutdown,0,0)){
        if(InterlockedExchange(&e->stop_requested,0)){stop_capture(e,"Stopped by user; pending starts and reconnect attempts cancelled.",false);ResetEvent(e->cancel);}
        Command c={0};bool has=false;lock(e);if(e->queue_count){c=e->queue[0];memmove(e->queue,e->queue+1,(size_t)(--e->queue_count)*sizeof(Command));has=true;}unlock(e);
        if(has){e->operation_epoch=c.cancellation_epoch;if(c.cancellation_epoch==(uint32_t)InterlockedCompareExchange(&e->stop_epoch,0,0))command(e,&c);else if(c.configuration){SecureZeroMemory(c.configuration,sizeof(Configuration));free(c.configuration);}SecureZeroMemory(&c,sizeof(c));}else{e->operation_epoch=(uint32_t)InterlockedCompareExchange(&e->stop_epoch,0,0);tick(e);}
        WaitForSingleObject(e->wake,100);
    }
    stop_capture(e,"Application closed; owned capture stopped.",false);platform_shutdown();CoUninitialize();return 0;
}
Engine *engine_create(HWND window){return engine_create_at_settings(window,NULL);}
Engine *engine_create_at_settings(HWND window,const wchar_t *settings){Engine *e=calloc(1,sizeof(*e));if(!e)return NULL;if(settings)awm_wcopy(e->settings_path,AWM_PATH,settings);InitializeCriticalSection(&e->lock);e->window=window;e->wake=CreateEventW(NULL,FALSE,FALSE,NULL);e->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!e->wake||!e->cancel){engine_destroy(e);return NULL;}e->thread=CreateThread(NULL,0,worker,e,0,NULL);if(!e->thread){engine_destroy(e);return NULL;}return e;}
bool engine_send(Engine *e,const Command *c){
    if(c->type==CMD_STOP){InterlockedIncrement(&e->stop_epoch);SetEvent(e->cancel);InterlockedExchange(&e->stop_requested,1);lock(e);for(int i=0;i<e->queue_count;i++)if(e->queue[i].configuration){SecureZeroMemory(e->queue[i].configuration,sizeof(Configuration));free(e->queue[i].configuration);}SecureZeroMemory(e->queue,sizeof(e->queue));e->queue_count=0;unlock(e);SetEvent(e->wake);return true;}
    lock(e);bool ok=e->queue_count<16;if(ok){e->queue[e->queue_count]=*c;e->queue[e->queue_count].cancellation_epoch=(uint32_t)InterlockedCompareExchange(&e->stop_epoch,0,0);if(c->configuration){Configuration *copy=malloc(sizeof(*copy));if(!copy)ok=false;else{*copy=*c->configuration;e->queue[e->queue_count].configuration=copy;}}if(ok)e->queue_count++;}unlock(e);SetEvent(e->wake);return ok;
}
void engine_snapshot(Engine *e,EngineView *out){lock(e);*out=e->view;unlock(e);}
void engine_destroy(Engine *e){if(!e)return;InterlockedExchange(&e->shutdown,1);if(e->cancel)SetEvent(e->cancel);if(e->wake)SetEvent(e->wake);if(e->thread){/* Operations are cancelled; OBS cleanup has individual deadlines. Never free a live worker's memory. */WaitForSingleObject(e->thread,INFINITE);CloseHandle(e->thread);}if(e->wake)CloseHandle(e->wake);if(e->cancel)CloseHandle(e->cancel);for(int i=0;i<e->queue_count;i++)if(e->queue[i].configuration){SecureZeroMemory(e->queue[i].configuration,sizeof(Configuration));free(e->queue[i].configuration);}DeleteCriticalSection(&e->lock);SecureZeroMemory(e,sizeof(*e));free(e);}
