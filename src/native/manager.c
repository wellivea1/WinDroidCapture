#include "manager.h"
#include "command.h"
#include "microphone_client.h"
#include "../../camera/camera_ids.h"
#include "device_profile.h"
#include <mfapi.h>
#include <mfidl.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct NativeManager {
    HWND window;HANDLE thread,wake,cancel;CRITICAL_SECTION lock;volatile LONG quit,stop,epoch;
    NativeCommand queue[8];int count;NativeView view,shared;
    Process capture,host;AwmHub hub;Device selected;NativeCaptureProfile profile;wchar_t directory[AWM_PATH],settings_path[AWM_PATH];
    int adb_protocol;ULONGLONG discover_at,outputs_at,start_at,retry_at;unsigned retries;bool desired;
};
static void publish(NativeManager *m){EnterCriticalSection(&m->lock);m->shared=m->view;LeaveCriticalSection(&m->lock);if(m->window)PostMessageW(m->window,NATIVE_UPDATE,0,0);}
static void log_text(NativeManager *m,const char *t){char clean[8192];awm_copy(clean,sizeof(clean),t);redact_text(clean,&m->view.devices);size_t n=strlen(m->view.diagnostics),l=strlen(clean);if(n+l+3>=AWM_TEXT){memmove(m->view.diagnostics,m->view.diagnostics+n/2,n-n/2+1);n=strlen(m->view.diagnostics);}snprintf(m->view.diagnostics+n,AWM_TEXT-n,"%s\r\n",clean);}
static void notice(NativeManager *m,const char *t){awm_copy(m->view.notice,sizeof(m->view.notice),t);log_text(m,t);publish(m);}
static void save_settings(NativeManager *m){m->view.profile_saved=m->view.settings_writable&&native_settings_save(&m->view.settings,m->settings_path);if(!m->view.profile_saved)awm_copy(m->view.notice,sizeof(m->view.notice),"Settings were not saved; the existing rejected/unwritable file is preserved.");}
static void path(NativeManager *m,wchar_t *p,const wchar_t *name){swprintf(p,AWM_PATH,L"%s\\%s",m->directory,name);}
static bool run(NativeManager *m,Arguments *a,const char *input,char *out,size_t cap,DWORD timeout){
    char err[2048]={0};DWORD code=0;bool ok=process_run(a,m->view.settings.adb,input,m->cancel,timeout,out,cap,&code,err,sizeof(err));
    if(!ok||code){if(input)notice(m,"Pairing failed or cancelled; the pairing code was not logged.");else notice(m,err[0]?err:out);return false;}return true;
}
static bool guard(NativeManager *m){char err[2048];if(!m->adb_protocol){notice(m,"ADB is unverified. Choose its executable in Setup.");return false;}if(!adb_server_compatible(m->adb_protocol,err,sizeof(err))){notice(m,err);return false;}return true;}
static void adb_args(NativeManager *m,Arguments *a,const Device *d){memset(a,0,sizeof(*a));args_add(a,m->view.settings.adb);if(d){wchar_t id[256];if(d->transport_id[0]){args_add(a,L"-t");utf8_to_wide(d->transport_id,id,256);}else{args_add(a,L"-s");utf8_to_wide(d->serial,id,256);}args_add(a,id);}}
static void stop(NativeManager *m,const char *why,bool failed){
    if(m->hub.view)awm_hub_stop(&m->hub);process_close(&m->capture);process_close(&m->host);awm_hub_close(&m->hub);
    m->view.active=false;m->view.busy=false;m->view.source_live=m->view.microphone_forwarding=false;m->view.phase=failed?N_ERROR:N_IDLE;m->view.mapping_name[0]=0;m->view.peak=0;m->view.active_serial[0]=m->view.active_transport[0]=0;m->view.request_width=m->view.request_height=m->view.request_format=0;m->view.camera_requests=m->view.repeated_samples=0;
    awm_copy(m->view.camera_status,512,why);awm_copy(m->view.mic_status,512,"Phone microphone stopped; driver queue resets on producer close.");
    if(failed&&m->desired&&m->profile.reconnect&&m->retries<3){m->retry_at=GetTickCount64()+3000;m->retries++;m->view.phase=N_RECONNECTING;}else{m->desired=false;m->retry_at=0;}notice(m,why);
}
static void outputs(NativeManager *m){
    m->outputs_at=GetTickCount64()+5000;IMFAttributes *a=NULL;IMFActivate **devices=NULL;UINT32 count=0;m->view.camera_registered=false;m->view.camera_name[0]=0;
    if(SUCCEEDED(MFCreateAttributes(&a,1))&&SUCCEEDED(IMFAttributes_SetGUID(a,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))&&SUCCEEDED(MFEnumDeviceSources(a,&devices,&count))){
        for(UINT32 i=0;i<count;i++){wchar_t *name=NULL;UINT32 len=0;if(SUCCEEDED(IMFActivate_GetAllocatedString(devices[i],&MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&len))){if(wcsstr(name,AWM_CAMERA_FRIENDLY)){m->view.camera_registered=true;awm_wcopy(m->view.camera_name,256,name);}CoTaskMemFree(name);}IMFActivate_Release(devices[i]);}CoTaskMemFree(devices);}if(a)IMFAttributes_Release(a);
    AudioEndpoints *endpoints=calloc(1,sizeof(*endpoints));char err[512];m->view.microphone_installed=false;m->view.microphone_name[0]=0;m->view.microphone_id[0]=0;
    if(endpoints&&audio_enumerate(endpoints,err,sizeof(err))){int matches=0;for(int i=0;i<endpoints->count;i++){AudioEndpoint *ep=&endpoints->items[i];if(ep->flow==eCapture&&ep->state==DEVICE_STATE_ACTIVE&&wcsstr(ep->name,L"Android Webcam Microphone")){matches++;awm_wcopy(m->view.microphone_name,256,ep->name);awm_wcopy(m->view.microphone_id,1024,ep->id);}}HANDLE h=CreateFileW(L"\\\\.\\AWMPhoneMic",0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);m->view.microphone_installed=matches==1&&h!=INVALID_HANDLE_VALUE;if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}free(endpoints);
    awm_copy(m->view.output_status,512,m->view.camera_registered?"Camera is enumerated by Windows; receiver delivery is a separate test.":"Camera is not registered. Use Setup → Install camera source / Register camera.");
    m->view.outputs_checked=true;
}
static void tools(NativeManager *m){
    Arguments *a=calloc(1,sizeof(*a));char *out=calloc(AWM_TEXT,1),*help=calloc(AWM_TEXT,1),*ah=calloc(AWM_TEXT,1);if(!a||!out||!help||!ah)goto done;
    m->view.busy=true;publish(m);m->adb_protocol=0;args_add(a,m->view.settings.adb);args_add(a,L"version");
    if(run(m,a,NULL,out,AWM_TEXT,5000)){int x,y;sscanf(out,"Android Debug Bridge version %d.%d.%d",&x,&y,&m->adb_protocol);out[strcspn(out,"\r\n")]=0;awm_copy(m->view.adb_version,256,out);a->count=1;args_add(a,L"help");run(m,a,NULL,ah,AWM_TEXT,5000);}
    a->count=0;args_add(a,m->view.settings.scrcpy);args_add(a,L"--version");
    if(run(m,a,NULL,out,AWM_TEXT,5000)){out[strcspn(out,"\r\n")]=0;awm_copy(m->view.scrcpy_version,256,out);a->count=1;args_add(a,L"--help");run(m,a,NULL,help,AWM_TEXT,5000);}
    parse_features(help,ah,&m->view.features);path(m,m->view.capture_client,L"scrcpy-awm.exe");a->count=0;args_add(a,m->view.capture_client);args_add(a,L"--version");m->view.capture_version[0]=0;if(run(m,a,NULL,out,AWM_TEXT,5000)){out[strcspn(out,"\r\n")]=0;awm_copy(m->view.capture_version,256,out);}outputs(m);
done:free(a);free(out);free(help);free(ah);m->view.busy=false;publish(m);
}
static void discover(NativeManager *m){
    m->discover_at=GetTickCount64()+3000;if(!guard(m))return;Arguments *a=calloc(1,sizeof(*a));DeviceList *list=calloc(1,sizeof(*list));char *out=calloc(AWM_TEXT,1);if(!a||!list||!out)goto done;
    adb_args(m,a,NULL);args_add(a,L"devices");args_add(a,L"-l");if(!run(m,a,NULL,out,AWM_TEXT,3000)||!parse_adb_devices(out,list)){if(m->view.active)stop(m,"ADB discovery failed; phone outputs stopped.",true);goto done;}
    for(int i=0;i<list->count;i++){Device *d=&list->items[i];int old=find_device(&m->view.devices,d->serial,d->transport_id);if(d->state==DEV_AUTHORIZED){if(old>=0&&m->view.devices.items[old].sdk){d->sdk=m->view.devices.items[old].sdk;awm_copy(d->android,64,m->view.devices.items[old].android);}else{adb_args(m,a,d);args_add(a,L"shell");args_add(a,L"getprop");args_add(a,L"ro.build.version.sdk");if(run(m,a,NULL,out,AWM_TEXT,1500))d->sdk=atoi(out);a->count--;args_add(a,L"ro.build.version.release");if(run(m,a,NULL,out,AWM_TEXT,1500)){out[strcspn(out,"\r\n")]=0;awm_copy(d->android,64,out);}}}}
    merge_devices(&m->view.devices,list);if(m->view.active){int i=find_device(list,m->selected.serial,m->selected.transport_id);if(i<0||list->items[i].state!=DEV_AUTHORIZED)stop(m,"Selected phone disconnected or lost authorization.",true);}publish(m);
done:free(a);free(list);free(out);
}
static bool selected(NativeManager *m,Device *d){int i=find_device(&m->view.devices,d->serial,d->transport_id);if(i<0||m->view.devices.items[i].state!=DEV_AUTHORIZED){notice(m,"The selected transport is unavailable. Refresh and select it again.");return false;}*d=m->view.devices.items[i];return true;}
static void probe(NativeManager *m,Device d,CaptureKind kind){
    m->view.probe_revision++;
    if(m->view.active||!selected(m,&d)||!guard(m)){publish(m);return;}
    if(kind==CAPTURE_CAMERA&&(d.sdk<31||d.transport==TRANSPORT_EMULATOR)){notice(m,"This transport supports screen probing; camera mode needs a physical Android 12+ phone.");return;}
    Arguments *a=calloc(1,sizeof(*a));char *out=calloc(AWM_TEXT,1);if(!a||!out)goto done;
    m->view.busy=true;m->view.capabilities_serial[0]=0;publish(m);memset(&m->view.capabilities,0,sizeof(m->view.capabilities));memset(&m->view.displays,0,sizeof(m->view.displays));
    wchar_t serial[256],server[AWM_PATH];path(m,server,L"scrcpy-server-awm");bool pixel=kind==CAPTURE_CAMERA&&native_pixel_profile(&d)&&path_is_file(server);
    utf8_to_wide(d.serial,serial,256);args_add(a,pixel?m->view.capture_client:m->view.settings.scrcpy);args_add(a,L"--serial");args_add(a,serial);
    if(kind==CAPTURE_CAMERA){args_add(a,L"--video-source=camera");args_add(a,L"--list-camera-sizes");}
    else {args_add(a,L"--list-displays");args_add(a,L"--no-audio");args_add(a,L"--no-control");}
    if(run(m,a,NULL,out,AWM_TEXT,15000)){
        bool recognized=true;if(kind==CAPTURE_CAMERA){parse_capabilities(out,&m->view.capabilities);recognized=m->view.capabilities.camera_count>0;}
        else recognized=native_parse_displays(out,&m->view.displays)&&m->view.displays.count>0;
        log_text(m,out);a->count=3;args_add(a,L"--list-encoders");if(run(m,a,NULL,out,AWM_TEXT,15000))parse_capabilities(out,&m->view.capabilities);
        if(recognized){awm_copy(m->view.capabilities_serial,256,d.serial);awm_copy(m->view.capabilities_transport,32,d.transport_id);awm_copy(m->view.capabilities_version,256,m->view.scrcpy_version);m->view.capabilities_kind=kind;notice(m,"Capabilities inspected for the selected source/transport. Startup validates the actual combination.");}
        else notice(m,"No recognized usable source enumeration. Inspect diagnostics; unknown output is not healthy.");
    }
done:free(a);free(out);m->view.busy=false;publish(m);
}
static void start(NativeManager *m,Device d,NativeCaptureProfile p,LONG epoch){
    if(m->view.active||!selected(m,&d)||!guard(m))return;
    if(strcmp(m->view.capabilities_serial,d.serial)||strcmp(m->view.capabilities_transport,d.transport_id)||m->view.capabilities_kind!=p.kind||strcmp(m->view.capabilities_version,m->view.scrcpy_version)){notice(m,"Inspect this source on the selected transport before starting.");m->desired=false;return;}
    char err[2048]={0},note[1024];if(!native_capture_declared(&p,&m->view.capabilities,&m->view.displays,note,sizeof(note))){notice(m,note);m->desired=false;return;}
    if(p.audio&&!m->view.microphone_installed){notice(m,"Phone microphone output unavailable: our signed driver is not installed. Choose Audio Off explicitly for video-only capture.");m->desired=false;return;}
    if(!strstr(m->view.scrcpy_version,"scrcpy 5.0.1")||!strstr(m->view.capture_version,"AWM export protocol 3")){notice(m,"A matching upstream 5.0.1 probe distribution and AWM export protocol 3 client are required; a stock client cannot export to our host.");m->desired=false;return;}
    GUID guid;CoCreateGuid(&guid);wchar_t id[64],pipe[200],exe[AWM_PATH],b[128];StringFromGUID2(&guid,id,64);swprintf(pipe,200,L"\\\\.\\pipe\\AWM.Export.%s",id);swprintf(m->view.mapping_name,200,L"Local\\AWM.Camera.Frames.%s",id);
    Arguments *a=calloc(1,sizeof(*a)),*ha=calloc(1,sizeof(*ha));if(!a||!ha){free(a);free(ha);m->desired=false;return;}
    path(m,exe,L"scrcpy-awm.exe");if(!native_capture_args_profile(exe,pipe,&d,&p,&m->view.features,a,err,sizeof(err))){notice(m,err);free(a);free(ha);m->desired=false;return;}
    quote_windows_args(a,m->view.command,32768);path(m,exe,L"awm-media-host.exe");args_add(ha,exe);args_add(ha,L"--pipe");args_add(ha,pipe);args_add(ha,L"--mapping");args_add(ha,m->view.mapping_name);
    args_add(ha,L"--scaler");swprintf(b,128,L"%u",m->view.settings.pipeline_scaler);args_add(ha,b);args_add(ha,L"--framing");swprintf(b,128,L"%u",p.framing);args_add(ha,b);
    args_add(ha,L"--video-buffer");swprintf(b,128,L"%u",m->view.settings.video_buffer);args_add(ha,b);args_add(ha,L"--capture-fps");swprintf(b,128,L"%d",p.kind==CAPTURE_CAMERA?p.camera.fps:p.screen.fps_limit);args_add(ha,b);
    args_add(ha,L"--rotate");swprintf(b,128,L"%d",p.rotation);args_add(ha,b);if(p.kind==CAPTURE_SCREEN)args_add(ha,L"--screen");if(p.mirror)args_add(ha,L"--mirror");if(p.audio)args_add(ha,L"--microphone");
    m->view.busy=true;m->view.phase=N_STARTING;publish(m);
    bool ok=epoch==m->epoch&&WaitForSingleObject(m->cancel,0)!=WAIT_OBJECT_0&&process_start(&m->host,ha,m->view.settings.adb,NULL,true,err,sizeof(err));free(ha);
    if(ok){ULONGLONG deadline=GetTickCount64()+3000;while(!awm_hub_open_named(&m->hub,false,m->view.mapping_name)&&GetTickCount64()<deadline&&WaitForSingleObject(m->cancel,40)==WAIT_TIMEOUT&&process_alive(&m->host)){}ok=m->hub.view!=NULL&&WaitForSingleObject(m->cancel,0)!=WAIT_OBJECT_0;}
    if(ok)ok=process_start(&m->capture,a,m->view.settings.adb,NULL,true,err,sizeof(err));free(a);
    if(!ok||epoch!=m->epoch||WaitForSingleObject(m->cancel,0)==WAIT_OBJECT_0){stop(m,err[0]?err:"Start cancelled or media host did not become ready.",false);return;}
    m->selected=d;m->profile=p;m->start_at=GetTickCount64();m->view.active=true;m->view.active_kind=p.kind;m->view.receiver_checked=false;m->view.frames=m->view.audio_samples=0;
    awm_copy(m->view.active_serial,256,d.serial);awm_copy(m->view.active_transport,32,d.transport_id);native_capture_set(&m->view.settings,d.serial,&p);save_settings(m);
    notice(m,p.kind==CAPTURE_SCREEN?"Starting selected phone display. Receiver delivery is a separate test.":"Starting selected phone camera. Receiver delivery is a separate test.");publish(m);
}
static bool validate_paths(NativeManager *m,const NativeSettings *candidate){
    if(!path_is_file(candidate->adb)||!path_is_file(candidate->scrcpy)){notice(m,"Both selected executable paths must exist; previous paths were preserved.");return false;}
    Arguments *a=calloc(1,sizeof(*a));char *out=calloc(AWM_TEXT,1);if(!a||!out){free(a);free(out);return false;}
    char error[1024];DWORD code=0;args_add(a,candidate->adb);args_add(a,L"version");
    bool ok=process_run(a,candidate->adb,NULL,m->cancel,5000,out,AWM_TEXT,&code,error,sizeof(error))&&code==0&&strstr(out,"Android Debug Bridge version ");
    if(ok){a->count=0;args_add(a,candidate->scrcpy);args_add(a,L"--version");ok=process_run(a,candidate->adb,NULL,m->cancel,5000,out,AWM_TEXT,&code,error,sizeof(error))&&code==0&&strstr(out,"scrcpy 5.0.1");}
    free(a);free(out);if(!ok)notice(m,"Executable validation failed. Select ADB and the matching upstream scrcpy 5.0.1; previous paths were preserved.");return ok;
}
static void handle(NativeManager *m,NativeCommand *c){
    switch(c->action){
    case N_REFRESH:tools(m);discover(m);break;
    case N_SCALERS:if(c->settings.preview_scaler<=2&&c->settings.pipeline_scaler<=2&&c->settings.ui_theme<=2&&c->settings.video_buffer<=1){m->view.settings.video_buffer=c->settings.video_buffer;m->view.settings.ui_theme=c->settings.ui_theme;m->view.settings.preview_scaler=c->settings.preview_scaler;m->view.settings.pipeline_scaler=c->settings.pipeline_scaler;if((c->settings.output_width==1280&&c->settings.output_height==720&&c->settings.output_fps==30)||(c->settings.output_width==1920&&c->settings.output_height==1080&&(c->settings.output_fps==30||c->settings.output_fps==60))||(c->settings.output_width==3840&&c->settings.output_height==2160&&c->settings.output_fps==30)){m->view.settings.output_width=c->settings.output_width;m->view.settings.output_height=c->settings.output_height;m->view.settings.output_fps=c->settings.output_fps;}save_settings(m);notice(m,m->view.profile_saved?"Settings saved.":"Settings could not be saved; see Diagnostics.");}break;
    case N_SETTINGS:if(m->view.active){notice(m,"Stop capture before changing dependency paths.");break;}if(!validate_paths(m,&c->settings))break;m->view.settings=c->settings;save_settings(m);m->view.capabilities_serial[0]=0;tools(m);discover(m);break;
    case N_PROBE:probe(m,c->device,c->capture.kind);break;
    case N_START:{if(c->epoch!=m->epoch)break;m->desired=true;m->retries=0;NativeCaptureProfile p;if(c->use_capture)p=c->capture;else native_capture_from_legacy(&p,&c->profile);start(m,c->device,p,c->epoch);break;}
    case N_PROFILE:{NativeCaptureProfile p;if(c->use_capture)p=c->capture;else native_capture_from_legacy(&p,&c->profile);char err[1024];if(native_capture_validate(&p,&c->device,&m->view.features,err,sizeof(err))){native_capture_set(&m->view.settings,c->device.serial,&p);save_settings(m);publish(m);}else notice(m,err);break;}
    case N_REGISTER:{Arguments *a=calloc(1,sizeof(*a));char out[4096];wchar_t exe[AWM_PATH];path(m,exe,L"awm-camera-control.exe");args_add(a,exe);args_add(a,L"register-user");if(run(m,a,NULL,out,sizeof(out),15000))notice(m,out);free(a);outputs(m);publish(m);break;}
    case N_PAIR:case N_CONNECT:case N_DISCONNECT:case N_MDNS:{
        if(!guard(m))break;if(c->action!=N_MDNS&&!valid_wireless_address(c->address)){notice(m,"Enter a valid address and port. Pairing and connection ports can differ.");break;}
        if(c->action==N_PAIR&&(!m->view.features.adb_pair||!valid_pairing_code(c->code))){notice(m,"Pairing requires a supported ADB and a six digit phone pairing code.");break;}
        Arguments *a=calloc(1,sizeof(*a));char out[4096],code[32]={0};adb_args(m,a,NULL);
        if(c->action==N_MDNS){args_add(a,L"mdns");args_add(a,L"services");}else{args_add(a,c->action==N_PAIR?L"pair":c->action==N_CONNECT?L"connect":L"disconnect");args_add(a,c->address);}
        if(c->action==N_PAIR){wide_to_utf8(c->code,code,sizeof(code));strcat_s(code,sizeof(code),"\n");}
        if(run(m,a,c->action==N_PAIR?code:NULL,out,sizeof(out),15000))notice(m,c->action==N_PAIR?"Pairing completed. Connect using the connection address/port shown on the phone.":out);
        SecureZeroMemory(code,sizeof(code));free(a);discover(m);break;}
    default:break;}
}
static DWORD WINAPI worker(void *context){
    NativeManager *m=context;CoInitializeEx(NULL,COINIT_MULTITHREADED);MFStartup(MF_VERSION,MFSTARTUP_FULL);tools(m);discover(m);
    while(!m->quit){
        if(InterlockedExchange(&m->stop,0)){m->desired=false;m->retry_at=0;stop(m,"Stopped. Camera receivers get the stopped state; no phone audio is forwarded.",false);ResetEvent(m->cancel);}
        NativeCommand *c=calloc(1,sizeof(*c));bool has=false;EnterCriticalSection(&m->lock);if(c&&m->count){*c=m->queue[0];memmove(m->queue,m->queue+1,(--m->count)*sizeof(*c));has=true;}LeaveCriticalSection(&m->lock);if(has){handle(m,c);SecureZeroMemory(c,sizeof(*c));}free(c);
        if(m->view.active){
            char out[8192];while(process_read(&m->capture,out,sizeof(out)))log_text(m,out);while(process_read(&m->host,out,sizeof(out)))log_text(m,out);
            if(!process_alive(&m->capture)||!process_alive(&m->host))stop(m,"An owned capture process exited. Phone outputs stopped.",true);
            else if(m->hub.view){AwmFrameHub *h=m->hub.view;m->view.frames=h->video_frames;m->view.audio_samples=h->audio_frames;m->view.peak=h->microphone_peak;long index=InterlockedCompareExchange(&h->published,0,0);if(index>=0&&index<2){m->view.width=h->slots[index].width;m->view.height=h->slots[index].height;}
                bool live=awm_hub_live(&m->hub,GetTickCount64());m->view.source_live=live;m->view.microphone_forwarding=live&&m->profile.audio&&h->microphone_live&&h->microphone_driver;m->view.content_age_ms=h->heartbeat_ms?GetTickCount64()-h->heartbeat_ms:0;m->view.video_epoch=h->video_epoch;m->view.camera_requests=h->camera_requests;m->view.repeated_samples=h->repeated_samples;m->view.request_width=h->request_width;m->view.request_height=h->request_height;m->view.request_format=h->request_format;
                if(live&&m->view.phase==N_STARTING&&(!m->profile.audio||(h->microphone_live&&h->microphone_driver))){m->view.phase=N_RUNNING;m->view.busy=false;}
                awm_copy(m->view.camera_status,512,live?(m->profile.kind==CAPTURE_SCREEN&&m->view.content_age_ms>1000?"Screen connected - no new content updates":"Source frames available"):"Waiting for a valid source frame");
                awm_copy(m->view.mic_status,512,!m->profile.audio?"Phone microphone disabled":h->microphone_live&&h->microphone_driver?"Phone PCM forwarded to our driver; test its Windows endpoint.":"Waiting for microphone / driver");
                if(GetTickCount64()-m->start_at>15000&&(!live||(m->profile.audio&&!h->microphone_live)))stop(m,"Capture did not deliver required video/audio before its startup deadline.",true);
            }publish(m);
        }
        if(GetTickCount64()>=m->discover_at)discover(m);if(GetTickCount64()>=m->outputs_at){outputs(m);if(m->view.active&&m->profile.audio&&!m->view.microphone_installed)stop(m,"Required own microphone endpoint disappeared; phone outputs stopped.",true);publish(m);}
        if(m->retry_at&&GetTickCount64()>=m->retry_at){m->retry_at=0;if(m->desired&&!m->stop){int i=find_device(&m->view.devices,m->selected.serial,m->selected.transport_id);if(i>=0&&m->view.devices.items[i].state==DEV_AUTHORIZED)start(m,m->selected,m->profile,m->epoch);else if(m->retries<3){m->retry_at=GetTickCount64()+3000;m->retries++;m->view.phase=N_RECONNECTING;}else m->desired=false;}}
        WaitForSingleObject(m->wake,100);
    }
    m->desired=false;stop(m,"Manager closed.",false);MFShutdown();CoUninitialize();return 0;
}
NativeManager *native_manager_create_at(HWND window,const wchar_t *settings,const wchar_t *components){
    NativeManager *m=calloc(1,sizeof(*m));if(!m)return NULL;InitializeCriticalSection(&m->lock);m->window=window;m->wake=CreateEventW(NULL,FALSE,FALSE,NULL);m->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);GetModuleFileNameW(NULL,m->directory,AWM_PATH);PathRemoveFileSpecW(m->directory);
    native_settings_defaults(&m->view.settings);if(settings)awm_wcopy(m->settings_path,AWM_PATH,settings);else native_settings_path(m->settings_path);if(components)awm_wcopy(m->directory,AWM_PATH,components);m->view.settings_writable=native_settings_load(&m->view.settings,m->settings_path)||!path_is_file(m->settings_path);if(!m->view.settings_writable)awm_copy(m->view.notice,sizeof(m->view.notice),"Existing settings were rejected and will not be overwritten.");awm_copy(m->view.camera_status,512,"Camera stopped");awm_copy(m->view.mic_status,512,"Microphone stopped");m->shared=m->view;m->thread=CreateThread(NULL,0,worker,m,0,NULL);return m;
}
NativeManager *native_manager_create(HWND window){return native_manager_create_at(window,NULL,NULL);}
void native_manager_begin_close(NativeManager *m){if(!m)return;InterlockedExchange(&m->quit,1);SetEvent(m->cancel);SetEvent(m->wake);NativeCommand c={0};c.action=N_STOP;native_manager_send(m,&c);}
bool native_manager_closed(NativeManager *m){return !m||WaitForSingleObject(m->thread,0)==WAIT_OBJECT_0;}
void native_manager_destroy(NativeManager *m){if(!m)return;native_manager_begin_close(m);WaitForSingleObject(m->thread,INFINITE);CloseHandle(m->thread);CloseHandle(m->cancel);CloseHandle(m->wake);DeleteCriticalSection(&m->lock);free(m);}
bool native_manager_send(NativeManager *m,const NativeCommand *c){
    if(c->action==N_STOP){InterlockedIncrement(&m->epoch);InterlockedExchange(&m->stop,1);SetEvent(m->cancel);wchar_t mapping[200];EnterCriticalSection(&m->lock);awm_wcopy(mapping,200,m->shared.mapping_name);m->shared.phase=N_STOPPING;m->shared.source_live=m->shared.microphone_forwarding=false;LeaveCriticalSection(&m->lock);AwmHub h={0};if(mapping[0]&&awm_hub_open_named(&h,false,mapping)){awm_hub_stop(&h);awm_hub_close(&h);}SetEvent(m->wake);if(m->window)PostMessageW(m->window,NATIVE_UPDATE,0,0);return true;}
    EnterCriticalSection(&m->lock);bool ok=m->count<8;if(ok){m->queue[m->count]=*c;m->queue[m->count].epoch=m->epoch;m->count++;if(c->action==N_START&&!m->shared.active){m->shared.phase=N_STARTING;m->shared.busy=true;}}LeaveCriticalSection(&m->lock);if(ok){SetEvent(m->wake);if(m->window)PostMessageW(m->window,NATIVE_UPDATE,0,0);}return ok;
}
void native_manager_snapshot(NativeManager *m,NativeView *v){EnterCriticalSection(&m->lock);*v=m->shared;LeaveCriticalSection(&m->lock);}
