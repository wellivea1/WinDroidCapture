/* CLEARLY LABELED FAKES. No phone, camera device or microphone delivery proof. */
#include "../src/native/manager.h"
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failed,checks;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL line %d %s\n",__LINE__,#c);failed++;}}while(0)
static bool has(int n,wchar_t **args,const wchar_t *s){for(int i=1;i<n;i++)if(!wcscmp(args[i],s))return true;return false;}
static int fake(int argc,wchar_t **args){
    wchar_t self[AWM_PATH];GetModuleFileNameW(NULL,self,AWM_PATH);wchar_t *base=PathFindFileNameW(self);
    if(!wcscmp(base,L"adb.exe")){
        if(has(argc,args,L"version"))puts("Android Debug Bridge version 1.0.41\nVersion FAKE TEST ADB");
        else if(has(argc,args,L"help"))puts("pair HOST[:PORT]\nmdns services");
        else if(has(argc,args,L"devices")){wchar_t offline[4];puts(GetEnvironmentVariableW(L"AWM_TEST_OFFLINE",offline,4)?"List of devices attached\nFIRST device model:Same transport_id:7\nSELECTED offline model:Same transport_id:8":"List of devices attached\nFIRST offline model:Same transport_id:7\nSELECTED device usb:1 model:Same transport_id:8");}
        else if(has(argc,args,L"ro.build.version.sdk"))puts("37");else if(has(argc,args,L"ro.build.version.release"))puts("FAKE 17");return 0;
    }
    if(!wcscmp(base,L"scrcpy.exe")){
        if(has(argc,args,L"--version"))puts("scrcpy 5.0.1 FAKE TEST");
        else if(has(argc,args,L"--help"))puts("--video-source= --camera-id= --camera-fps= --no-downsize-on-error --display-orientation= --window-width= --window-height= --require-audio --list-cameras --list-camera-sizes --list-encoders --list-displays --max-fps= --capture-orientation=");
        else if(has(argc,args,L"--list-displays"))puts("List of displays:\n--display-id=0 (1344x2992)");
        else if(has(argc,args,L"--list-camera-sizes"))puts("--camera-id=5 (back, 1920x1080, fps={30, 60})\n - 1920x1080");
        else if(has(argc,args,L"--list-encoders"))puts("--video-codec=h264 --video-encoder=fake.encoder (hw)");return 0;
    }
    if(!wcscmp(base,L"awm-media-host.exe")){
        const wchar_t *mapping=AWM_FRAME_MAPPING;for(int i=1;i+1<argc;i++)if(!wcscmp(args[i],L"--mapping"))mapping=args[i+1];AwmHub hub={0};if(!awm_hub_open_named(&hub,true,mapping))return 1;hub.view->source_kind=has(argc,args,L"--screen")?1:0;BYTE p[16*16*4];memset(p,19,sizeof(p));
        ULONGLONG end=GetTickCount64()+20000;while(!hub.view->stop&&GetTickCount64()<end){awm_hub_publish(&hub,p,16,16,123);Sleep(33);}awm_hub_stop(&hub);awm_hub_close(&hub);return 0;
    }
    if(!wcscmp(base,L"scrcpy-awm.exe")){if(has(argc,args,L"--version")){puts("AWM export protocol 3 / FAKE TEST upstream client");return 0;}wchar_t fail[4];if(GetEnvironmentVariableW(L"AWM_TEST_CHILD_FAIL",fail,4))return 33;Sleep(20000);return 0;}
    return -1;
}
static bool wait_state(NativeManager *m,NativeView *v,int goal,unsigned timeout){
    ULONGLONG end=GetTickCount64()+timeout;while(GetTickCount64()<end){native_manager_snapshot(m,v);if(goal==0&&!v->busy&&v->devices.count==2)return true;if(goal==1&&v->active&&v->frames>1)return true;if(goal==2&&!v->active&&!v->busy)return true;if(goal==3&&v->capabilities.camera_count==1&&!v->busy)return true;if(goal==4&&!v->active&&strstr(v->notice,"exited"))return true;Sleep(20);}return false;
}
int wmain(int argc,wchar_t **args){
    int child=fake(argc,args);if(child>=0)return child;platform_init();CoInitializeEx(NULL,COINIT_MULTITHREADED);
    wchar_t tmp[AWM_PATH],dir[AWM_PATH],self[AWM_PATH],settings[AWM_PATH],p[AWM_PATH];GetTempPathW(AWM_PATH,tmp);swprintf(dir,AWM_PATH,L"%sAWM-fakes-%lu-日本語",tmp,GetCurrentProcessId());CreateDirectoryW(dir,NULL);GetModuleFileNameW(NULL,self,AWM_PATH);
    const wchar_t *files[]={L"adb.exe",L"scrcpy.exe",L"scrcpy-awm.exe",L"awm-media-host.exe"};for(int i=0;i<4;i++){swprintf(p,AWM_PATH,L"%s\\%s",dir,files[i]);CHECK(CopyFileW(self,p,FALSE));}
    swprintf(settings,AWM_PATH,L"%s\\settings.dpapi",dir);NativeSettings *s=calloc(1,sizeof(*s));s->magic=0x324D5741;s->version=6;s->output_width=1920;s->output_height=1080;s->output_fps=30;swprintf(s->adb,AWM_PATH,L"%s\\adb.exe",dir);swprintf(s->scrcpy,AWM_PATH,L"%s\\scrcpy.exe",dir);CHECK(native_settings_save(s,settings));free(s);
    NativeManager *m=native_manager_create_at(NULL,settings,dir);NativeView *v=calloc(1,sizeof(*v));CHECK(wait_state(m,v,0,10000));CHECK(v->outputs_checked&&!v->source_live&&!v->microphone_forwarding);NativeCommand *c=calloc(1,sizeof(*c));c->device=v->devices.items[1];CHECK(!strcmp(c->device.serial,"SELECTED"));c->action=N_PROBE;CHECK(native_manager_send(m,c));CHECK(wait_state(m,v,3,5000));profile_default(&c->profile,0);awm_copy(c->profile.camera_id,80,"5");c->profile.audio=false;
    c->action=N_START;CHECK(native_manager_send(m,c));c->action=N_STOP;CHECK(native_manager_send(m,c));Sleep(300);CHECK(wait_state(m,v,2,5000));CHECK(!v->active);/* Cancelled generation must not resurrect. */
    c->action=N_START;CHECK(native_manager_send(m,c));CHECK(wait_state(m,v,1,5000));CHECK(v->source_live&&!v->microphone_forwarding&&v->phase==N_RUNNING);CHECK(wcsstr(v->command,L"SELECTED")&&!wcsstr(v->command,L"FIRST"));c->action=N_STOP;CHECK(native_manager_send(m,c));Sleep(100);CHECK(wait_state(m,v,2,5000));CHECK(!v->source_live&&!v->microphone_forwarding);
    SetEnvironmentVariableW(L"AWM_TEST_CHILD_FAIL",L"1");c->action=N_START;CHECK(native_manager_send(m,c));CHECK(wait_state(m,v,4,5000));SetEnvironmentVariableW(L"AWM_TEST_CHILD_FAIL",NULL);CHECK(!v->active);CHECK(!v->source_live&&!v->microphone_forwarding&&v->phase==N_ERROR);AwmHub hub={0};CHECK(!awm_hub_open(&hub,false));
    c->action=N_START;CHECK(native_manager_send(m,c));CHECK(wait_state(m,v,1,5000));SetEnvironmentVariableW(L"AWM_TEST_OFFLINE",L"1");c->action=N_REFRESH;CHECK(native_manager_send(m,c));Sleep(100);CHECK(wait_state(m,v,2,6000));CHECK(strstr(v->notice,"lost authorization")!=NULL&&!v->active);CHECK(!v->source_live&&!v->microphone_forwarding);CHECK(!awm_hub_open(&hub,false));SetEnvironmentVariableW(L"AWM_TEST_OFFLINE",NULL);
    c->action=N_REFRESH;CHECK(native_manager_send(m,c));Sleep(100);CHECK(wait_state(m,v,0,10000));c->device=v->devices.items[1];
    AwmHub old_reader={0};wchar_t previous[200]=L"";
    for(int iteration=0;iteration<10;iteration++){
        CaptureKind kind=iteration%2?CAPTURE_CAMERA:CAPTURE_SCREEN;c->action=N_PROBE;c->capture.kind=kind;CHECK(native_manager_send(m,c));
        ULONGLONG end=GetTickCount64()+5000;bool probed=false;
        while(GetTickCount64()<end){native_manager_snapshot(m,v);if(!v->busy&&v->capabilities_serial[0]&&v->capabilities_kind==kind){probed=true;break;}Sleep(20);}CHECK(probed);
        native_capture_default(&c->capture,kind,0);c->capture.audio=false;c->capture.screen.display_id=0;awm_copy(c->capture.camera.id,80,"5");c->use_capture=true;c->action=N_START;
        CHECK(native_manager_send(m,c)&&wait_state(m,v,1,5000));CHECK(wcscmp(previous,v->mapping_name)!=0);
        CHECK(wcsstr(v->command,kind==CAPTURE_SCREEN?L"--video-source=display":L"--video-source=camera")!=NULL);
        if(old_reader.view){CHECK(!awm_hub_live(&old_reader,GetTickCount64()));awm_hub_close(&old_reader);}
        CHECK(awm_hub_open_named(&old_reader,false,v->mapping_name));awm_wcopy(previous,200,v->mapping_name);c->action=N_STOP;
        CHECK(native_manager_send(m,c));CHECK(!awm_hub_live(&old_reader,GetTickCount64()));CHECK(wait_state(m,v,2,5000));
    }
    awm_hub_close(&old_reader);
    /* Required microphone absence is a refusal, never a silent audio-Off fallback. */
    if(!v->microphone_installed){c->capture.audio=true;c->action=N_START;CHECK(native_manager_send(m,c));Sleep(100);CHECK(wait_state(m,v,2,5000)&&!v->active&&strstr(v->notice,"signed driver"));c->capture.audio=false;}
    /* Candidate executable existence alone is not a successful dependency validation. */
    awm_wcopy(c->settings.adb,AWM_PATH,v->settings.adb);awm_wcopy(c->settings.scrcpy,AWM_PATH,v->settings.adb);c->action=N_SETTINGS;
    CHECK(native_manager_send(m,c));Sleep(100);native_manager_snapshot(m,v);CHECK(wcsstr(v->settings.scrcpy,L"scrcpy.exe")!=NULL);
    native_manager_destroy(m);
    /* Remove only explicitly created files; no recursive path deletion. */
    for(int i=0;i<4;i++){swprintf(p,AWM_PATH,L"%s\\%s",dir,files[i]);DeleteFileW(p);}DeleteFileW(settings);RemoveDirectoryW(dir);
    printf("SIMULATED integration: %d checks, %d failures. Safe Unicode subprocesses, multiple-device selection, start/stop race, process failure, selected-device loss, 10 camera/screen switches, retained stopped readers, required-mic refusal, path validation. No physical delivery assertion.\n",checks,failed);
    free(v);free(c);CoUninitialize();platform_shutdown();return failed?1:0;
}
