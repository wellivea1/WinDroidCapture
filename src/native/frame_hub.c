#include "frame_hub.h"
#include <sddl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
bool awm_user_security(SECURITY_ATTRIBUTES *a,PSECURITY_DESCRIPTOR *sd,bool fs){
    HANDLE token=NULL;DWORD bytes=0;TOKEN_USER *user=NULL;LPWSTR sid=NULL;bool ok=false;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    GetTokenInformation(token,TokenUser,NULL,0,&bytes);user=malloc(bytes);
    if(user&&GetTokenInformation(token,TokenUser,user,bytes,&bytes)&&ConvertSidToStringSidW(user->User.Sid,&sid)){
        wchar_t sddl[512];swprintf(sddl,512,L"D:P(A;;GA;;;SY)%s(A;;GA;;;%s)",fs?L"(A;;GA;;;LS)":L"",sid);
        ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,sd,NULL)!=0;
    }
    if(sid)LocalFree(sid);free(user);CloseHandle(token);
    if(ok){a->nLength=sizeof(*a);a->lpSecurityDescriptor=*sd;a->bInheritHandle=FALSE;}return ok;
}
bool awm_frame_shape(uint32_t w,uint32_t h){return w>=16&&h>=16&&w<=3840&&h<=3840&&(uint64_t)w*h<=AWM_MAX_PIXELS&&!(w&1)&&!(h&1);}
bool awm_packet_valid(const AwmPacketHeader *p){
    if(p->magic!=AWM_EXPORT_MAGIC||p->version!=AWM_EXPORT_VERSION||p->stream>1||p->size>AWM_PACKET_MAX||p->type<1||p->type>6)return false;
    if(p->type==AWM_MSG_HEARTBEAT)return p->size==0;
    if(p->type==AWM_MSG_SESSION)return p->stream==0&&p->size==0&&awm_frame_shape(p->width,p->height);
    if(p->type==AWM_MSG_OPEN)return p->stream==0?awm_frame_shape(p->width,p->height):(p->sample_rate>=8000&&p->sample_rate<=192000&&p->channels>=1&&p->channels<=8);
    return p->type!=AWM_MSG_PACKET||p->size>0;
}
bool awm_hub_open(AwmHub *h,bool create){return awm_hub_open_named(h,create,AWM_FRAME_MAPPING);}
bool awm_hub_open_named(AwmHub *h,bool create,const wchar_t *name){
    memset(h,0,sizeof(*h));SECURITY_ATTRIBUTES sa;PSECURITY_DESCRIPTOR sd=NULL;
    if(create){if(!awm_user_security(&sa,&sd,false))return false;h->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,(DWORD)sizeof(AwmFrameHub),name);DWORD err=GetLastError();LocalFree(sd);if(h->mapping&&err==ERROR_ALREADY_EXISTS){CloseHandle(h->mapping);h->mapping=NULL;SetLastError(ERROR_BUSY);return false;}}
    else h->mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name);
    if(!h->mapping)return false;h->view=MapViewOfFile(h->mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(AwmFrameHub));
    if(!h->view){CloseHandle(h->mapping);memset(h,0,sizeof(*h));return false;}
    if(create){memset(h->view,0,sizeof(*h->view));h->view->version=AWM_HUB_VERSION;h->view->generation=GetCurrentProcessId()^(uint32_t)GetTickCount64();h->view->video_epoch=1;}
    else if(h->view->magic!=AWM_FRAME_MAGIC||h->view->version!=AWM_HUB_VERSION){awm_hub_close(h);return false;}
    wchar_t event_name[256];if(wcslen(name)>240){awm_hub_close(h);SetLastError(ERROR_INVALID_NAME);return false;}swprintf(event_name,256,L"%s.Ready",name);
    if(create){if(!awm_user_security(&sa,&sd,false)){awm_hub_close(h);return false;}h->ready=CreateEventW(&sa,FALSE,FALSE,event_name);LocalFree(sd);if(!h->ready){awm_hub_close(h);return false;}}
    else h->ready=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,event_name);
    if(create)InterlockedExchange((volatile LONG *)&h->view->magic,AWM_FRAME_MAGIC);
    return true;
}
bool awm_hub_live(const AwmHub *h,uint64_t now){if(!h->view||!h->view->live||h->view->stop||now-h->view->session_heartbeat_ms>3000)return false;int i=(int)InterlockedCompareExchange(&h->view->published,0,0);return i>=0&&i<2&&(h->view->source_kind==1||now-h->view->slots[i].heartbeat_ms<=1000);}
void awm_hub_reconfigure(AwmHub *h){if(h->view){InterlockedExchange(&h->view->live,0);h->view->video_epoch++;if(h->ready)SetEvent(h->ready);}}
void awm_hub_close(AwmHub *h){if(h->ready)CloseHandle(h->ready);if(h->view)UnmapViewOfFile(h->view);if(h->mapping)CloseHandle(h->mapping);memset(h,0,sizeof(*h));}
bool awm_hub_publish(AwmHub *h,const uint8_t *p,uint32_t w,uint32_t height,int64_t pts){
    if(!h->view||!awm_frame_shape(w,height)||h->view->stop)return false;
    int slot=1-(int)InterlockedCompareExchange(&h->view->published,0,0);AwmFrameSlot *s=&h->view->slots[slot];
    InterlockedIncrement(&s->sequence);memcpy(s->pixels,p,(size_t)w*height*4);s->width=w;s->height=height;s->pts_us=pts;s->generation=h->view->generation;s->epoch=h->view->video_epoch;s->frame_number=h->view->video_frames+1;s->heartbeat_ms=GetTickCount64();MemoryBarrier();InterlockedIncrement(&s->sequence);
    InterlockedExchange(&h->view->published,slot);InterlockedExchange(&h->view->live,1);h->view->heartbeat_ms=s->heartbeat_ms;h->view->session_heartbeat_ms=s->heartbeat_ms;InterlockedIncrement64((volatile LONG64 *)&h->view->video_frames);if(h->ready)SetEvent(h->ready);return true;
}
bool awm_hub_copy_frame(AwmHub *h,uint8_t *p,size_t cap,AwmFrameInfo *info){
    if(!awm_hub_live(h,GetTickCount64()))return false;
    for(int tries=0;tries<3;tries++){
        int index=(int)InterlockedCompareExchange(&h->view->published,0,0);if(index<0||index>1)return false;AwmFrameSlot *s=&h->view->slots[index];LONG seq=InterlockedCompareExchange(&s->sequence,0,0);
        if(seq&1)continue;uint32_t sw=s->width,sh=s->height;uint64_t tick=s->heartbeat_ms;
        if(!awm_frame_shape(sw,sh)||cap<(size_t)sw*sh*4||(h->view->source_kind!=1&&GetTickCount64()-tick>1000)||s->epoch!=h->view->video_epoch)return false;
        AwmFrameInfo copied={sw,sh,s->epoch,s->frame_number,tick,s->pts_us};memcpy(p,s->pixels,(size_t)sw*sh*4);MemoryBarrier();
        if(seq==InterlockedCompareExchange(&s->sequence,0,0)&&s->epoch==h->view->video_epoch&&awm_hub_live(h,GetTickCount64())){*info=copied;return true;}
    }return false;
}
bool awm_hub_copy(AwmHub *h,uint8_t *p,size_t cap,uint32_t *w,uint32_t *height,int64_t *pts){AwmFrameInfo info;if(!awm_hub_copy_frame(h,p,cap,&info))return false;*w=info.width;*height=info.height;if(pts)*pts=info.pts_us;return true;}
void awm_hub_stop(AwmHub *h){if(h->view){InterlockedExchange(&h->view->live,0);InterlockedExchange(&h->view->microphone_live,0);h->view->microphone_peak=0;InterlockedExchange(&h->view->stop,1);if(h->ready)SetEvent(h->ready);}}
bool awm_pipe_io(HANDLE pipe,void *data,DWORD bytes,bool write,HANDLE cancel,DWORD timeout){
    BYTE *p=data;ULONGLONG deadline=GetTickCount64()+timeout;
    while(bytes){OVERLAPPED o={0};o.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!o.hEvent)return false;DWORD n=0;BOOL ok=write?WriteFile(pipe,p,bytes,&n,&o):ReadFile(pipe,p,bytes,&n,&o);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){HANDLE waits[2]={o.hEvent,cancel};ULONGLONG now=GetTickCount64();DWORD wait=WaitForMultipleObjects(cancel?2:1,waits,FALSE,(DWORD)(deadline>now?deadline-now:0));
            if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(pipe,&o,&n,FALSE);else{CancelIoEx(pipe,&o);GetOverlappedResult(pipe,&o,&n,TRUE);ok=FALSE;SetLastError(wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_CANCELLED);}}
        CloseHandle(o.hEvent);if(!ok||!n)return false;p+=n;bytes-=n;
    }return true;
}
void awm_scale_bgra(const uint8_t *s,uint32_t sw,uint32_t sh,uint8_t *d,uint32_t dw,uint32_t dh){
    if(sw==dw&&sh==dh){memcpy(d,s,(size_t)dw*dh*4);return;}
    memset(d,0,(size_t)dw*dh*4);uint32_t rw=dw,rh=(uint32_t)((uint64_t)sh*dw/sw);if(rh>dh){rh=dh;rw=(uint32_t)((uint64_t)sw*dh/sh);}
    if(!rw||!rh)return;uint32_t ox=(dw-rw)/2,oy=(dh-rh)/2;
    for(uint32_t y=0;y<rh;y++){uint32_t sy=(uint32_t)((uint64_t)y*sh/rh);for(uint32_t x=0;x<rw;x++){uint32_t sx=(uint32_t)((uint64_t)x*sw/rw);memcpy(d+((size_t)(y+oy)*dw+x+ox)*4,s+((size_t)sy*sw+sx)*4,4);}}
}
static uint8_t clamp(int x){return (uint8_t)(x<0?0:x>255?255:x);}
void awm_bgra_to_nv12(const uint8_t *p,uint32_t w,uint32_t h,uint8_t *d){
    uint8_t *uv=d+(size_t)w*h;
    for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){const uint8_t *q=p+((size_t)y*w+x)*4;d[(size_t)y*w+x]=clamp(((47*q[2]+157*q[1]+16*q[0]+128)>>8)+16);}
    for(uint32_t y=0;y<h;y+=2)for(uint32_t x=0;x<w;x+=2){int b=0,g=0,r=0;for(uint32_t j=0;j<2;j++)for(uint32_t i=0;i<2;i++){const uint8_t *q=p+((size_t)(y+j)*w+x+i)*4;b+=q[0];g+=q[1];r+=q[2];}
        b/=4;g/=4;r/=4;uv[(size_t)(y/2)*w+x]=clamp(((-26*r-87*g+112*b+128)>>8)+128);uv[(size_t)(y/2)*w+x+1]=clamp(((112*r-102*g-10*b+128)>>8)+128);}
}
