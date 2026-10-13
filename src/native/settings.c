#include "settings.h"
#include <wincrypt.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void native_settings_defaults(NativeSettings *s){
    memset(s,0,sizeof(*s));s->magic=0x324D5741;s->version=6;s->output_width=1920;s->output_height=1080;s->output_fps=30;s->video_buffer=1;
    Configuration *old=calloc(1,sizeof(*old));if(!old)return;config_defaults(old);wchar_t path[AWM_PATH];char err[1024];
    if(config_path(path,AWM_PATH))config_load(path,old,err,sizeof(err));
    detect_dependencies(old);awm_wcopy(s->adb,AWM_PATH,old->adb);awm_wcopy(s->scrcpy,AWM_PATH,old->scrcpy);s->profile_count=old->profile_count;
    memcpy(s->profiles,old->profiles,sizeof(s->profiles));SecureZeroMemory(old,sizeof(*old));free(old);
}
bool native_settings_path(wchar_t *p){if(!config_path(p,AWM_PATH))return false;PathRemoveFileSpecW(p);return PathAppendW(p,L"settings-native-v2.dpapi")!=0;}
bool native_settings_save(const NativeSettings *s,const wchar_t *path){
    DATA_BLOB in={sizeof(*s),(BYTE *)s},out={0};if(!CryptProtectData(&in,L"AWM native settings v6",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&out))return false;
    wchar_t tmp[AWM_PATH];if(swprintf(tmp,AWM_PATH,L"%s.tmp",path)<0){LocalFree(out.pbData);return false;}HANDLE f=CreateFileW(tmp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);DWORD n=0;
    bool ok=f!=INVALID_HANDLE_VALUE&&WriteFile(f,out.pbData,out.cbData,&n,NULL)&&n==out.cbData;
    if(f!=INVALID_HANDLE_VALUE){FlushFileBuffers(f);CloseHandle(f);}LocalFree(out.pbData);
    if(ok)ok=MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok)DeleteFileW(tmp);return ok;
}
static void terminate_profile(NativeCaptureProfile *p){
    p->camera.id[79]=p->camera.facing[31]=p->camera.aspect[31]=p->encoding.codec[31]=p->encoding.encoder[255]=p->audio_source[63]=0;
}
bool native_settings_load(NativeSettings *s,const wchar_t *path){
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(f==INVALID_HANDLE_VALUE)return false;
    DWORD len=GetFileSize(f,NULL),n=0;BYTE *b=len&&len<1024*1024?malloc(len):NULL;bool ok=b&&ReadFile(f,b,len,&n,NULL)&&n==len;CloseHandle(f);
    DATA_BLOB in={len,b},out={0};if(ok)ok=CryptUnprotectData(&in,NULL,NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&out)!=0;free(b);
    if(ok){
        NativeSettings *v=(void *)out.pbData;bool v2=out.cbData==offsetof(NativeSettings,preview_scaler),v3=out.cbData==offsetof(NativeSettings,output_width),v4=out.cbData==offsetof(NativeSettings,ui_theme),v5=out.cbData==AWM_SETTINGS_V5_BYTES,v6=out.cbData==sizeof(*s);
        ok=(v2||v3||v4||v5||v6)&&v->version==(v2?2u:v3?3u:v4?4u:v5?5u:6u)&&v->magic==0x324D5741&&v->profile_count>=0&&v->profile_count<=AWM_PROFILES;
        if(ok&&!v2)ok=v->preview_scaler<=2&&v->pipeline_scaler<=2;
        if(ok&&(v4||v5||v6)){
            ok=v->capture_count>=0&&v->capture_count<=AWM_PROFILES&&((v->output_width==1280&&v->output_height==720&&v->output_fps==30)||(v->output_width==1920&&v->output_height==1080&&(v->output_fps==30||v->output_fps==60))||(v->output_width==3840&&v->output_height==2160&&v->output_fps==30));
            for(int i=0;ok&&i<v->capture_count;i++)ok=(v->captures[i].last_kind==CAPTURE_CAMERA||v->captures[i].last_kind==CAPTURE_SCREEN)&&v->captures[i].camera.kind==CAPTURE_CAMERA&&v->captures[i].screen.kind==CAPTURE_SCREEN&&v->captures[i].camera.framing>=FRAME_FIT&&v->captures[i].camera.framing<=FRAME_FILL&&v->captures[i].screen.framing>=FRAME_FIT&&v->captures[i].screen.framing<=FRAME_FILL;
        }
        if(ok&&(v5||v6))ok=v->ui_theme<=2;
        if(ok&&v6)ok=v->video_buffer<=1;
        if(ok&&!v6){wchar_t backup[AWM_PATH];ok=swprintf(backup,AWM_PATH,L"%s.%s",path,v5?L"pre-v6.bak":v4?L"pre-v5.bak":L"pre-v4.bak")>0;if(ok)ok=CopyFileW(path,backup,TRUE)||GetLastError()==ERROR_FILE_EXISTS;}
        if(ok){
            memset(s,0,sizeof(*s));memcpy(s,v,out.cbData);s->version=6;s->adb[AWM_PATH-1]=s->scrcpy[AWM_PATH-1]=0;if(!v6)s->video_buffer=1;
            if(!v4&&!v5&&!v6){s->output_width=1920;s->output_height=1080;s->output_fps=30;}
            for(int i=0;i<s->profile_count;i++){s->profiles[i].serial[255]=0;Profile *p=&s->profiles[i].profile;p->camera_id[79]=p->camera_facing[31]=p->codec[31]=p->encoder[255]=p->audio_source[63]=p->camera_ar[31]=0;}
            for(int i=0;i<s->capture_count;i++){s->captures[i].serial[255]=0;terminate_profile(&s->captures[i].camera);terminate_profile(&s->captures[i].screen);}
        }
        SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);
    }return ok;
}
bool native_profile_get(const NativeSettings *s,const char *id,Profile *p){for(int i=0;i<s->profile_count;i++)if(!strcmp(s->profiles[i].serial,id)){*p=s->profiles[i].profile;return true;}return false;}
void native_profile_set(NativeSettings *s,const char *id,const Profile *p){int i;for(i=0;i<s->profile_count;i++)if(!strcmp(s->profiles[i].serial,id))break;if(i==AWM_PROFILES)return;if(i==s->profile_count)s->profile_count++;awm_copy(s->profiles[i].serial,256,id);s->profiles[i].profile=*p;}
bool native_capture_get(const NativeSettings *s,const char *id,CaptureKind kind,NativeCaptureProfile *p){
    for(int i=0;i<s->capture_count;i++)if(!strcmp(s->captures[i].serial,id)){
        const SavedCapture *v=&s->captures[i];if(kind==CAPTURE_CAMERA&&v->camera_saved){*p=v->camera;return true;}if(kind==CAPTURE_SCREEN&&v->screen_saved){*p=v->screen;return true;}
    }
    if(kind==CAPTURE_CAMERA){Profile old;if(native_profile_get(s,id,&old)){native_capture_from_legacy(p,&old);return true;}}
    native_capture_default(p,kind,0);return false;
}
void native_capture_set(NativeSettings *s,const char *id,const NativeCaptureProfile *p){
    if(!id[0])return;int i;for(i=0;i<s->capture_count;i++)if(!strcmp(s->captures[i].serial,id))break;if(i==AWM_PROFILES)return;
    if(i==s->capture_count){s->capture_count++;SavedCapture *v=&s->captures[i];memset(v,0,sizeof(*v));awm_copy(v->serial,256,id);native_capture_default(&v->camera,CAPTURE_CAMERA,0);native_capture_default(&v->screen,CAPTURE_SCREEN,0);}
    SavedCapture *v=&s->captures[i];v->last_kind=p->kind;
    if(p->kind==CAPTURE_CAMERA){v->camera=*p;v->camera_saved=true;Profile old;native_capture_to_legacy(p,&old);native_profile_set(s,id,&old);}else{v->screen=*p;v->screen_saved=true;}
}
CaptureKind native_last_kind(const NativeSettings *s,const char *id){for(int i=0;i<s->capture_count;i++)if(!strcmp(s->captures[i].serial,id))return s->captures[i].last_kind;return CAPTURE_CAMERA;}
