#include <initguid.h>
#include "audio.h"
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <ksmedia.h>
#include <math.h>
#include <stdio.h>
/* C has no __uuidof. These identifiers are from the installed Windows SDK's IDL headers. */
DEFINE_GUID(CLSID_MMDeviceEnumerator,0xbcde0395,0xe52f,0x467c,0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator,0xa95664d2,0x9614,0x4f35,0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6);
DEFINE_GUID(IID_IMMEndpoint,0x1be09788,0x6894,0x4089,0x85,0x86,0x9a,0x2a,0x6c,0x26,0x5a,0xc5);
DEFINE_GUID(IID_IAudioClient,0x1cb9ad4c,0xdbfa,0x4c32,0xb1,0x78,0xc2,0xf5,0x68,0xa7,0x03,0xb2);
DEFINE_GUID(IID_IAudioCaptureClient,0xc8adbd64,0xe71e,0x48a0,0xa4,0xde,0x18,0x5c,0x39,0x5c,0xd3,0x17);
DEFINE_GUID(IID_IAudioSessionControl2,0xbfb7ff88,0x7239,0x4fc9,0x8f,0xa2,0x07,0xc9,0x50,0xbe,0x9c,0x6d);
DEFINE_GUID(IID_IAudioSessionManager2,0x77aa99a0,0x1bd6,0x484f,0x8b,0xc7,0x2c,0x65,0x4c,0x9a,0x9b,0x6f);
static const GUID float_subtype={3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static IMMDeviceEnumerator *enumerator(void){IMMDeviceEnumerator *e=NULL;CoCreateInstance(&CLSID_MMDeviceEnumerator,NULL,CLSCTX_INPROC_SERVER,&IID_IMMDeviceEnumerator,(void **)&e);return e;}
static void hr_error(HRESULT hr,char *out,size_t cap){snprintf(out,cap,"Windows audio error 0x%08lX. Check endpoint availability and Windows microphone privacy permission.",(unsigned long)hr);}
bool audio_enumerate(AudioEndpoints *out,char *err,size_t cap){
    memset(out,0,sizeof(*out));IMMDeviceEnumerator *e=enumerator();if(!e){awm_copy(err,cap,"Cannot enumerate Windows audio endpoints.");return false;}
    for(int flow=0;flow<2;flow++){
        IMMDeviceCollection *list=NULL;HRESULT hr=IMMDeviceEnumerator_EnumAudioEndpoints(e,(EDataFlow)flow,DEVICE_STATEMASK_ALL,&list);if(FAILED(hr)){hr_error(hr,err,cap);IMMDeviceEnumerator_Release(e);return false;}
        UINT count=0;IMMDeviceCollection_GetCount(list,&count);
        for(UINT i=0;i<count&&out->count<AWM_ENDPOINTS;i++){
            IMMDevice *d=NULL;if(FAILED(IMMDeviceCollection_Item(list,i,&d)))continue;LPWSTR id=NULL;DWORD state=0;IPropertyStore *store=NULL;PROPVARIANT pv;PropVariantInit(&pv);
            if(SUCCEEDED(IMMDevice_GetId(d,&id))&&SUCCEEDED(IMMDevice_GetState(d,&state))){AudioEndpoint *a=&out->items[out->count++];awm_wcopy(a->id,1024,id);a->state=state;a->flow=(EDataFlow)flow;
                if(SUCCEEDED(IMMDevice_OpenPropertyStore(d,STGM_READ,&store))&&SUCCEEDED(IPropertyStore_GetValue(store,&PKEY_Device_FriendlyName,&pv))&&pv.vt==VT_LPWSTR)awm_wcopy(a->name,256,pv.pwszVal);else awm_wcopy(a->name,256,L"Unnamed endpoint");}
            PropVariantClear(&pv);if(store)IPropertyStore_Release(store);CoTaskMemFree(id);IMMDevice_Release(d);
        }
        IMMDeviceCollection_Release(list);
    }
    IMMDeviceEnumerator_Release(e);return true;
}
int audio_find(const AudioEndpoints *e,const wchar_t *id,EDataFlow flow){for(int i=0;i<e->count;i++)if(e->items[i].flow==flow&&!wcscmp(e->items[i].id,id))return i;return -1;}
bool audio_endpoint_active(const wchar_t *id,EDataFlow flow){
    if(!id[0])return false;IMMDeviceEnumerator *e=enumerator();IMMDevice *d=NULL;bool ok=false;
    if(e&&SUCCEEDED(IMMDeviceEnumerator_GetDevice(e,id,&d))){DWORD state=0;IMMEndpoint *endpoint=NULL;EDataFlow actual=eAll;
        if(SUCCEEDED(IMMDevice_QueryInterface(d,&IID_IMMEndpoint,(void **)&endpoint))){IMMEndpoint_GetDataFlow(endpoint,&actual);IMMEndpoint_Release(endpoint);}
        ok=SUCCEEDED(IMMDevice_GetState(d,&state))&&state==DEVICE_STATE_ACTIVE&&actual==flow;IMMDevice_Release(d);}
    if(e)IMMDeviceEnumerator_Release(e);return ok;
}
bool audio_process_routed(const wchar_t *id,DWORD pid,int *other){
    bool found=false;*other=0;IMMDeviceEnumerator *e=enumerator();IMMDevice *d=NULL;IAudioSessionManager2 *manager=NULL;IAudioSessionEnumerator *sessions=NULL;
    if(e&&SUCCEEDED(IMMDeviceEnumerator_GetDevice(e,id,&d))&&SUCCEEDED(IMMDevice_Activate(d,&IID_IAudioSessionManager2,CLSCTX_INPROC_SERVER,NULL,(void **)&manager))&&SUCCEEDED(IAudioSessionManager2_GetSessionEnumerator(manager,&sessions))){
        int count=0;IAudioSessionEnumerator_GetCount(sessions,&count);for(int i=0;i<count;i++){IAudioSessionControl *control=NULL;IAudioSessionControl2 *control2=NULL;
            if(SUCCEEDED(IAudioSessionEnumerator_GetSession(sessions,i,&control))&&SUCCEEDED(IAudioSessionControl_QueryInterface(control,&IID_IAudioSessionControl2,(void **)&control2))){DWORD owner=0;AudioSessionState state=AudioSessionStateInactive;
                IAudioSessionControl2_GetProcessId(control2,&owner);IAudioSessionControl_GetState(control,&state);
                if(state==AudioSessionStateActive){if(owner==pid)found=true;else (*other)++;}IAudioSessionControl2_Release(control2);}if(control)IAudioSessionControl_Release(control);}
    }
    if(sessions)IAudioSessionEnumerator_Release(sessions);if(manager)IAudioSessionManager2_Release(manager);if(d)IMMDevice_Release(d);if(e)IMMDeviceEnumerator_Release(e);return found;
}
bool audio_meter_open(AudioMeter *m,const wchar_t *id,char *err,size_t cap){
    memset(m,0,sizeof(*m));if(!audio_endpoint_active(id,eCapture)){awm_copy(err,cap,"Selected recording endpoint is missing, disabled, unplugged, or not a recording device.");return false;}
    IMMDeviceEnumerator *e=enumerator();if(!e)return false;HRESULT hr=IMMDeviceEnumerator_GetDevice(e,id,&m->device);IMMDeviceEnumerator_Release(e);
    if(SUCCEEDED(hr))hr=IMMDevice_Activate(m->device,&IID_IAudioClient,CLSCTX_INPROC_SERVER,NULL,(void **)&m->client);
    if(SUCCEEDED(hr))hr=IAudioClient_GetMixFormat(m->client,&m->format);
    if(SUCCEEDED(hr))hr=IAudioClient_Initialize(m->client,AUDCLNT_SHAREMODE_SHARED,0,1000000,0,m->format,NULL);
    if(SUCCEEDED(hr))hr=IAudioClient_GetService(m->client,&IID_IAudioCaptureClient,(void **)&m->capture);
    if(SUCCEEDED(hr))hr=IAudioClient_Start(m->client);
    if(FAILED(hr)){hr_error(hr,err,cap);audio_meter_close(m);return false;}return true;
}
bool audio_meter_tick(AudioMeter *m,float *peak){
    if(!m->capture)return false;float max=0;UINT32 count=0;HRESULT hr;
    while(SUCCEEDED(hr=IAudioCaptureClient_GetNextPacketSize(m->capture,&count))&&count){BYTE *data=NULL;UINT32 frames=0;DWORD flags=0;
        hr=IAudioCaptureClient_GetBuffer(m->capture,&data,&frames,&flags,NULL,NULL);if(FAILED(hr))return false;
        bool floating=m->format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
        if(m->format->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&m->format->cbSize>=22){WAVEFORMATEXTENSIBLE *f=(WAVEFORMATEXTENSIBLE *)m->format;floating=IsEqualGUID(&f->SubFormat,&float_subtype)!=0;}
        if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)&&data){size_t samples=(size_t)frames*m->format->nChannels;int bits=m->format->wBitsPerSample;
            for(size_t i=0;i<samples;i++){float v=0;if(floating&&bits==32){float sample;memcpy(&sample,data+4*i,4);v=fabsf(sample);}else if(bits==16){int16_t sample;memcpy(&sample,data+2*i,2);v=fabsf((float)sample/32768.0f);}else if(bits==32){int32_t sample;memcpy(&sample,data+4*i,4);v=fabsf((float)sample/2147483648.0f);}else if(bits==24){const BYTE *b=data+3*i;int32_t sample=(int32_t)(b[0]|b[1]<<8|b[2]<<16);if(sample&0x800000)sample|=(int32_t)0xff000000;v=fabsf((float)sample/8388608.0f);}if(isfinite(v)&&v>max)max=v;}}
        hr=IAudioCaptureClient_ReleaseBuffer(m->capture,frames);if(FAILED(hr))return false;
    }
    if(FAILED(hr))return false;m->peak=max>1.0f?1.0f:max;*peak=m->peak;return true;
}
void audio_meter_close(AudioMeter *m){if(m->client)IAudioClient_Stop(m->client);if(m->capture)IAudioCaptureClient_Release(m->capture);if(m->format)CoTaskMemFree(m->format);if(m->client)IAudioClient_Release(m->client);if(m->device)IMMDevice_Release(m->device);memset(m,0,sizeof(*m));}
