#include "command.h"
#include <stdio.h>
#include <windows.h>
bool native_capture_args_profile(const wchar_t *exe,const wchar_t *pipe,const Device *d,const NativeCaptureProfile *p,const ToolFeatures *f,Arguments *a,char *err,size_t cap){
    memset(a,0,sizeof(*a));if(!native_capture_validate(p,d,f,err,cap))return false;
    static const wchar_t prefix[]=L"\\\\.\\pipe\\AWM.Export.";
    if(wcsncmp(pipe,prefix,ARRAYSIZE(prefix)-1)||wcslen(pipe)>200){awm_copy(err,cap,"Invalid owned export pipe.");return false;}
    wchar_t serial[256],b[512],value[256];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,d->serial,-1,serial,256)){awm_copy(err,cap,"Invalid device identity encoding.");return false;}
    bool ok=args_add(a,exe)&&args_add(a,L"--serial")&&args_add(a,serial)&&args_add(a,p->kind==CAPTURE_CAMERA?L"--video-source=camera":L"--video-source=display")&&args_add(a,L"--no-control")&&args_add(a,L"--no-downsize-on-error")&&args_add(a,L"--no-window")&&args_add(a,L"--no-playback")&&args_add(a,L"--record-format=mkv");
#define ADD(...) do{swprintf(b,512,__VA_ARGS__);ok=args_add(a,b)&&ok;}while(0)
    if(p->kind==CAPTURE_CAMERA){
        MultiByteToWideChar(CP_UTF8,0,p->camera.id,-1,value,256);ADD(L"--camera-id=%s",value);ADD(L"--camera-size=%dx%d",p->camera.width,p->camera.height);ADD(L"--camera-fps=%d",p->camera.fps);if(f->zoom)ADD(L"--camera-zoom=%.3f",p->camera.zoom);
    }else{
        ADD(L"--display-id=%d",p->screen.display_id);if(p->screen.max_size)ADD(L"--max-size=%d",p->screen.max_size);ADD(L"--max-fps=%d",p->screen.fps_limit);if(p->screen.lock_orientation)ok=args_add(a,L"--capture-orientation=@")&&ok;
    }
    MultiByteToWideChar(CP_UTF8,0,p->encoding.codec,-1,value,256);ADD(L"--video-codec=%s",value);ADD(L"--video-bit-rate=%dM",p->encoding.bitrate_mbps);ADD(L"--record=awm-pipe:%s",pipe);
    if(p->encoding.encoder[0]){MultiByteToWideChar(CP_UTF8,0,p->encoding.encoder,-1,value,256);ADD(L"--video-encoder=%s",value);}
    if(p->audio){MultiByteToWideChar(CP_UTF8,0,p->audio_source,-1,value,256);ADD(L"--audio-source=%s",value);ok=args_add(a,L"--audio-codec=raw")&&args_add(a,L"--require-audio")&&ok;}else ok=args_add(a,L"--no-audio")&&ok;
#undef ADD
    if(!ok)awm_copy(err,cap,"Command exceeds argument bounds.");return ok;
}
bool native_capture_args(const wchar_t *exe,const wchar_t *pipe,const Device *d,const Profile *v,const ToolFeatures *f,Arguments *a,char *err,size_t cap){NativeCaptureProfile p;native_capture_from_legacy(&p,v);return native_capture_args_profile(exe,pipe,d,&p,f,a,err,cap);}
