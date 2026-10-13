/* AWM export adapter. Upstream server/demux/packet sinks remain unchanged.
 * This managed client accepts only awm-pipe: targets and never records files. */
#include "recorder.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "util/log.h"
#include "util/tick.h"
#include "native/protocol.h"
typedef struct Exporter {HANDLE pipe;uint32_t sequence;bool failed;} Exporter;
static Exporter *exporter(struct sc_recorder *r){return (Exporter *)r->ctx;}
static bool send_bytes(HANDLE pipe,const void *data,DWORD bytes){
    const BYTE *p=data;while(bytes){OVERLAPPED o={0};o.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!o.hEvent)return false;DWORD n=0;BOOL ok=WriteFile(pipe,p,bytes,&n,&o);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){DWORD wait=WaitForSingleObject(o.hEvent,1000);if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(pipe,&o,&n,FALSE);else{CancelIoEx(pipe,&o);GetOverlappedResult(pipe,&o,&n,TRUE);ok=FALSE;}}
        CloseHandle(o.hEvent);if(!ok||!n)return false;p+=n;bytes-=n;}return true;
}
static bool send_locked(struct sc_recorder *r,AwmPacketHeader *h,const void *data){
    Exporter *e=exporter(r);if(r->stopped||e->failed)return false;
    h->magic=AWM_EXPORT_MAGIC;h->version=AWM_PROTOCOL_VERSION;h->sequence=++e->sequence;
    bool ok=send_bytes(e->pipe,h,sizeof(*h))&&(!h->size||send_bytes(e->pipe,data,h->size));if(!ok){e->failed=true;r->stopped=true;sc_cond_signal(&r->cond);}return ok;
}
static struct sc_recorder *from_sink(struct sc_packet_sink *s,bool audio){return (struct sc_recorder *)((char *)s-(audio?offsetof(struct sc_recorder,audio_packet_sink):offsetof(struct sc_recorder,video_packet_sink)));}
static bool open_sink(struct sc_packet_sink *s,const AVCodecParameters *p,const struct sc_stream_session *session,bool audio){
    struct sc_recorder *r=from_sink(s,audio);AwmPacketHeader h={0};h.type=AWM_MSG_OPEN;h.stream=audio?1:0;h.codec=(uint32_t)p->codec_id;h.width=!audio&&session?session->video.width:(uint32_t)p->width;h.height=!audio&&session?session->video.height:(uint32_t)p->height;h.sample_rate=(uint32_t)p->sample_rate;h.channels=(uint32_t)p->ch_layout.nb_channels;h.size=(uint32_t)p->extradata_size;
    sc_mutex_lock(&r->mutex);bool ok=h.size<=AWM_PACKET_MAX&&send_locked(r,&h,p->extradata);sc_mutex_unlock(&r->mutex);return ok;
}
static bool video_open(struct sc_packet_sink *s,const AVCodecParameters *p,const struct sc_stream_session *v){return open_sink(s,p,v,false);}
static bool audio_open(struct sc_packet_sink *s,const AVCodecParameters *p,const struct sc_stream_session *v){return open_sink(s,p,v,true);}
static enum sc_sink_result push_sink(struct sc_packet_sink *s,const AVPacket *p,bool audio){
    struct sc_recorder *r=from_sink(s,audio);AwmPacketHeader h={0};h.type=AWM_MSG_PACKET;h.stream=audio?1:0;h.flags=(uint32_t)p->flags|(p->pts==AV_NOPTS_VALUE?AWM_FLAG_CONFIG:0);h.pts_us=p->pts;h.size=(uint32_t)p->size;
    sc_mutex_lock(&r->mutex);bool ok=p->size>0&&h.size<=AWM_PACKET_MAX&&send_locked(r,&h,p->data);sc_mutex_unlock(&r->mutex);return ok?SC_SINK_OK:SC_SINK_KO;
}
static enum sc_sink_result video_push(struct sc_packet_sink *s,const AVPacket *p){return push_sink(s,p,false);}
static enum sc_sink_result audio_push(struct sc_packet_sink *s,const AVPacket *p){return push_sink(s,p,true);}
static enum sc_sink_result video_session(struct sc_packet_sink *s,const struct sc_stream_session *v){
    struct sc_recorder *r=from_sink(s,false);AwmPacketHeader h={0};h.type=AWM_MSG_SESSION;h.width=v->video.width;h.height=v->video.height;h.flags=v->video.client_resized?1:0;
    sc_mutex_lock(&r->mutex);bool ok=send_locked(r,&h,NULL);sc_mutex_unlock(&r->mutex);return ok?SC_SINK_OK:SC_SINK_KO;
}
static void close_sink(struct sc_packet_sink *s,bool audio){struct sc_recorder *r=from_sink(s,audio);AwmPacketHeader h={0};h.type=AWM_MSG_CLOSE;h.stream=audio?1:0;sc_mutex_lock(&r->mutex);send_locked(r,&h,NULL);r->stopped=true;sc_cond_signal(&r->cond);sc_mutex_unlock(&r->mutex);}
static void video_close(struct sc_packet_sink *s){close_sink(s,false);}
static void audio_close(struct sc_packet_sink *s){close_sink(s,true);}
static void audio_disable(struct sc_packet_sink *s){struct sc_recorder *r=from_sink(s,true);AwmPacketHeader h={0};h.type=AWM_MSG_DISABLED;h.stream=1;sc_mutex_lock(&r->mutex);send_locked(r,&h,NULL);sc_mutex_unlock(&r->mutex);}
static int monitor(void *ptr){struct sc_recorder *r=ptr;sc_mutex_lock(&r->mutex);while(!r->stopped){sc_cond_timedwait(&r->cond,&r->mutex,sc_tick_now()+SC_TICK_FROM_MS(500));if(!r->stopped){AwmPacketHeader h={0};h.type=AWM_MSG_HEARTBEAT;send_locked(r,&h,NULL);}}bool ok=!exporter(r)->failed;sc_mutex_unlock(&r->mutex);r->cbs->on_ended(r,ok,r->cbs_userdata);return 0;}
bool sc_recorder_init(struct sc_recorder *r,const char *name,enum sc_record_format format,bool video,bool audio,enum sc_orientation orientation,const struct sc_recorder_callbacks *cbs,void *userdata){
    (void)format;(void)orientation;static const char prefix[]="awm-pipe:\\\\.\\pipe\\AWM.Export.";if(strncmp(name,prefix,sizeof(prefix)-1)){LOGE("AWM bridge only accepts an owned awm-pipe target");return false;}
    memset(r,0,sizeof(*r));r->filename=_strdup(name+9);Exporter *e=calloc(1,sizeof(*e));r->ctx=(AVFormatContext *)(void *)e;if(!r->filename||!e){free(r->filename);free(e);return false;}
    if(!sc_mutex_init(&r->mutex)){free(r->filename);free(r->ctx);return false;}if(!sc_cond_init(&r->cond)){sc_mutex_destroy(&r->mutex);free(r->filename);free(r->ctx);return false;}
    r->video=video;r->audio=audio;r->cbs=cbs;r->cbs_userdata=userdata;
    static const struct sc_packet_sink_ops v={.open=video_open,.close=video_close,.push=video_push,.push_session=video_session};
    static const struct sc_packet_sink_ops a={.open=audio_open,.close=audio_close,.push=audio_push,.disable=audio_disable};
    r->video_packet_sink.ops=&v;r->audio_packet_sink.ops=&a;return true;
}
bool sc_recorder_start(struct sc_recorder *r){
    wchar_t name[512];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,r->filename,-1,name,512))return false;
    Exporter *e=exporter(r);e->pipe=CreateFileW(name,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);if(e->pipe==INVALID_HANDLE_VALUE){e->pipe=NULL;LOGE("AWM export pipe unavailable (%lu)",GetLastError());return false;}
    LOGI("AWM export protocol 3; ordered heartbeat/session events, no media files or speaker playback");return sc_thread_create(&r->thread,monitor,"AWM export",r);
}
void sc_recorder_stop(struct sc_recorder *r){sc_mutex_lock(&r->mutex);r->stopped=true;sc_cond_signal(&r->cond);sc_mutex_unlock(&r->mutex);}
void sc_recorder_join(struct sc_recorder *r){sc_thread_join(&r->thread,NULL);}
void sc_recorder_destroy(struct sc_recorder *r){Exporter *e=exporter(r);if(e&&e->pipe)CloseHandle(e->pipe);free(e);free(r->filename);sc_cond_destroy(&r->cond);sc_mutex_destroy(&r->mutex);}
