#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
void awm_copy(char *d, size_t n, const char *s) { if(n) { size_t l=strlen(s); if(l>=n) l=n-1; memcpy(d,s,l); d[l]=0; } }
void awm_wcopy(wchar_t *d, size_t n, const wchar_t *s) { if(n) { size_t l=wcslen(s); if(l>=n) l=n-1; memcpy(d,s,l*sizeof(*d)); d[l]=0; } }
const char *device_state_name(DeviceState s) {
    static const char *names[]={"Unknown","Authorized","Unauthorized","Offline","Disconnected","Unsupported"};
    return s>=DEV_UNKNOWN && s<=DEV_UNSUPPORTED?names[s]:"Unknown";
}
const char *transport_name(Transport t) {
    static const char *names[]={"Unknown","USB","Wireless","Emulator"};
    return t>=TRANSPORT_UNKNOWN && t<=TRANSPORT_EMULATOR?names[t]:"Unknown";
}
static void token_value(const char *line,const char *key,char *out,size_t cap) {
    const char *p=strstr(line,key); if(!p) return; p+=strlen(key);
    size_t n=0; while(p[n]&&!isspace((unsigned char)p[n])) n++;
    if(n>=cap)n=cap-1; memcpy(out,p,n); out[n]=0;
}
bool parse_adb_devices(const char *text, DeviceList *out) {
    memset(out,0,sizeof(*out)); bool header=false;
    for(const char *p=text; *p;) {
        const char *e=strchr(p,'\n'); size_t len=e?(size_t)(e-p):strlen(p); char line[2048];
        if(len>=sizeof(line)) return false;
        memcpy(line,p,len);line[len]=0;
        if(strstr(line,"List of devices attached")) header=true;
        else if(header && len && !isspace((unsigned char)line[0]) && line[0]!='*') {
            Device d={0}; char state[80]={0};
            if(sscanf(line,"%255s %79s",d.serial,state)==2) {
                if(out->count==AWM_DEVICES){out->truncated=true;break;}
                awm_copy(d.raw_state,sizeof(d.raw_state),state);
                if(!strcmp(state,"device")) d.state=DEV_AUTHORIZED;
                else if(!strcmp(state,"unauthorized"))d.state=DEV_UNAUTHORIZED;
                else if(!strcmp(state,"offline"))d.state=DEV_OFFLINE;
                else if(!strcmp(state,"no")||!strcmp(state,"recovery")||!strcmp(state,"sideload")||!strcmp(state,"bootloader"))d.state=DEV_UNSUPPORTED;
                else d.state=DEV_UNKNOWN;
                token_value(line,"model:",d.model,sizeof(d.model));
                token_value(line,"transport_id:",d.transport_id,sizeof(d.transport_id));
                if(!d.model[0])awm_copy(d.model,sizeof(d.model),"Android device");
                for(char *m=d.model;*m;m++)if(*m=='_')*m=' ';
                if(!strncmp(d.serial,"emulator-",9))d.transport=TRANSPORT_EMULATOR;
                else if(strchr(d.serial,':')||strstr(d.serial,"._adb-tls-")||strstr(d.serial,".local"))d.transport=TRANSPORT_WIRELESS;
                else if(strstr(line,"usb:")||d.state==DEV_AUTHORIZED)d.transport=TRANSPORT_USB;
                out->items[out->count++]=d;
            }
        }
        if(!e)break;p=e+1;
    }
    return header;
}
int find_device(const DeviceList *l,const char *serial,const char *tid) {
    for(int i=0;i<l->count;i++) if(!strcmp(l->items[i].serial,serial)&&(!tid[0]||!strcmp(l->items[i].transport_id,tid)))return i;
    return -1;
}
void merge_devices(DeviceList *current,const DeviceList *snapshot) {
    DeviceList merged=*snapshot;
    for(int i=0;i<current->count;i++) {
        Device d=current->items[i];int index=find_device(&merged,d.serial,"");
        if(index>=0) { if(!merged.items[index].android[0])awm_copy(merged.items[index].android,sizeof(d.android),d.android);if(!merged.items[index].sdk)merged.items[index].sdk=d.sdk; }
        else if(merged.count<AWM_DEVICES){d.state=DEV_DISCONNECTED;awm_copy(d.raw_state,sizeof(d.raw_state),"disconnected");merged.items[merged.count++]=d;}
    }
    *current=merged;
}
static int parse_fps(const char *p,int *out) {
    int n=0;if(!p)return 0;p=strchr(p,'{');if(!p)return 0;p++;
    while(*p&&*p!='}'&&n<32){while(*p==' '||*p==',')p++; char *end;long v=strtol(p,&end,10);if(end==p)break;if(v>0&&v<1000)out[n++]=(int)v;p=end;}
    return n;
}
void parse_capabilities(const char *text,Capabilities *out) {
    if(strstr(text,"[AWM camera API 1]"))out->camera_api=1;
    int current=-1;bool highspeed=false;
    for(const char *p=text;*p;) {
        const char *end=strchr(p,'\n');size_t len=end?(size_t)(end-p):strlen(p);char line[2048];
        if(len>=sizeof(line))len=sizeof(line)-1;memcpy(line,p,len);line[len]=0;
        char *id=strstr(line,"--camera-id=");
        if(id) {
            char cid[80]={0};token_value(line,"--camera-id=",cid,sizeof(cid));int index=-1;
            for(int i=0;i<out->camera_count;i++)if(!strcmp(out->cameras[i].id,cid))index=i;
            bool physical=!strncmp(cid,"awm-physical:",13);char parent[80]={0},member[80]={0};
            if(physical){char *separator=strchr(cid+13,':');if(out->camera_api!=1||!separator||separator==cid+13||!separator[1]||strchr(separator+1,':')){current=-1;if(!end)break;p=end+1;continue;}size_t bytes=(size_t)(separator-cid-13);memcpy(parent,cid+13,bytes);awm_copy(member,sizeof(member),separator+1);}
            if(index<0&&out->camera_count<AWM_CAMERAS){index=out->camera_count++;memset(&out->cameras[index],0,sizeof(Camera));}else if(index<0)out->camera_truncated=true;
            current=index;highspeed=false;
            if(index>=0) {
                Camera *c=&out->cameras[index];awm_copy(c->id,sizeof(c->id),cid);char *q=strchr(id,'(');
                if(q)sscanf(q+1,"%31[^,], %dx%d",c->facing,&c->width,&c->height);
                c->fps_count=parse_fps(strstr(line,"fps="),c->fps);
                q=strstr(line,"zoom-range=[");if(q)sscanf(q+12,"%f, %f",&c->zoom_min,&c->zoom_max);
                c->physical=physical;if(physical){awm_copy(c->logical_id,sizeof(c->logical_id),parent);awm_copy(c->physical_id,sizeof(c->physical_id),member);}
                q=strstr(line,"focal-mm=");if(q)sscanf(q+9,"%f",&c->focal_mm);
                q=strstr(line,"sensor-width-mm=");if(q)sscanf(q+16,"%f",&c->sensor_width_mm);
            }
        } else if(strstr(line,"High speed capture"))highspeed=true;
        else if(current>=0&&!highspeed) {
            int w,h;if(sscanf(line," - %dx%d",&w,&h)==2&&w>0&&h>0&&w<=32768&&h<=32768){
                Camera *c=&out->cameras[current];bool seen=false;
                for(int i=0;i<c->size_count;i++)if(c->sizes[i].width==w&&c->sizes[i].height==h)seen=true;
                if(!seen&&c->size_count<AWM_SIZES)c->sizes[c->size_count++]=(CameraSize){w,h};
            }
        }
        if(strstr(line,"--video-encoder=")) {
            Encoder enc={0};token_value(line,"--video-encoder=",enc.name,sizeof(enc.name));token_value(line,"--video-codec=",enc.codec,sizeof(enc.codec));
            enc.hardware=strstr(line,"(hw)")!=NULL;bool seen=false;
            for(int i=0;i<out->encoder_count;i++)if(!strcmp(out->encoders[i].name,enc.name))seen=true;
            if(!seen&&out->encoder_count<AWM_ENCODERS)out->encoders[out->encoder_count++]=enc;
        }
        if(!end)break;p=end+1;
    }
}
void parse_features(const char *s,const char *a,ToolFeatures *f) {
    memset(f,0,sizeof(*f));
    f->camera=strstr(s,"--video-source=")&&strstr(s,"--camera-id=");
    f->list_cameras=strstr(s,"--list-cameras")!=NULL;f->list_sizes=strstr(s,"--list-camera-sizes")!=NULL;
    f->list_encoders=strstr(s,"--list-encoders")!=NULL;f->camera_fps=strstr(s,"--camera-fps=")!=NULL;
    f->require_audio=strstr(s,"--require-audio")!=NULL;f->no_downsize=strstr(s,"--no-downsize-on-error")!=NULL;
    f->display_orientation=strstr(s,"--display-orientation=")!=NULL;
    f->window_size=strstr(s,"--window-width=")&&strstr(s,"--window-height=");
    f->zoom=strstr(s,"--camera-zoom=")!=NULL;f->mic_sources=strstr(s,"mic-unprocessed")!=NULL;f->hwdec=strstr(s,"--hwdec=")!=NULL;
    f->adb_pair=strstr(a,"pair HOST")!=NULL;f->adb_mdns=strstr(a,"mdns services")!=NULL;
    f->list_displays=strstr(s,"--list-displays")!=NULL;f->max_fps=strstr(s,"--max-fps=")!=NULL;f->capture_orientation=strstr(s,"--capture-orientation=")!=NULL;
}
void profile_default(Profile *p,int preset) {
    memset(p,0,sizeof(*p));p->width=preset==1?3840:1920;p->height=preset==1?2160:1080;
    p->fps=preset==2?60:30;p->bitrate_mbps=preset==1?40:preset==2?24:16;
    p->audio=true;p->zoom=1.0f;awm_copy(p->codec,sizeof(p->codec),preset==1?"h265":"h264");
    awm_copy(p->audio_source,sizeof(p->audio_source),"mic");
}
static bool reject(char *out,size_t cap,const char *why){awm_copy(out,cap,why);return false;}
bool validate_profile(const Profile *p,const ToolFeatures *f,char *err,size_t cap) {
    if(!f->camera||!f->camera_fps||!f->display_orientation||!f->no_downsize||!f->window_size)return reject(err,cap,"scrcpy lacks required camera/FPS/orientation/window/no-downsize options. Select a supported version.");
    if(!p->camera_id[0])return reject(err,cap,"Probe capabilities and select an enumerated camera.");
    if(p->camera_facing[0])return reject(err,cap,"Camera ID and facing selection are mutually exclusive.");
    if(p->max_size||p->camera_ar[0])return reject(err,cap,"Explicit dimensions conflict with max-size or camera aspect ratio.");
    if(p->width<16||p->height<16||p->width>16384||p->height>16384||p->fps<1||p->fps>240||p->bitrate_mbps<1||p->bitrate_mbps>200)return reject(err,cap,"Invalid dimensions (16..16384), FPS (1..240), or bitrate (1..200 Mbps).");
    if(strcmp(p->codec,"h264")&&strcmp(p->codec,"h265")&&strcmp(p->codec,"av1"))return reject(err,cap,"Select H.264, H.265, or AV1.");
    if(p->rotation!=0&&p->rotation!=90&&p->rotation!=180&&p->rotation!=270)return reject(err,cap,"Rotation must be 0, 90, 180, or 270 degrees.");
    if(p->audio&&!f->require_audio)return reject(err,cap,"This scrcpy cannot require microphone capture. Update it for microphone mode.");
    if(strcmp(p->audio_source,"mic")&&(!f->mic_sources||(strcmp(p->audio_source,"mic-unprocessed")&&strcmp(p->audio_source,"mic-voice-communication")&&strcmp(p->audio_source,"mic-voice-recognition"))))return reject(err,cap,"Unsupported microphone source; phone output audio is not a microphone.");
    if(!(p->zoom>0.0f&&p->zoom<=100.0f))return reject(err,cap,"Zoom must be a finite positive value.");
    if(p->zoom!=1.0f&&!f->zoom)return reject(err,cap,"Installed scrcpy does not support camera zoom.");
    return true;
}
bool profile_declared(const Profile *p,const Capabilities *c,char *note,size_t cap) {
    const Camera *cam=NULL;for(int i=0;i<c->camera_count;i++)if(!strcmp(c->cameras[i].id,p->camera_id))cam=&c->cameras[i];
    if(!cam)return reject(note,cap,"Selected camera is not in the device enumeration.");
    if(cam->physical&&(c->camera_api!=1||p->zoom!=1))return reject(note,cap,"Direct physical-camera capture requires the AWM camera server and fixed zoom 1; no automatic-lens fallback.");
    if(cam->zoom_min>0&&(p->zoom<cam->zoom_min||p->zoom>cam->zoom_max))return reject(note,cap,"Zoom is outside the camera's reported range.");
    if(p->encoder[0]){bool enc=false;for(int i=0;i<c->encoder_count;i++)if(!strcmp(c->encoders[i].name,p->encoder)&&!strcmp(c->encoders[i].codec,p->codec))enc=true;if(!enc)return reject(note,cap,"Encoder is not enumerated for the selected codec.");}
    bool size=false,fps=false;
    for(int i=0;i<cam->size_count;i++)if(cam->sizes[i].width==p->width&&cam->sizes[i].height==p->height)size=true;
    for(int i=0;i<cam->fps_count;i++)if(cam->fps[i]==p->fps)fps=true;
    if(!size||!fps)return reject(note,cap,"Requested size/FPS is not declared. Edit settings or explicitly try the mode; startup remains the real validation.");
    awm_copy(note,cap,"Size and FPS are declared separately; their combination is unverified until capture starts.");return true;
}
bool args_add(Arguments *a,const wchar_t *v){if(a->count>=AWM_ARGS||wcslen(v)>=AWM_PATH)return false;awm_wcopy(a->values[a->count++],AWM_PATH,v);return true;}
static bool arg_utf8(Arguments *a,const char *s){wchar_t w[AWM_PATH];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,w,AWM_PATH))return false;return args_add(a,w);}
bool build_scrcpy_args(const wchar_t *exe,const Device *d,const Profile *p,const ToolFeatures *f,const wchar_t *title,bool mic,Arguments *a,char *err,size_t cap) {
    memset(a,0,sizeof(*a));if(d->state!=DEV_AUTHORIZED||d->transport==TRANSPORT_EMULATOR||d->sdk<31)return reject(err,cap,"Select an authorized physical Android 12+ device.");
    Profile effective=*p;effective.audio=mic;if(!validate_profile(&effective,f,err,cap))return false;
    bool ok=args_add(a,exe)&&args_add(a,L"--serial")&&arg_utf8(a,d->serial)&&args_add(a,L"--video-source=camera")&&args_add(a,L"--no-control")&&args_add(a,L"--no-downsize-on-error")&&args_add(a,L"--window-borderless")&&args_add(a,L"--window-title")&&args_add(a,title)&&args_add(a,L"--print-fps")&&args_add(a,L"--verbosity=debug");
    if(f->hwdec)ok=args_add(a,L"--hwdec=disabled")&&ok;
    char b[512];
#define ADD_FMT(...) do {snprintf(b,sizeof(b),__VA_ARGS__);ok=arg_utf8(a,b)&&ok;} while(0)
    ADD_FMT("--camera-id=%s",p->camera_id);ADD_FMT("--camera-size=%dx%d",p->width,p->height);
    ADD_FMT("--camera-fps=%d",p->fps);ADD_FMT("--video-codec=%s",p->codec);ADD_FMT("--video-bit-rate=%dM",p->bitrate_mbps);
    ADD_FMT("--display-orientation=%s%d",p->mirror?"flip":"",p->rotation);
    if(p->encoder[0])ADD_FMT("--video-encoder=%s",p->encoder);
    if(f->zoom)ADD_FMT("--camera-zoom=%.3f",p->zoom);
    int w=p->width,h=p->height;if(p->rotation==90||p->rotation==270){w=p->height;h=p->width;}
    ADD_FMT("--window-width=%d",w);ADD_FMT("--window-height=%d",h);
    if(mic){ADD_FMT("--audio-source=%s",p->audio_source);ok=args_add(a,L"--require-audio")&&ok;}else ok=args_add(a,L"--no-audio")&&ok;
#undef ADD_FMT
    if(!ok)return reject(err,cap,"Command exceeds safe argument limits.");return true;
}
bool quote_windows_args(const Arguments *a,wchar_t *out,size_t cap){
    size_t n=0;
#define PUT(ch) do{if(n+1>=cap)return false;out[n++]=(ch);}while(0)
    for(int i=0;i<a->count;i++){
        if(i)PUT(L' ');PUT(L'"');const wchar_t *p=a->values[i];
        while(*p){size_t slashes=0;while(*p==L'\\'){slashes++;p++;}
            if(*p==L'"'){for(size_t j=0;j<2*slashes+1;j++)PUT(L'\\');PUT(*p++);}
            else if(!*p){for(size_t j=0;j<2*slashes;j++)PUT(L'\\');break;}
            else{for(size_t j=0;j<slashes;j++)PUT(L'\\');PUT(*p++);}
        }PUT(L'"');
    }
    if(n>=cap)return false;out[n]=0;return true;
#undef PUT
}
bool valid_wireless_address(const wchar_t *a){
    if(!a||wcslen(a)>300||wcslen(a)<3)return false;
    const wchar_t *colon=wcsrchr(a,L':');if(!colon||!colon[1])return false;
    unsigned long port=0;for(const wchar_t *p=colon+1;*p;p++){if(*p<L'0'||*p>L'9')return false;port=port*10+(unsigned)(*p-L'0');if(port>65535)return false;}if(!port)return false;
    size_t len=(size_t)(colon-a);if(!len||len>255)return false;
    if(a[0]==L'['){if(len<4||a[len-1]!=L']')return false;wchar_t host[256];memcpy(host,a+1,(len-2)*sizeof(wchar_t));host[len-2]=0;IN6_ADDR ip;return InetPtonW(AF_INET6,host,&ip)==1;}
    bool numeric=true;for(size_t i=0;i<len;i++)if((a[i]<L'0'||a[i]>L'9')&&a[i]!=L'.')numeric=false;
    if(numeric){wchar_t host[256];memcpy(host,a,len*sizeof(wchar_t));host[len]=0;IN_ADDR ip;return InetPtonW(AF_INET,host,&ip)==1;}
    size_t label=0;for(size_t i=0;i<len;i++){wchar_t c=a[i];if(c==L'.'){if(!label||a[i-1]==L'-')return false;label=0;}else{if(!((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'-'))return false;if(!label&&c==L'-')return false;if(++label>63)return false;}}
    return label>0&&a[len-1]!=L'-';
}
bool valid_pairing_code(const wchar_t *c){if(wcslen(c)!=6)return false;for(int i=0;i<6;i++)if(c[i]<L'0'||c[i]>L'9')return false;return true;}
uint32_t session_start(SessionState *s){s->generation++;s->desired=true;s->phase=SESSION_STARTING;s->retries=0;return s->generation;}
void session_stop(SessionState *s){s->generation++;s->desired=false;s->retries=0;s->phase=SESSION_IDLE;}
bool session_complete(SessionState *s,uint32_t g,bool output){if(g!=s->generation||!s->desired)return false;s->phase=output?SESSION_OUTPUT:SESSION_PREVIEW;return true;}
bool session_failure(SessionState *s,uint32_t g,bool reconnect){if(g!=s->generation||!s->desired)return false;if(reconnect&&s->retries<3){s->phase=SESSION_RETRY;s->retries++;}else{s->phase=SESSION_FAILED;s->desired=false;}return true;}
void redact_text(char *s,const DeviceList *l){for(int i=0;i<l->count;i++){const char *id=l->items[i].serial;size_t n=strlen(id);if(!n)continue;char *p;while((p=strstr(s,id))!=NULL)memset(p,'*',n);}}
