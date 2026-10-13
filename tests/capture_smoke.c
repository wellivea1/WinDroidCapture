/* Real selected-device capture probe. No recording and no receiver claim. */
#include "../src/native/manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int wmain(int argc,wchar_t **argv){
    char serial[256]="",transport[32]="",camera_id[80]="";CaptureKind kind=CAPTURE_SCREEN;int display=0,seconds=75,preset=0;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--serial")&&i+1<argc)wide_to_utf8(argv[++i],serial,256);
        else if(!wcscmp(argv[i],L"--transport")&&i+1<argc)wide_to_utf8(argv[++i],transport,32);
        else if(!wcscmp(argv[i],L"--camera")&&i+1<argc)wide_to_utf8(argv[++i],camera_id,80);
        else if(!wcscmp(argv[i],L"--display")&&i+1<argc)display=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--duration")&&i+1<argc)seconds=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--preset")&&i+1<argc)preset=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--source")&&i+1<argc){const wchar_t *s=argv[++i];if(!wcscmp(s,L"camera"))kind=CAPTURE_CAMERA;else if(wcscmp(s,L"screen"))return 2;}
        else return 2;
    }
    if(!serial[0]||!transport[0]||(kind==CAPTURE_CAMERA&&!camera_id[0])||seconds<1||seconds>600||display<0||preset<0||preset>2){puts("Explicit --serial and --transport are required; duration 1..600.");return 2;}
    platform_init();CoInitializeEx(NULL,COINIT_MULTITHREADED);wchar_t tmp[AWM_PATH],file[AWM_PATH];GetTempPathW(AWM_PATH,tmp);swprintf(file,AWM_PATH,L"%sAWM-real-probe-%lu.dpapi",tmp,GetCurrentProcessId());
    NativeSettings *settings=calloc(1,sizeof(*settings));native_settings_defaults(settings);native_settings_save(settings,file);free(settings);
    NativeManager *m=native_manager_create_at(NULL,file,NULL);NativeView *v=calloc(1,sizeof(*v));NativeCommand *c=calloc(1,sizeof(*c));bool ok=false;ULONGLONG deadline=GetTickCount64()+25000;
    while(GetTickCount64()<deadline){native_manager_snapshot(m,v);int i=find_device(&v->devices,serial,transport);if(!v->busy&&i>=0){c->device=v->devices.items[i];ok=true;break;}Sleep(50);}
    if(ok){c->action=N_PROBE;c->capture.kind=kind;native_manager_send(m,c);ok=false;deadline=GetTickCount64()+25000;
        while(GetTickCount64()<deadline){native_manager_snapshot(m,v);if(!v->busy&&v->capabilities_serial[0]&&v->capabilities_kind==kind){ok=true;break;}Sleep(50);}}
    if(ok){native_capture_default(&c->capture,kind,preset);c->capture.audio=false;c->capture.screen.display_id=display;if(kind==CAPTURE_CAMERA)awm_copy(c->capture.camera.id,80,camera_id);
        c->action=N_START;c->use_capture=true;native_manager_send(m,c);ok=false;deadline=GetTickCount64()+20000;
        while(GetTickCount64()<deadline){native_manager_snapshot(m,v);if(v->phase==N_RUNNING&&v->frames){ok=true;break;}Sleep(50);}}
    if(ok){puts("REAL source frames available; explicit Windows receiver validation is separate. No recording.");fflush(stdout);deadline=GetTickCount64()+(ULONGLONG)seconds*1000;ULONGLONG report=0;
        while(GetTickCount64()<deadline){native_manager_snapshot(m,v);if(!v->active){ok=false;break;}if(GetTickCount64()>=report){printf("SOURCE: %ux%u frames=%llu content_age_ms=%llu epoch=%u camera_requests=%llu repeated=%llu\n",v->width,v->height,(unsigned long long)v->frames,(unsigned long long)v->content_age_ms,v->video_epoch,(unsigned long long)v->camera_requests,(unsigned long long)v->repeated_samples);fflush(stdout);report=GetTickCount64()+5000;}Sleep(100);}}
    if(!ok)printf("CAPTURE FAILED: %s\n%s\n",v->notice,v->diagnostics);
    c->action=N_STOP;native_manager_send(m,c);native_manager_destroy(m);free(v);free(c);DeleteFileW(file);wchar_t backup[AWM_PATH];swprintf(backup,AWM_PATH,L"%s.pre-v4.bak",file);DeleteFileW(backup);CoUninitialize();platform_shutdown();return ok?0:1;
}
