#include "capture_profile.h"
#include "frame_hub.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static bool invalid(char *err,size_t cap,const char *s){awm_copy(err,cap,s);return false;}
const char *native_capture_name(CaptureKind kind){return kind==CAPTURE_SCREEN?"Phone screen":"Phone camera";}
void native_capture_from_legacy(NativeCaptureProfile *p,const Profile *v){
    memset(p,0,sizeof(*p));p->kind=CAPTURE_CAMERA;awm_copy(p->camera.id,80,v->camera_id);awm_copy(p->camera.facing,32,v->camera_facing);awm_copy(p->camera.aspect,32,v->camera_ar);
    p->camera.width=v->width;p->camera.height=v->height;p->camera.fps=v->fps;p->camera.max_size=v->max_size;p->camera.zoom=v->zoom;
    awm_copy(p->encoding.codec,32,v->codec);awm_copy(p->encoding.encoder,256,v->encoder);p->encoding.bitrate_mbps=v->bitrate_mbps;
    awm_copy(p->audio_source,64,v->audio_source);p->rotation=v->rotation;p->mirror=v->mirror;p->audio=v->audio;p->reconnect=v->reconnect;
}
void native_capture_to_legacy(const NativeCaptureProfile *p,Profile *v){
    memset(v,0,sizeof(*v));awm_copy(v->camera_id,80,p->camera.id);awm_copy(v->camera_facing,32,p->camera.facing);awm_copy(v->camera_ar,32,p->camera.aspect);
    v->width=p->camera.width;v->height=p->camera.height;v->fps=p->camera.fps;v->max_size=p->camera.max_size;v->zoom=p->camera.zoom;
    awm_copy(v->codec,32,p->encoding.codec);awm_copy(v->encoder,256,p->encoding.encoder);v->bitrate_mbps=p->encoding.bitrate_mbps;
    awm_copy(v->audio_source,64,p->audio_source);v->rotation=p->rotation;v->mirror=p->mirror;v->audio=p->audio;v->reconnect=p->reconnect;
}
void native_capture_default(NativeCaptureProfile *p,CaptureKind kind,int preset){
    Profile v;profile_default(&v,preset);native_capture_from_legacy(p,&v);p->kind=kind;
    if(kind==CAPTURE_SCREEN){p->screen.display_id=-1;p->screen.max_size=preset==1?0:1920;p->screen.fps_limit=preset==2?60:30;
        p->encoding.bitrate_mbps=preset==1?24:preset==2?24:16;p->audio=false;}
}
bool native_capture_validate(const NativeCaptureProfile *p,const Device *d,const ToolFeatures *f,char *err,size_t cap){
    if(d->state!=DEV_AUTHORIZED)return invalid(err,cap,"Select an authorized ADB transport.");
    if(p->kind!=CAPTURE_CAMERA&&p->kind!=CAPTURE_SCREEN)return invalid(err,cap,"Unknown video source.");
    if(!f->no_downsize)return invalid(err,cap,"The selected scrcpy lacks no-downsize-on-error.");
    if(p->encoding.bitrate_mbps<1||p->encoding.bitrate_mbps>200)return invalid(err,cap,"Bitrate must be 1..200 Mbps.");
    if(strcmp(p->encoding.codec,"h264")&&strcmp(p->encoding.codec,"h265")&&strcmp(p->encoding.codec,"av1"))return invalid(err,cap,"Select H.264, H.265 or AV1.");
    if(p->rotation!=0&&p->rotation!=90&&p->rotation!=180&&p->rotation!=270)return invalid(err,cap,"Rotation must be 0, 90, 180 or 270 degrees.");
    if(p->framing!=FRAME_FIT&&p->framing!=FRAME_FILL)return invalid(err,cap,"Unknown output framing.");
    if(p->audio&&!f->require_audio)return invalid(err,cap,"The selected scrcpy cannot require microphone capture.");
    if(strcmp(p->audio_source,"mic")&&(!f->mic_sources||(strcmp(p->audio_source,"mic-unprocessed")&&strcmp(p->audio_source,"mic-voice-communication")&&strcmp(p->audio_source,"mic-voice-recognition"))))return invalid(err,cap,"Use an explicit supported phone microphone source.");
    if(p->kind==CAPTURE_CAMERA){
        if(d->sdk<31||d->transport==TRANSPORT_EMULATOR)return invalid(err,cap,"Camera mode requires an authorized physical Android 12+ phone.");
        if(!f->camera||!f->camera_fps)return invalid(err,cap,"Camera mode/FPS options are unavailable.");
        if(!p->camera.id[0])return invalid(err,cap,"Select an enumerated camera.");
        if(p->camera.facing[0]||p->camera.max_size||p->camera.aspect[0])return invalid(err,cap,"Camera ID/explicit dimensions conflict with facing, automatic size or aspect selection.");
        if(!awm_frame_shape(p->camera.width,p->camera.height)||p->camera.fps<1||p->camera.fps>240)return invalid(err,cap,"Camera shape exceeds the host bounds or camera FPS is invalid.");
        if(!(p->camera.zoom>0&&p->camera.zoom<=100)||(p->camera.zoom!=1&&!f->zoom))return invalid(err,cap,"Camera zoom is invalid or unsupported.");
    }else{
        if(d->sdk&&d->sdk<21)return invalid(err,cap,"Screen mode requires Android 5 or newer.");
        if(!f->list_displays||!f->max_fps||(p->screen.lock_orientation&&!f->capture_orientation))return invalid(err,cap,"Screen enumeration/FPS/orientation options are unavailable.");
        if(p->screen.display_id<0||p->screen.display_id>65535)return invalid(err,cap,"Select an enumerated display.");
        if((p->screen.max_size&& (p->screen.max_size<16||p->screen.max_size>3840))||p->screen.fps_limit<1||p->screen.fps_limit>240)return invalid(err,cap,"Screen long-edge limit must be Native or 16..3840; FPS cap must be 1..240.");
    }return true;
}
bool native_capture_declared(const NativeCaptureProfile *p,const Capabilities *c,const DisplayList *ds,char *err,size_t cap){
    if(p->kind==CAPTURE_CAMERA){Profile v;native_capture_to_legacy(p,&v);return profile_declared(&v,c,err,cap);}
    const PhoneDisplay *display=NULL;for(int i=0;i<ds->count;i++)if(ds->items[i].id==p->screen.display_id)display=&ds->items[i];
    if(!display)return invalid(err,cap,"Selected display is not in this transport's enumeration.");
    if(!p->screen.max_size&&display->size_known&&!awm_frame_shape(display->width,display->height))return invalid(err,cap,"Native display size exceeds the host bounds. Select an explicit long-edge limit.");
    if(p->encoding.encoder[0]){bool found=false;for(int i=0;i<c->encoder_count;i++)if(!strcmp(c->encoders[i].codec,p->encoding.codec)&&!strcmp(c->encoders[i].name,p->encoding.encoder))found=true;if(!found)return invalid(err,cap,"Encoder is not enumerated for this codec.");}
    awm_copy(err,cap,"Display declared; startup validates actual size and encoder.");return true;
}
bool native_parse_displays(const char *text,DisplayList *out){
    memset(out,0,sizeof(*out));if(strstr(text,"List of displays")||strstr(text,"list of displays")||strstr(text,"No display"))out->recognized=true;
    const char *s=text;while((s=strstr(s,"--display-id="))){s+=13;char *end;long id=strtol(s,&end,10);if(end==s||id<0||id>65535)continue;
        out->recognized=true;bool duplicate=false;for(int i=0;i<out->count;i++)if(out->items[i].id==id)duplicate=true;if(duplicate)continue;
        if(out->count==AWM_DISPLAYS){out->truncated=true;continue;}PhoneDisplay *d=&out->items[out->count++];d->id=(int)id;
        int w=0,h=0;if(sscanf(end," (%dx%d)",&w,&h)==2&&w>0&&h>0){d->size_known=true;d->width=w;d->height=h;}
    }return out->recognized;
}
