#include "engine.h"
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks=0,failures=0;
#define CHECK(x) do{checks++;if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);failures++;}}while(0)
static const wchar_t *argument(int argc,wchar_t **argv,const wchar_t *key){for(int i=1;i+1<argc;i++)if(!wcscmp(argv[i],key))return argv[i+1];return L"";}
static bool has(int argc,wchar_t **argv,const wchar_t *key){for(int i=1;i<argc;i++)if(!wcscmp(argv[i],key))return true;return false;}
static bool state_is(const char *wanted){FILE *f=fopen("fake-state.txt","rb");if(!f)return false;char state[32]={0};fread(state,1,31,f);fclose(f);return !strcmp(state,wanted);}
static int fake_adb(int argc,wchar_t **argv){
    if(has(argc,argv,L"version")){puts("Android Debug Bridge version 1.0.41\nVersion FAKE for tests");return 0;}
    if(has(argc,argv,L"help")){puts("pair HOST[:PORT] [PAIRING CODE]\nmdns services");return 0;}
    if(has(argc,argv,L"devices")){puts("List of devices attached\nFAKE_A device model:Same_Fake_Model transport_id:1");if(!state_is("disconnected"))puts("FAKE_B device model:Same_Fake_Model transport_id:2");return 0;}
    if(has(argc,argv,L"getprop")){puts(has(argc,argv,L"ro.build.version.sdk")?"37":"17");return 0;}
    puts("FAKE adb: command deliberately unsupported");return 2;
}
static LRESULT CALLBACK fake_window(HWND w,UINT m,WPARAM a,LPARAM b){(void)a;(void)b;if(m==WM_CLOSE){DestroyWindow(w);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}if(m==WM_TIMER){if(state_is("crash"))ExitProcess(9);puts("INFO: 30 fps");return 0;}return DefWindowProcW(w,m,a,b);}
static int fake_scrcpy(int argc,wchar_t **argv){
    setvbuf(stdout,NULL,_IONBF,0);
    if(has(argc,argv,L"--version")){puts("scrcpy FAKE 5.0.1 — no hardware access");return 0;}
    if(has(argc,argv,L"--help")){puts("--video-source=source --camera-id=id --camera-fps=value --list-cameras --list-camera-sizes --list-encoders --require-audio --no-downsize-on-error --display-orientation=value --window-width=value --window-height=value --camera-zoom=zoom mic-unprocessed --hwdec=mode");return 0;}
    if(has(argc,argv,L"--list-camera-sizes")){puts("--camera-id=5 (back, 1920x1080, fps={30, 60}, zoom-range=[1, 4])\n - 1920x1080\n--camera-id=11 (front, 1920x1080, fps={30}, zoom-range=[1, 2])\n - 1920x1080");return 0;}
    if(has(argc,argv,L"--list-encoders")){puts("--video-codec=h264 --video-encoder=FAKE.encoder (sw)");return 0;}
    const wchar_t *serial=argument(argc,argv,L"--serial");if(!serial[0])serial=L"FAKE_UNRELATED";char id[256];wide_to_utf8(serial,id,sizeof(id));printf("FAKE capture explicitly selected %s; no camera or microphone device was opened\nINFO: Texture: 1920x1080\n",id);
    HINSTANCE instance=GetModuleHandleW(NULL);WNDCLASSW c={0};c.hInstance=instance;c.lpfnWndProc=fake_window;c.lpszClassName=L"AwmFakeWindow";c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);
    HWND w=CreateWindowExW(0,c.lpszClassName,argument(argc,argv,L"--window-title"),WS_POPUP|WS_VISIBLE,20,20,1920,1080,NULL,NULL,instance,NULL);SetTimer(w,1,1000,NULL);MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}
static void write_state(const wchar_t *dir,const char *state){wchar_t path[AWM_PATH];swprintf(path,AWM_PATH,L"%s\\fake-state.txt",dir);FILE *f=_wfopen(path,L"wb");if(f){fwrite(state,1,strlen(state),f);fclose(f);}}
static bool wait_for(Engine *e,EngineView *v,int kind,DWORD duration){ULONGLONG until=GetTickCount64()+duration;do{engine_snapshot(e,v);bool yes=false;
        if(kind==0)yes=v->devices.count==2&&v->features.camera&&v->devices.items[1].sdk==37;
        if(kind==1)yes=!strcmp(v->capability_serial,"FAKE_B")&&v->capabilities.camera_count==2;
        if(kind==2)yes=v->session.phase==SESSION_PREVIEW&&!strcmp(v->active_serial,"FAKE_B");
        if(kind==3)yes=v->session.phase==SESSION_IDLE&&!v->active_serial[0];
        if(kind==4)yes=v->session.phase==SESSION_FAILED&&!v->active_serial[0];
        if(kind==5)yes=v->session.phase==SESSION_RETRY&&!v->active_serial[0];
        if(yes)return true;Sleep(30);
    }while(GetTickCount64()<until);printf("Wait failed; notice: %s; phase %d\n",v->notice,v->session.phase);return false;}
int wmain(int argc,wchar_t **argv){
    wchar_t exe[AWM_PATH];GetModuleFileNameW(NULL,exe,AWM_PATH);const wchar_t *name=PathFindFileNameW(exe);if(!wcscmp(name,L"fake-adb.exe"))return fake_adb(argc,argv);if(!wcscmp(name,L"fake-scrcpy.exe"))return fake_scrcpy(argc,argv);
    puts("SIMULATED ENGINE TESTS: FAKE adb and FAKE scrcpy use blank test windows; no phone, camera, microphone, OBS, or receiver success is claimed.");platform_init();CoInitializeEx(NULL,COINIT_MULTITHREADED);
    wchar_t temp[MAX_PATH],dir[AWM_PATH],settings[AWM_PATH],adb[AWM_PATH],scrcpy[AWM_PATH];GetTempPathW(MAX_PATH,temp);swprintf(dir,AWM_PATH,L"%sAWM fake 工具 space %lu",temp,GetCurrentProcessId());CreateDirectoryW(dir,NULL);swprintf(settings,AWM_PATH,L"%s\\settings.dpapi",dir);swprintf(adb,AWM_PATH,L"%s\\fake-adb.exe",dir);swprintf(scrcpy,AWM_PATH,L"%s\\fake-scrcpy.exe",dir);CHECK(CopyFileW(exe,adb,TRUE));CHECK(CopyFileW(exe,scrcpy,TRUE));
    Configuration *config=calloc(1,sizeof(*config));EngineView *view=calloc(1,sizeof(*view));config_defaults(config);awm_wcopy(config->adb,AWM_PATH,adb);awm_wcopy(config->scrcpy,AWM_PATH,scrcpy);config->obs_port=65534;char err[1024];CHECK(config_save(settings,config,err,sizeof(err)));
    Engine *engine=engine_create_at_settings(NULL,settings);CHECK(engine!=NULL);CHECK(wait_for(engine,view,0,12000));Device selected=view->devices.items[1];Command c={0};c.type=CMD_PROBE;c.device=selected;CHECK(engine_send(engine,&c));CHECK(wait_for(engine,view,1,6000));
    Profile p;profile_default(&p,0);awm_copy(p.camera_id,sizeof(p.camera_id),"5");p.audio=false;
    Arguments *unrelated_args=calloc(1,sizeof(*unrelated_args));args_add(unrelated_args,scrcpy);args_add(unrelated_args,L"--window-title");args_add(unrelated_args,L"FAKE unrelated capture");Process unrelated;CHECK(process_start(&unrelated,unrelated_args,NULL,NULL,true,err,sizeof(err)));
    c.type=CMD_PREVIEW;c.profile=p;c.device=selected;CHECK(engine_send(engine,&c));Command stop={0};stop.type=CMD_STOP;CHECK(engine_send(engine,&stop));CHECK(wait_for(engine,view,3,5000));Sleep(500);engine_snapshot(engine,view);CHECK(!view->active_serial[0]&&view->session.phase==SESSION_IDLE);
    CHECK(engine_send(engine,&c));CHECK(wait_for(engine,view,2,6000));CHECK(wcsstr(view->effective_command,L"FAKE_B")!=NULL);CHECK(wcsstr(view->effective_command,L"FAKE_A")==NULL);CHECK(!view->microphone);CHECK(process_alive(&unrelated));
    engine_send(engine,&stop);CHECK(wait_for(engine,view,3,5000));CHECK(process_alive(&unrelated));
    engine_send(engine,&c);CHECK(wait_for(engine,view,2,6000));write_state(dir,"disconnected");Command refresh={0};refresh.type=CMD_REFRESH;engine_send(engine,&refresh);CHECK(wait_for(engine,view,4,6000));CHECK(!view->microphone&&!view->routed);CHECK(process_alive(&unrelated));
    write_state(dir,"present");engine_send(engine,&refresh);CHECK(wait_for(engine,view,0,6000));engine_send(engine,&c);CHECK(wait_for(engine,view,2,6000));write_state(dir,"crash");CHECK(wait_for(engine,view,4,6000));
    /* The unrelated fake also watches the crash fixture; replace it for ownership checks. */
    process_close(&unrelated);write_state(dir,"present");CHECK(process_start(&unrelated,unrelated_args,NULL,NULL,true,err,sizeof(err)));
    c.profile.reconnect=true;engine_send(engine,&c);CHECK(wait_for(engine,view,2,6000));write_state(dir,"disconnected");engine_send(engine,&refresh);CHECK(wait_for(engine,view,5,6000));engine_send(engine,&stop);CHECK(wait_for(engine,view,3,5000));write_state(dir,"present");Sleep(3000);engine_snapshot(engine,view);CHECK(!view->active_serial[0]&&!view->session.desired);CHECK(process_alive(&unrelated));
    c.profile.reconnect=false;engine_send(engine,&refresh);CHECK(wait_for(engine,view,0,6000));engine_send(engine,&c);CHECK(wait_for(engine,view,2,6000));Command output=c;output.type=CMD_OUTPUT_PLAN;engine_send(engine,&output);CHECK(wait_for(engine,view,4,7000));CHECK(!view->microphone&&!view->active_serial[0]);CHECK(process_alive(&unrelated));
    Command config_command={0};config_command.type=CMD_CONFIG;awm_wcopy(config->playback_id,1024,L"FAKE missing playback");awm_wcopy(config->recording_id,1024,L"FAKE missing recording");config_command.configuration=config;engine_send(engine,&config_command);Sleep(800);c.type=CMD_MIC_SETUP;c.profile.audio=true;engine_send(engine,&c);Sleep(800);engine_snapshot(engine,view);CHECK(!view->active_serial[0]&&!view->microphone);CHECK(strstr(view->notice,"explicitly selected active")!=NULL);
    engine_destroy(engine);CHECK(process_alive(&unrelated));process_close(&unrelated);free(unrelated_args);DeleteFileW(settings);wchar_t fixture[AWM_PATH];swprintf(fixture,AWM_PATH,L"%s\\fake-state.txt",dir);DeleteFileW(fixture);DeleteFileW(adb);DeleteFileW(scrcpy);RemoveDirectoryW(dir);SecureZeroMemory(config,sizeof(*config));free(config);free(view);CoUninitialize();platform_shutdown();printf("%d simulated engine checks, %d failed\n",checks,failures);return failures?1:0;
}
