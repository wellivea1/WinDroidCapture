#include "obs.h"
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(char *out,size_t cap,const char *s){awm_copy(out,cap,s);return false;}
static bool hash_base64(const wchar_t *algorithm,const char *input,char *out,size_t cap){
    BCRYPT_ALG_HANDLE a=NULL;BCRYPT_HASH_HANDLE h=NULL;BYTE digest[32];DWORD n=0,bytes=0;bool ok=false;
    if(BCryptOpenAlgorithmProvider(&a,algorithm,NULL,0)<0)return false;
    if(BCryptGetProperty(a,BCRYPT_HASH_LENGTH,(BYTE *)&n,sizeof(n),&bytes,0)>=0&&n<=sizeof(digest)&&BCryptCreateHash(a,&h,NULL,0,NULL,0,0)>=0&&BCryptHashData(h,(BYTE *)input,(ULONG)strlen(input),0)>=0&&BCryptFinishHash(h,digest,n,0)>=0){DWORD length=(DWORD)cap;ok=CryptBinaryToStringA(digest,n,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,out,&length)!=0;}
    if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);SecureZeroMemory(digest,sizeof(digest));return ok;
}
bool obs_auth_string(const char *password,const char *salt,const char *challenge,char *out,size_t cap){
    char first[2048],secret[64],second[2048];if(snprintf(first,sizeof(first),"%s%s",password,salt)<0||strlen(password)+strlen(salt)>=sizeof(first))return false;
    bool ok=hash_base64(BCRYPT_SHA256_ALGORITHM,first,secret,sizeof(secret));
    if(ok&&strlen(secret)+strlen(challenge)<sizeof(second)){snprintf(second,sizeof(second),"%s%s",secret,challenge);ok=hash_base64(BCRYPT_SHA256_ALGORITHM,second,out,cap);}else ok=false;
    SecureZeroMemory(first,sizeof(first));SecureZeroMemory(secret,sizeof(secret));SecureZeroMemory(second,sizeof(second));return ok;
}
static bool socket_wait(Obs *o,bool write,ULONGLONG until){
    while(GetTickCount64()<until){if(o->cancel&&WaitForSingleObject(o->cancel,0)==WAIT_OBJECT_0)return false;
        fd_set set;FD_ZERO(&set);FD_SET(o->socket,&set);struct timeval t={0,100000};
        int r=select(0,write?NULL:&set,write?&set:NULL,NULL,&t);if(r>0)return true;if(r<0)return false;
    }return false;
}
static bool socket_io(Obs *o,char *data,size_t n,bool write,ULONGLONG until){
    size_t offset=0;while(offset<n){if(!socket_wait(o,write,until))return false;
        int r=write?send(o->socket,data+offset,(int)(n-offset),0):recv(o->socket,data+offset,(int)(n-offset),0);
        if(r==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK)continue;if(r<=0)return false;offset+=(size_t)r;}return true;
}
static bool ws_send(Obs *o,unsigned opcode,const char *message,size_t n,ULONGLONG until){
    if(n>AWM_TEXT)return false;BYTE header[14],mask[4];if(BCryptGenRandom(NULL,mask,4,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return false;
    size_t h=0;header[h++]=(BYTE)(0x80|opcode);if(n<126)header[h++]=(BYTE)(0x80|n);else{header[h++]=0x80|126;header[h++]=(BYTE)(n>>8);header[h++]=(BYTE)n;}
    memcpy(header+h,mask,4);h+=4;char *payload=malloc(n?n:1);if(!payload)return false;for(size_t i=0;i<n;i++)payload[i]=(char)((BYTE)message[i]^mask[i%4]);
    bool ok=socket_io(o,(char *)header,h,true,until)&&socket_io(o,payload,n,true,until);free(payload);return ok;
}
static bool ws_receive(Obs *o,char *out,size_t cap,ULONGLONG until,char *err,size_t ec){
    size_t n=0;bool fragment=false;for(;;){BYTE header[2];if(!socket_io(o,(char *)header,2,false,until))return fail(err,ec,"OBS read timed out, was cancelled, or the connection closed.");
        unsigned opcode=header[0]&15;bool final=(header[0]&0x80)!=0;uint64_t length=header[1]&127;
        if((header[0]&0x70)||(header[1]&0x80))return fail(err,ec,"OBS sent an invalid WebSocket frame.");
        if(length==126){BYTE b[2];if(!socket_io(o,(char *)b,2,false,until))return false;length=((unsigned)b[0]<<8)|b[1];}
        else if(length==127){BYTE b[8];if(!socket_io(o,(char *)b,8,false,until))return false;length=0;for(int i=0;i<8;i++)length=(length<<8)|b[i];}
        if(opcode>=8){if(!final||length>125)return fail(err,ec,"Invalid OBS control frame.");char control[126];if(!socket_io(o,control,(size_t)length,false,until))return false;
            if(opcode==8)return fail(err,ec,"OBS closed the connection. Check the WebSocket password and supported version.");
            if(opcode==9&&!ws_send(o,10,control,(size_t)length,until))return false;
            if(opcode!=9&&opcode!=10)return fail(err,ec,"Unsupported OBS control frame.");continue;}
        if((opcode!=1&&opcode!=0)||(opcode==0&&!fragment)||(opcode==1&&fragment))return fail(err,ec,"Unsupported OBS WebSocket message encoding.");
        if(length>=cap-n)return fail(err,ec,"OBS response exceeded the bounded message limit.");
        if(!socket_io(o,out+n,(size_t)length,false,until))return fail(err,ec,"OBS response timed out.");n+=(size_t)length;fragment=!final;
        if(final){out[n]=0;return true;}
    }
}
void obs_disconnect(Obs *o){if(o->socket!=INVALID_SOCKET){closesocket(o->socket);o->socket=INVALID_SOCKET;}o->connected=false;}
bool obs_connect(Obs *o,unsigned port,const wchar_t *password,HANDLE cancel,char *err,size_t cap){
    memset(o,0,sizeof(*o));o->socket=INVALID_SOCKET;o->cancel=cancel;o->port=port;
    if(!port||port>65535)return fail(err,cap,"OBS port must be 1..65535.");
    o->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(o->socket==INVALID_SOCKET)return fail(err,cap,"Cannot create local OBS connection.");
    u_long nonblocking=1;ioctlsocket(o->socket,FIONBIO,&nonblocking);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((u_short)port);
    int r=connect(o->socket,(struct sockaddr *)&a,sizeof(a));ULONGLONG until=GetTickCount64()+3000;
    if(r==SOCKET_ERROR){int error=WSAGetLastError();if(error!=WSAEWOULDBLOCK||!socket_wait(o,true,until))goto connection_error;int soerr=0,sz=sizeof(soerr);if(getsockopt(o->socket,SOL_SOCKET,SO_ERROR,(char *)&soerr,&sz)||soerr)goto connection_error;}
    BYTE nonce[16];char key[64];DWORD kl=sizeof(key);if(BCryptGenRandom(NULL,nonce,sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||!CryptBinaryToStringA(nonce,sizeof(nonce),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,key,&kl))goto connection_error;
    char request[512];snprintf(request,sizeof(request),"GET / HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: obswebsocket.json\r\n\r\n",port,key);
    if(!socket_io(o,request,strlen(request),true,until))goto connection_error;
    char headers[8192];size_t n=0;do{if(n+1>=sizeof(headers)||!socket_io(o,headers+n,1,false,until))goto connection_error;n++;headers[n]=0;}while(!strstr(headers,"\r\n\r\n"));
    if(strncmp(headers,"HTTP/1.1 101 ",13))goto connection_error;
    char concat[128],accept[64];snprintf(concat,sizeof(concat),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);if(!hash_base64(BCRYPT_SHA1_ALGORITHM,concat,accept,sizeof(accept)))goto connection_error;
    bool match=false;for(char *line=headers;line&&*line;){char *next=strstr(line,"\r\n");if(next)*next=0;if(!_strnicmp(line,"Sec-WebSocket-Accept:",21)){char *v=line+21;while(*v==' ')v++;match=!strcmp(v,accept);}line=next?next+2:NULL;}
    if(!match)goto connection_error;
    char message[AWM_TEXT];Json j;if(!ws_receive(o,message,sizeof(message),until,err,cap)||!json_parse(&j,message)||json_int(&j,json_get(&j,0,"op"),-1)!=0)goto bad;
    int d=json_get(&j,0,"d");if(json_int(&j,json_get(&j,d,"rpcVersion"),0)<1){fail(err,cap,"OBS WebSocket RPC version 1 is required (OBS 28+ with obs-websocket 5.x).");goto bad;}
    json_string(&j,json_get(&j,d,"obsStudioVersion"),o->version,sizeof(o->version));json_string(&j,json_get(&j,d,"obsWebSocketVersion"),o->websocket_version,sizeof(o->websocket_version));
    int auth=json_get(&j,d,"authentication");char identify[512];
    if(auth>=0){char salt[512],challenge[512],pass[1024],response[64];salt[0]=challenge[0]=0;wide_to_utf8(password,pass,sizeof(pass));
        bool ok=json_string(&j,json_get(&j,auth,"salt"),salt,sizeof(salt))&&json_string(&j,json_get(&j,auth,"challenge"),challenge,sizeof(challenge))&&obs_auth_string(pass,salt,challenge,response,sizeof(response));SecureZeroMemory(pass,sizeof(pass));
        if(!ok){fail(err,cap,"Could not compute OBS authentication.");goto bad;}snprintf(identify,sizeof(identify),"{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":0,\"authentication\":\"%s\"}}",response);SecureZeroMemory(response,sizeof(response));
    }else awm_copy(identify,sizeof(identify),"{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":0}}");
    bool sent=ws_send(o,1,identify,strlen(identify),until);SecureZeroMemory(identify,sizeof(identify));
    if(!sent||!ws_receive(o,message,sizeof(message),until,err,cap)||!json_parse(&j,message)||json_int(&j,json_get(&j,0,"op"),-1)!=2){fail(err,cap,"OBS authentication or version negotiation failed. Check WebSocket settings and password.");goto bad;}
    if(json_int(&j,json_get(&j,json_get(&j,0,"d"),"negotiatedRpcVersion"),0)!=1){fail(err,cap,"OBS negotiated an unsupported RPC version.");goto bad;}
    o->connected=true;if(!obs_request(o,"GetVersion","{}",message,sizeof(message),err,cap))goto bad;
    if(!json_parse(&j,message)||!json_raw(&j,json_get(&j,0,"availableRequests"),o->available,sizeof(o->available))){fail(err,cap,"OBS did not supply supported requests.");goto bad;}return true;
connection_error:
    fail(err,cap,"Cannot connect to local OBS WebSocket. Enable Tools > WebSocket Server Settings; verify the port. OBS running alone does not enable webcam output.");
bad:
    obs_disconnect(o);return false;
}
static bool request_available(Obs *o,const char *name){Json j;if(!o->available[0])return true;if(!json_parse(&j,o->available)||j.tokens[0].type!='[')return false;for(int t=1;t<j.tokens[0].next;t=j.tokens[t].next)if(json_equal(&j,t,name))return true;return false;}
bool obs_request(Obs *o,const char *type,const char *data,char *out,size_t outcap,char *err,size_t cap){
    if(!o->connected)return fail(err,cap,"OBS is not connected.");if(!request_available(o,type)){snprintf(err,cap,"OBS WebSocket does not support required request %s. Update OBS.",type);return false;}
    char message[AWM_TEXT],id[32];snprintf(id,sizeof(id),"awm-%u",++o->sequence);
    int n=snprintf(message,sizeof(message),"{\"op\":6,\"d\":{\"requestType\":\"%s\",\"requestId\":\"%s\",\"requestData\":%s}}",type,id,data);
    ULONGLONG until=GetTickCount64()+2500;if(n<0||(size_t)n>=sizeof(message)||!ws_send(o,1,message,(size_t)n,until))return fail(err,cap,"OBS request send failed or timed out.");
    for(;;){if(!ws_receive(o,message,sizeof(message),until,err,cap))return false;Json j;if(!json_parse(&j,message))return fail(err,cap,"OBS sent invalid JSON.");
        if(json_int(&j,json_get(&j,0,"op"),-1)!=7)continue;int d=json_get(&j,0,"d");if(!json_equal(&j,json_get(&j,d,"requestId"),id))continue;
        int status=json_get(&j,d,"requestStatus");o->last_status_code=json_int(&j,json_get(&j,status,"code"),-1);if(!json_bool(&j,json_get(&j,status,"result"),false)){char comment[512]={0};json_string(&j,json_get(&j,status,"comment"),comment,sizeof(comment));snprintf(err,cap,"OBS %s failed (%d): %s",type,o->last_status_code,comment);return false;}
        int response=json_get(&j,d,"responseData");if(response<0){awm_copy(out,outcap,"{}");return true;}return json_raw(&j,response,out,outcap);
    }
}
static bool output_idle(Obs *o,char *err,size_t cap){
    const char *requests[]={"GetStreamStatus","GetRecordStatus","GetReplayBufferStatus","GetVirtualCamStatus"};char response[AWM_TEXT];
    for(int i=0;i<4;i++){o->last_status_code=0;if(!obs_request(o,requests[i],"{}",response,sizeof(response),err,cap)){if(i==2&&o->last_status_code==604)continue;return false;}Json j;if(!json_parse(&j,response))return false;if(json_bool(&j,json_get(&j,0,"outputActive"),true))return fail(err,cap,"An OBS stream, recording, replay buffer, or Virtual Camera is already active. This app will not take over it. Stop it yourself or use a separate OBS configuration.");}return true;
}
bool obs_preflight(Obs *o,char *err,size_t cap){
    const char *required[]={"CreateScene","RemoveScene","GetSceneItemList","GetSceneItemId","GetSceneItemTransform","SetSceneItemTransform","SetSceneItemEnabled","GetCurrentProgramScene","SetCurrentProgramScene","GetVideoSettings","GetInputKindList","GetInputDefaultSettings","CreateInput","RemoveInput","GetInputSettings","SetInputSettings","GetInputPropertiesListPropertyItems","StartVirtualCam","StopVirtualCam","GetVirtualCamStatus"};
    for(size_t i=0;i<sizeof(required)/sizeof(required[0]);i++)if(!request_available(o,required[i])){snprintf(err,cap,"OBS lacks request %s; update OBS.",required[i]);return false;}
    if(!output_idle(o,err,cap))return false;char response[AWM_TEXT];Json j;
    if(!obs_request(o,"GetCurrentProgramScene","{}",response,sizeof(response),err,cap)||!json_parse(&j,response)||!json_string(&j,json_get(&j,0,"currentProgramSceneName"),o->previous_scene,sizeof(o->previous_scene)))return fail(err,cap,"Cannot inspect OBS program scene.");
    if(!obs_request(o,"GetVideoSettings","{}",response,sizeof(response),err,cap)||!json_parse(&j,response))return false;
    o->base_width=json_int(&j,json_get(&j,0,"baseWidth"),0);o->base_height=json_int(&j,json_get(&j,0,"baseHeight"),0);o->output_width=json_int(&j,json_get(&j,0,"outputWidth"),0);o->output_height=json_int(&j,json_get(&j,0,"outputHeight"),0);o->fps_num=json_int(&j,json_get(&j,0,"fpsNumerator"),0);o->fps_den=json_int(&j,json_get(&j,0,"fpsDenominator"),1);
    if(o->base_width<=0||o->base_height<=0||o->output_width<=0||o->output_height<=0||o->fps_num<=0||o->fps_den<=0)return fail(err,cap,"OBS returned invalid video settings.");
    if(!obs_request(o,"GetInputKindList","{\"unversioned\":true}",response,sizeof(response),err,cap)||!json_parse(&j,response))return false;
    int kinds=json_get(&j,0,"inputKinds");bool found=false;if(kinds>=0)for(int t=kinds+1;t<j.tokens[kinds].next;t=j.tokens[t].next)if(json_equal(&j,t,"window_capture"))found=true;
    if(!found)return fail(err,cap,"OBS window_capture input is unavailable. Desktop capture is never used as a fallback.");return true;
}
static void names_data(Obs *o,char *data,size_t cap){char s[512],i[512];json_escape(o->scene,s,sizeof(s));json_escape(o->input,i,sizeof(i));snprintf(data,cap,"{\"sceneName\":%s,\"inputName\":%s}",s,i);}
static bool list_has_value(Obs *o,const char *property,int v,char *err,size_t cap){char data[1024],response[AWM_TEXT],input[512];json_escape(o->input,input,sizeof(input));snprintf(data,sizeof(data),"{\"inputName\":%s,\"propertyName\":\"%s\"}",input,property);
    if(!obs_request(o,"GetInputPropertiesListPropertyItems",data,response,sizeof(response),err,cap))return false;Json j;if(!json_parse(&j,response))return false;int a=json_get(&j,0,"propertyItems");if(a>=0)for(int t=a+1;t<j.tokens[a].next;t=j.tokens[t].next){if(json_int(&j,json_get(&j,t,"itemValue"),-1)==v&&json_bool(&j,json_get(&j,t,"itemEnabled"),false))return true;}return fail(err,cap,"OBS window capture does not expose the required WGC method or exact-title priority.");}
bool obs_start_output(Obs *o,const wchar_t *title,const wchar_t *exe,bool consent,char *err,size_t cap){
    if(!consent)return fail(err,cap,"Explicit consent is required to switch the OBS program scene and start Virtual Camera.");
    char expected_scene[256];awm_copy(expected_scene,sizeof(expected_scene),o->previous_scene);
    if(!obs_preflight(o,err,cap)||strcmp(expected_scene,o->previous_scene))return fail(err,cap,"OBS changed after preflight or has an active output. Inspect it and approve again.");
    char response[AWM_TEXT],data[4096],scene[512],input[512];GUID guid;wchar_t gw[40];CoCreateGuid(&guid);StringFromGUID2(&guid,gw,40);char gu[80];wide_to_utf8(gw,gu,sizeof(gu));
    snprintf(o->scene,sizeof(o->scene),"WinDroid Capture %s",gu);snprintf(o->input,sizeof(o->input),"AWM camera %s",gu);json_escape(o->scene,scene,sizeof(scene));json_escape(o->input,input,sizeof(input));
    snprintf(data,sizeof(data),"{\"sceneName\":%s}",scene);if(!obs_request(o,"CreateScene",data,response,sizeof(response),err,cap))return false;o->scene_created=true;
    if(!obs_request(o,"GetInputDefaultSettings","{\"inputKind\":\"window_capture\"}",response,sizeof(response),err,cap))return false;Json j;if(!json_parse(&j,response))return false;
    int defaults=json_get(&j,0,"defaultInputSettings");if(json_get(&j,defaults,"client_area")<0||json_get(&j,defaults,"cursor")<0)return fail(err,cap,"OBS window capture settings are incompatible with the inspected schema.");
    snprintf(data,sizeof(data),"{\"sceneName\":%s,\"inputName\":%s,\"inputKind\":\"window_capture\",\"sceneItemEnabled\":false,\"inputSettings\":{\"window\":\"\",\"cursor\":false,\"client_area\":true,\"capture_audio\":false}}",scene,input);
    if(!obs_request(o,"CreateInput",data,response,sizeof(response),err,cap))return false;o->input_created=true;
    if(!json_parse(&j,response))return false;o->item_id=json_int(&j,json_get(&j,0,"sceneItemId"),-1);if(o->item_id<0)return fail(err,cap,"OBS did not return an owned scene-item ID.");
    if(!list_has_value(o,"method",2,err,cap)||!list_has_value(o,"priority",0,err,cap))return false;
    snprintf(data,sizeof(data),"{\"inputName\":%s,\"propertyName\":\"window\"}",input);
    if(!obs_request(o,"GetInputPropertiesListPropertyItems",data,response,sizeof(response),err,cap)||!json_parse(&j,response))return false;
    char expected[512],exe8[1024],window_value[2048]={0};wide_to_utf8(title,expected,sizeof(expected));wide_to_utf8(exe,exe8,sizeof(exe8));
    int array=json_get(&j,0,"propertyItems");if(array>=0)for(int t=array+1;t<j.tokens[array].next;t=j.tokens[t].next){char value[2048];if(json_bool(&j,json_get(&j,t,"itemEnabled"),false)&&json_string(&j,json_get(&j,t,"itemValue"),value,sizeof(value))){size_t len=strlen(expected);char *last=strrchr(value,':');if(!strncmp(value,expected,len)&&value[len]==':'&&last&&!_stricmp(last+1,exe8))awm_copy(window_value,sizeof(window_value),value);}}
    if(!window_value[0])return fail(err,cap,"OBS did not enumerate the exact owned scrcpy window. Keep it visible and unminimized; no other window will be captured.");
    char escaped_window[4096];if(!json_escape(window_value,escaped_window,sizeof(escaped_window)))return false;
    char settings[8192];snprintf(settings,sizeof(settings),"{\"inputName\":%s,\"inputSettings\":{\"window\":%s,\"method\":2,\"priority\":0,\"cursor\":false,\"client_area\":true,\"capture_audio\":false},\"overlay\":true}",input,escaped_window);
    if(!obs_request(o,"SetInputSettings",settings,response,sizeof(response),err,cap))return false;
    snprintf(data,sizeof(data),"{\"inputName\":%s}",input);if(!obs_request(o,"GetInputSettings",data,response,sizeof(response),err,cap)||!json_parse(&j,response))return false;
    int set=json_get(&j,0,"inputSettings");if(!json_equal(&j,json_get(&j,set,"window"),window_value)||json_int(&j,json_get(&j,set,"priority"),-1)!=0||json_int(&j,json_get(&j,set,"method"),-1)!=2||json_bool(&j,json_get(&j,set,"cursor"),true)||!json_bool(&j,json_get(&j,set,"client_area"),false)||json_bool(&j,json_get(&j,set,"capture_audio"),false))return fail(err,cap,"OBS did not retain the safe window capture settings.");
    snprintf(data,sizeof(data),"{\"sceneName\":%s,\"sceneItemId\":%d,\"sceneItemTransform\":{\"positionX\":0,\"positionY\":0,\"rotation\":0,\"scaleX\":1,\"scaleY\":1,\"cropLeft\":0,\"cropRight\":0,\"cropTop\":0,\"cropBottom\":0,\"alignment\":5,\"boundsType\":\"OBS_BOUNDS_SCALE_INNER\",\"boundsAlignment\":0,\"boundsWidth\":%d,\"boundsHeight\":%d}}",scene,o->item_id,o->base_width,o->base_height);
    if(!obs_request(o,"SetSceneItemTransform",data,response,sizeof(response),err,cap))return false;
    snprintf(data,sizeof(data),"{\"sceneName\":%s,\"sceneItemId\":%d,\"sceneItemEnabled\":true}",scene,o->item_id);
    if(!obs_request(o,"SetSceneItemEnabled",data,response,sizeof(response),err,cap))return false;
    snprintf(data,sizeof(data),"{\"sceneName\":%s}",scene);if(!obs_request(o,"SetCurrentProgramScene",data,response,sizeof(response),err,cap))return false;o->switched=true;
    if(!obs_request(o,"StartVirtualCam","{}",response,sizeof(response),err,cap))return false;o->virtual_owned=true;return true;
}
bool obs_poll_output(Obs *o,int *w,int *h,char *err,size_t cap){
    char response[AWM_TEXT],data[1024],scene[512];Json j;
    if(!obs_request(o,"GetCurrentProgramScene","{}",response,sizeof(response),err,cap)||!json_parse(&j,response))return false;
    if(!json_equal(&j,json_get(&j,0,"currentProgramSceneName"),o->scene))return fail(err,cap,"OBS program scene was changed outside this app. The phone session will stop; other OBS output is left under user control.");
    if(!obs_request(o,"GetVirtualCamStatus","{}",response,sizeof(response),err,cap)||!json_parse(&j,response)||!json_bool(&j,json_get(&j,0,"outputActive"),false))return fail(err,cap,"OBS Virtual Camera is no longer running.");
    json_escape(o->scene,scene,sizeof(scene));snprintf(data,sizeof(data),"{\"sceneName\":%s,\"sceneItemId\":%d}",scene,o->item_id);
    if(!obs_request(o,"GetSceneItemTransform",data,response,sizeof(response),err,cap)||!json_parse(&j,response))return false;
    int transform=json_get(&j,0,"sceneItemTransform");*w=json_int(&j,json_get(&j,transform,"sourceWidth"),0);*h=json_int(&j,json_get(&j,transform,"sourceHeight"),0);return true;
}
bool obs_blank(Obs *o,char *err,size_t cap){if(!o->connected||!o->scene_created||!o->input_created)return true;char scene[512],data[1024],response[AWM_TEXT];json_escape(o->scene,scene,sizeof(scene));snprintf(data,sizeof(data),"{\"sceneName\":%s,\"sceneItemId\":%d,\"sceneItemEnabled\":false}",scene,o->item_id);return obs_request(o,"SetSceneItemEnabled",data,response,sizeof(response),err,cap);}
bool obs_stop_output(Obs *o,char *err,size_t cap){
    if(!o->connected)return !o->virtual_owned;bool ok=obs_blank(o,err,cap);char response[AWM_TEXT];Json j;bool still_ours=false;
    if(!obs_request(o,"GetCurrentProgramScene","{}",response,sizeof(response),err,cap)||!json_parse(&j,response))return false;
    still_ours=json_equal(&j,json_get(&j,0,"currentProgramSceneName"),o->scene);
    /* A new stream/record/replay operation transfers the global scene/output decision back to the user. */
    if(o->virtual_owned&&still_ours){const char *requests[]={"GetStreamStatus","GetRecordStatus","GetReplayBufferStatus"};for(int i=0;i<3;i++){o->last_status_code=0;if(!obs_request(o,requests[i],"{}",response,sizeof(response),err,cap)){if(i==2&&o->last_status_code==604)continue;return false;}if(!json_parse(&j,response))return false;if(json_bool(&j,json_get(&j,0,"outputActive"),true)){still_ours=false;break;}}}
    if((o->virtual_owned||o->switched)&&!still_ours){o->cleanup_user_control=true;o->virtual_owned=false;o->switched=false;}
    if(o->virtual_owned&&still_ours){if(obs_request(o,"StopVirtualCam","{}",response,sizeof(response),err,cap))o->virtual_owned=false;else ok=false;}
    if(o->switched&&still_ours){char previous[1024],data[2048];json_escape(o->previous_scene,previous,sizeof(previous));snprintf(data,sizeof(data),"{\"sceneName\":%s}",previous);if(!obs_request(o,"SetCurrentProgramScene",data,response,sizeof(response),err,cap))ok=false;else o->switched=false;}
    /* Keep the disabled owned scene as an explicit black fallback. Never remove a graph the user may have extended. */
    return ok;
}
