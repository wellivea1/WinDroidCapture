#include "obs.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
static void usage(void){puts("WinDroid Capture diagnostics (no capture by default)\n  --inspect\n  --capture-test --serial EXACT_ID --camera ENUMERATED_ID --profile standard|detail|motion\n                 [--duration SECONDS] [--rotate 0|90|180|270] [--mirror] [--microphone]\n  --obs-read-only [--obs-port PORT] [--password-stdin]\nCapture tests never start OBS, join calls, monitor the cable through speakers, or record media.");}
static bool execute(const Arguments *a,const wchar_t *adb,char *out,char *err){DWORD code=0;bool ok=process_run(a,adb,NULL,NULL,15000,out,AWM_TEXT,&code,err,2048);if(!ok)return false;if(code){snprintf(err,2048,"Process exited %lu: %.1500s",code,out);return false;}return true;}
int wmain(int argc,wchar_t **argv){
    bool capture=false,obs_test=false,microphone=false,stdin_pass=false,configure=false;wchar_t playback_name[256]={0},recording_name[256]={0};char serial[256]={0},camera[80]={0};int preset=0,duration=12,rotation=0;bool mirror=false;unsigned port=0;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--inspect"))continue;else if(!wcscmp(argv[i],L"--capture-test"))capture=true;else if(!wcscmp(argv[i],L"--obs-read-only"))obs_test=true;else if(!wcscmp(argv[i],L"--configure"))configure=true;else if(!wcscmp(argv[i],L"--password-stdin"))stdin_pass=true;else if(!wcscmp(argv[i],L"--microphone"))microphone=true;else if(!wcscmp(argv[i],L"--mirror"))mirror=true;
        else if(i+1<argc&&!wcscmp(argv[i],L"--playback-name"))awm_wcopy(playback_name,256,argv[++i]);
        else if(i+1<argc&&!wcscmp(argv[i],L"--recording-name"))awm_wcopy(recording_name,256,argv[++i]);
        else if(i+1<argc&&!wcscmp(argv[i],L"--serial"))wide_to_utf8(argv[++i],serial,sizeof(serial));
        else if(i+1<argc&&!wcscmp(argv[i],L"--camera"))wide_to_utf8(argv[++i],camera,sizeof(camera));
        else if(i+1<argc&&!wcscmp(argv[i],L"--duration"))duration=_wtoi(argv[++i]);
        else if(i+1<argc&&!wcscmp(argv[i],L"--rotate"))rotation=_wtoi(argv[++i]);
        else if(i+1<argc&&!wcscmp(argv[i],L"--obs-port"))port=(unsigned)_wtoi(argv[++i]);
        else if(i+1<argc&&!wcscmp(argv[i],L"--profile")){i++;if(!wcscmp(argv[i],L"detail"))preset=1;else if(!wcscmp(argv[i],L"motion"))preset=2;else if(wcscmp(argv[i],L"standard")){usage();return 2;}}
        else{usage();return 2;}}
    if(capture&&(!serial[0]||!camera[0]||duration<1||duration>120)){usage();return 2;}
    platform_init();CoInitializeEx(NULL,COINIT_MULTITHREADED);int result=0;
    Configuration *config=calloc(1,sizeof(*config));Arguments *a=calloc(1,sizeof(*a));DeviceList *devices=calloc(1,sizeof(*devices));Capabilities *caps=calloc(1,sizeof(*caps));AudioEndpoints *endpoints=calloc(1,sizeof(*endpoints));Obs *obs=calloc(1,sizeof(*obs));
    char *out=malloc(AWM_TEXT),*help=calloc(AWM_TEXT,1),*adbhelp=calloc(AWM_TEXT,1);char err[2048]={0};if(!config||!a||!devices||!caps||!endpoints||!obs||!out||!help||!adbhelp){result=1;goto done;}
    config_defaults(config);wchar_t path[AWM_PATH];config_path(path,AWM_PATH);config_load(path,config,err,sizeof(err));detect_dependencies(config);if(port)config->obs_port=port;
    if(stdin_pass){char pass[1024];if(!fgets(pass,sizeof(pass),stdin)){result=2;goto done;}pass[strcspn(pass,"\r\n")]=0;utf8_to_wide(pass,config->obs_password,256);SecureZeroMemory(pass,sizeof(pass));}
    if(configure){
        if(!playback_name[0]||!recording_name[0]||!audio_enumerate(endpoints,err,sizeof(err))){puts("Configure requires exact playback and recording friendly names; no first-device assumption is made.");result=2;goto done;}
        int plays=0,records=0;for(int i=0;i<endpoints->count;i++){AudioEndpoint *ep=&endpoints->items[i];if(ep->state!=DEVICE_STATE_ACTIVE)continue;if(ep->flow==eRender&&!wcscmp(ep->name,playback_name)){plays++;awm_wcopy(config->playback_id,1024,ep->id);}if(ep->flow==eCapture&&!wcscmp(ep->name,recording_name)){records++;awm_wcopy(config->recording_id,1024,ep->id);}}
        if(plays!=1||records!=1){puts("Exact friendly-name selection was absent or ambiguous; setup was not saved.");result=1;goto done;}
        if(!config_save(path,config,err,sizeof(err))){puts(err);result=1;}else puts("Selected endpoint stable IDs and dependency paths saved with Windows DPAPI. Any supplied OBS password is protected in the same file. No capture or routing change was started.");goto done;
    }
    if(obs_test){
        if(!obs_connect(obs,config->obs_port,config->obs_password,NULL,err,sizeof(err))){puts(err);result=1;goto done;}
        printf("OBS connected/authenticated: %s; WebSocket %s; RPC 1\n",obs->version,obs->websocket_version);
        if(obs_preflight(obs,err,sizeof(err)))printf("Read-only preflight: idle outputs; canvas %dx%d; OBS output %dx%d at %d/%d FPS; current scene: %s\n",obs->base_width,obs->base_height,obs->output_width,obs->output_height,obs->fps_num,obs->fps_den,obs->previous_scene);
        else{puts(err);result=1;}obs_disconnect(obs);goto done;
    }
    wchar_t paths[4096];swprintf(paths,4096,L"ADB: %s\nscrcpy: %s\nOBS: %s\n",config->adb,config->scrcpy,config->obs);char paths8[12000];wide_to_utf8(paths,paths8,sizeof(paths8));puts(paths8);
    args_add(a,config->adb);args_add(a,L"version");int protocol=0;
    if(execute(a,NULL,out,err)){puts(out);int x,y;sscanf(out,"Android Debug Bridge version %d.%d.%d",&x,&y,&protocol);}else{puts(err);result=1;goto done;}
    if(!adb_server_compatible(protocol,err,sizeof(err))){puts(err);result=1;goto done;}
    a->count=1;args_add(a,L"help");execute(a,NULL,adbhelp,err);a->count=1;args_add(a,L"devices");args_add(a,L"-l");if(!execute(a,NULL,out,err)||!parse_adb_devices(out,devices)){puts(err);result=1;goto done;}
    for(int i=0;i<devices->count;i++)printf("Device row %d: %s | %s | %s | identity [redacted]\n",i+1,devices->items[i].model,transport_name(devices->items[i].transport),device_state_name(devices->items[i].state));
    a->count=0;args_add(a,config->scrcpy);args_add(a,L"--version");if(execute(a,NULL,out,err))puts(out);else{puts(err);result=1;goto done;}
    a->count=1;args_add(a,L"--help");if(!execute(a,NULL,help,err)){puts(err);result=1;goto done;}ToolFeatures features;parse_features(help,adbhelp,&features);
    if(audio_enumerate(endpoints,err,sizeof(err)))for(int i=0;i<endpoints->count;i++){char name[1024];wide_to_utf8(endpoints->items[i].name,name,sizeof(name));printf("Audio %s (%s): %s\n",endpoints->items[i].flow==eRender?"playback":"recording",endpoints->items[i].state==DEVICE_STATE_ACTIVE?"active":"unavailable",name);}else puts(err);
    char webcams[2048];if(webcam_device_names(webcams,sizeof(webcams)))printf("Windows webcam names:\n%s",webcams);
    if(!capture)goto done;
    int selected=find_device(devices,serial,"");if(selected<0||devices->items[selected].state!=DEV_AUTHORIZED){puts("Explicitly selected authorized device is unavailable; no fallback device was used.");result=1;goto done;}Device *d=&devices->items[selected];
    a->count=0;args_add(a,config->adb);args_add(a,L"-s");wchar_t id[256];utf8_to_wide(serial,id,256);args_add(a,id);args_add(a,L"shell");args_add(a,L"getprop");args_add(a,L"ro.build.version.sdk");if(execute(a,NULL,out,err))d->sdk=atoi(out);else{puts(err);result=1;goto done;}
    a->count=0;args_add(a,config->scrcpy);args_add(a,L"--serial");args_add(a,id);args_add(a,L"--video-source=camera");args_add(a,L"--list-camera-sizes");if(execute(a,config->adb,out,err))parse_capabilities(out,caps);else{puts(err);result=1;goto done;}
    a->count=3;args_add(a,L"--list-encoders");if(execute(a,config->adb,out,err))parse_capabilities(out,caps);
    Profile p;profile_default(&p,preset);awm_copy(p.camera_id,sizeof(p.camera_id),camera);p.rotation=rotation;p.mirror=mirror;p.audio=microphone;
    bool found=false;for(int i=0;i<caps->camera_count;i++)if(!strcmp(caps->cameras[i].id,camera))found=true;if(!found){puts("Requested camera ID was not enumerated on this phone.");result=1;goto done;}
    profile_declared(&p,caps,err,sizeof(err));printf("Device declaration: %s\n",err);
    if(microphone&&(!audio_endpoint_active(config->playback_id,eRender)||!audio_endpoint_active(config->recording_id,eCapture))){puts("Microphone test requires saved selected active cable endpoints in the GUI. No PC-microphone fallback is used.");result=1;goto done;}
    wchar_t title[128];GUID g;wchar_t gs[40];CoCreateGuid(&g);StringFromGUID2(&g,gs,40);swprintf(title,128,L"AWM physical test %s",gs);
    if(!build_scrcpy_args(config->scrcpy,d,&p,&features,title,microphone,a,err,sizeof(err))){puts(err);result=1;goto done;}
    Process process;if(!process_start(&process,a,config->adb,NULL,true,err,sizeof(err))){puts(err);result=1;goto done;}
    AudioMeter meter={0};if(microphone&&!audio_meter_open(&meter,config->recording_id,err,sizeof(err))){puts(err);process_close(&process);result=1;goto done;}
    size_t used=0;out[0]=0;ULONGLONG start=GetTickCount64();float peak=0;bool routed=false;int others=0;
    while(GetTickCount64()-start<(ULONGLONG)duration*1000&&process_alive(&process)){
        char chunk[4096];size_t n=process_read(&process,chunk,sizeof(chunk));if(used+n<AWM_TEXT){memcpy(out+used,chunk,n);used+=n;out[used]=0;}if(microphone){float level;if(audio_meter_tick(&meter,&level)&&level>peak)peak=level;routed=audio_process_routed(config->playback_id,process.pid,&others)||routed;}Sleep(80);
    }
    char chunk[4096];size_t n;while((n=process_read(&process,chunk,sizeof(chunk)))>0&&used+n<AWM_TEXT){memcpy(out+used,chunk,n);used+=n;out[used]=0;}
    bool alive=process_alive(&process);DWORD exit=process.exit_code;process_close(&process);audio_meter_close(&meter);redact_text(out,devices);puts(out);
    int width=0,height=0;char *texture=strstr(out,"Texture: ");if(texture)sscanf(texture+9,"%dx%d",&width,&height);int samples=0,min=10000,max=0,total=0;
    for(char *line=out;*line;){char *end=strchr(line,'\n');int fps=0;if(sscanf(line,"INFO: %d fps",&fps)==1&&fps>0){samples++;if(fps<min)min=fps;if(fps>max)max=fps;total+=fps;}if(!end)break;line=end+1;}
    printf("Physical capture process remained running at end: %s (earlier exit %lu)\nRequested: %dx%d @ %d FPS; decoded stream: %dx%d\n",alive?"yes":"no",exit,p.width,p.height,p.fps,width,height);
    if(samples)printf("Reported FPS samples: %d; min %d; max %d; arithmetic mean %.2f (scrcpy renderer counter, not receiver negotiation)\n",samples,min,max,(double)total/samples);
    if(microphone)printf("Selected playback PID observed: %s; selected recording peak %.4f; %d other active playback session(s). Phone-specific source test and receiver delivery: NOT VERIFIED by this automated capture.\n",routed?"yes":"no",peak,others);
    puts("Owned scrcpy process stopped. OBS and receiving-application tests were not performed by --capture-test.");if(!alive||width!=p.width||height!=p.height||!samples||(microphone&&!routed))result=1;
done:
    if(obs&&obs->connected)obs_disconnect(obs);if(config)SecureZeroMemory(config,sizeof(*config));free(config);free(a);free(devices);free(caps);free(endpoints);free(obs);free(out);free(help);free(adbhelp);CoUninitialize();platform_shutdown();return result;
}
