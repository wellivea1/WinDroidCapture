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
    wchar_t event_name[256],stop_name[256];if(wcslen(name)>240){awm_hub_close(h);SetLastError(ERROR_INVALID_NAME);return false;}swprintf(event_name,256,L"%s.Ready",name);swprintf(stop_name,256,L"%s.Stop",name);
    if(create){if(!awm_user_security(&sa,&sd,false)){awm_hub_close(h);return false;}h->ready=CreateEventW(&sa,FALSE,FALSE,event_name);h->stop_event=CreateEventW(&sa,TRUE,FALSE,stop_name);LocalFree(sd);if(!h->ready||!h->stop_event){awm_hub_close(h);return false;}}
    else{h->ready=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,event_name);h->stop_event=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,stop_name);}
    if(create)InterlockedExchange((volatile LONG *)&h->view->magic,AWM_FRAME_MAGIC);
    return true;
}
static uint64_t age_ms(uint64_t now,uint64_t then){return now>then?now-then:0;}
bool awm_hub_live(const AwmHub *h,uint64_t now){
    if(!h->view||!h->view->live||h->view->stop)return false;
    uint64_t session_hb=*(volatile const uint64_t *)&h->view->session_heartbeat_ms;
    if(!session_hb||age_ms(now,session_hb)>3000)return false;
    int i=(int)InterlockedCompareExchange(&h->view->published,0,0);
    if(i<0||i>=2)return false;
    uint64_t slot_hb=*(volatile const uint64_t *)&h->view->slots[i].heartbeat_ms;
    return h->view->source_kind==1||(slot_hb&&age_ms(now,slot_hb)<=1000);
}
void awm_hub_reconfigure(AwmHub *h){if(h->view){InterlockedExchange(&h->view->live,0);h->view->video_epoch++;if(h->ready)SetEvent(h->ready);}}
void awm_hub_close(AwmHub *h){if(h->stop_event)CloseHandle(h->stop_event);if(h->ready)CloseHandle(h->ready);if(h->view)UnmapViewOfFile(h->view);if(h->mapping)CloseHandle(h->mapping);memset(h,0,sizeof(*h));}
bool awm_hub_publish(AwmHub *h,const uint8_t *p,uint32_t w,uint32_t height,int64_t pts){
    if(!h->view||!awm_frame_shape(w,height)||h->view->stop)return false;
    int slot=1-(int)InterlockedCompareExchange(&h->view->published,0,0);AwmFrameSlot *s=&h->view->slots[slot];
    uint64_t tick=GetTickCount64(),next_frame=h->view->video_frames+1;
    InterlockedIncrement(&s->sequence);memcpy(s->pixels,p,(size_t)w*height*4);s->width=w;s->height=height;s->pts_us=pts;s->generation=h->view->generation;s->epoch=h->view->video_epoch;s->frame_number=next_frame;s->heartbeat_ms=tick;MemoryBarrier();InterlockedIncrement(&s->sequence);
    *(volatile uint64_t *)&h->view->heartbeat_ms=tick;*(volatile uint64_t *)&h->view->session_heartbeat_ms=tick;
    InterlockedIncrement64((volatile LONG64 *)&h->view->video_frames);
    InterlockedExchange(&h->view->published,slot);InterlockedExchange(&h->view->live,1);if(h->ready)SetEvent(h->ready);return true;
}
bool awm_hub_copy_frame(AwmHub *h,uint8_t *p,size_t cap,AwmFrameInfo *info){
    if(!awm_hub_live(h,GetTickCount64()))return false;
    for(int tries=0;tries<3;tries++){
        int index=(int)InterlockedCompareExchange(&h->view->published,0,0);if(index<0||index>1)return false;AwmFrameSlot *s=&h->view->slots[index];LONG seq=InterlockedCompareExchange(&s->sequence,0,0);
        if(seq&1)continue;uint32_t sw=s->width,sh=s->height;uint64_t tick=s->heartbeat_ms;
        if(!awm_frame_shape(sw,sh)||cap<(size_t)sw*sh*4||(h->view->source_kind!=1&&age_ms(GetTickCount64(),tick)>1000)||s->epoch!=h->view->video_epoch)return false;
        AwmFrameInfo copied={sw,sh,s->epoch,s->frame_number,tick,s->pts_us};memcpy(p,s->pixels,(size_t)sw*sh*4);MemoryBarrier();
        if(seq==InterlockedCompareExchange(&s->sequence,0,0)&&s->epoch==h->view->video_epoch&&awm_hub_live(h,GetTickCount64())){*info=copied;return true;}
    }return false;
}
bool awm_hub_copy(AwmHub *h,uint8_t *p,size_t cap,uint32_t *w,uint32_t *height,int64_t *pts){AwmFrameInfo info;if(!awm_hub_copy_frame(h,p,cap,&info))return false;*w=info.width;*height=info.height;if(pts)*pts=info.pts_us;return true;}
void awm_hub_stop(AwmHub *h){if(h->view){InterlockedExchange(&h->view->live,0);InterlockedExchange(&h->view->microphone_live,0);h->view->microphone_peak=0;InterlockedExchange(&h->view->stop,1);if(h->stop_event)SetEvent(h->stop_event);if(h->ready)SetEvent(h->ready);}}
bool awm_pipe_io_ev(HANDLE pipe,void *data,DWORD bytes,bool write,HANDLE cancel,DWORD timeout,HANDLE io_event){
    BYTE *p=data;ULONGLONG deadline=GetTickCount64()+timeout;
    HANDLE ev=io_event;bool owned=false;
    if(!ev){ev=CreateEventW(NULL,TRUE,FALSE,NULL);if(!ev)return false;owned=true;}
    bool result=true;
    while(bytes){
        if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0){SetLastError(ERROR_CANCELLED);result=false;break;}
        ResetEvent(ev);OVERLAPPED o={0};o.hEvent=ev;DWORD n=0;BOOL ok=write?WriteFile(pipe,p,bytes,&n,&o):ReadFile(pipe,p,bytes,&n,&o);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){HANDLE waits[2]={ev,cancel};ULONGLONG now=GetTickCount64();DWORD wait=WaitForMultipleObjects(cancel?2:1,waits,FALSE,(DWORD)(deadline>now?deadline-now:0));
            if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(pipe,&o,&n,FALSE);else{CancelIoEx(pipe,&o);GetOverlappedResult(pipe,&o,&n,TRUE);ok=FALSE;SetLastError(wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_CANCELLED);}}
        if(!ok||!n){result=false;break;}p+=n;bytes-=n;
    }
    if(owned)CloseHandle(ev);return result;
}
bool awm_pipe_io(HANDLE pipe,void *data,DWORD bytes,bool write,HANDLE cancel,DWORD timeout){return awm_pipe_io_ev(pipe,data,bytes,write,cancel,timeout,NULL);}
void awm_scale_bgra(const uint8_t *s,uint32_t sw,uint32_t sh,uint8_t *d,uint32_t dw,uint32_t dh){
    if(sw==dw&&sh==dh){memcpy(d,s,(size_t)dw*dh*4);return;}
    memset(d,0,(size_t)dw*dh*4);uint32_t rw=dw,rh=(uint32_t)((uint64_t)sh*dw/sw);if(rh>dh){rh=dh;rw=(uint32_t)((uint64_t)sw*dh/sh);}
    if(!rw||!rh)return;uint32_t ox=(dw-rw)/2,oy=(dh-rh)/2;
    for(uint32_t y=0;y<rh;y++){uint32_t sy=(uint32_t)((uint64_t)y*sh/rh);for(uint32_t x=0;x<rw;x++){uint32_t sx=(uint32_t)((uint64_t)x*sw/rw);memcpy(d+((size_t)(y+oy)*dw+x+ox)*4,s+((size_t)sy*sw+sx)*4,4);}}
}
static uint8_t clamp(int x){return (uint8_t)(x<0?0:x>255?255:x);}
void awm_bgra_to_nv12(const uint8_t *p,uint32_t w,uint32_t h,uint8_t *d){
    uint8_t *uv=d+(size_t)w*h;size_t row_bytes=(size_t)w*4;
    for(uint32_t y=0;y<h;y+=2){
        const uint8_t *r0=p+(size_t)y*row_bytes,*r1=r0+row_bytes;
        uint8_t *y0=d+(size_t)y*w,*y1=y0+w,*uv_row=uv+(size_t)(y>>1)*w;
        for(uint32_t x=0;x<w;x+=2){
            const uint8_t *q00=r0+(size_t)x*4,*q01=q00+4,*q10=r1+(size_t)x*4,*q11=q10+4;
            y0[x]=clamp(((47*q00[2]+157*q00[1]+16*q00[0]+128)>>8)+16);
            y0[x+1]=clamp(((47*q01[2]+157*q01[1]+16*q01[0]+128)>>8)+16);
            y1[x]=clamp(((47*q10[2]+157*q10[1]+16*q10[0]+128)>>8)+16);
            y1[x+1]=clamp(((47*q11[2]+157*q11[1]+16*q11[0]+128)>>8)+16);
            int b=(q00[0]+q01[0]+q10[0]+q11[0])>>2;
            int g=(q00[1]+q01[1]+q10[1]+q11[1])>>2;
            int r=(q00[2]+q01[2]+q10[2]+q11[2])>>2;
            uv_row[x]=clamp(((-26*r-87*g+112*b+128)>>8)+128);
            uv_row[x+1]=clamp(((112*r-102*g-10*b+128)>>8)+128);
        }
    }
}
