#ifndef AWM_AUDIO_H
#define AWM_AUDIO_H
#include "platform.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#define AWM_ENDPOINTS 128
typedef struct { wchar_t id[1024], name[256]; DWORD state; EDataFlow flow; } AudioEndpoint;
typedef struct { AudioEndpoint items[AWM_ENDPOINTS]; int count; } AudioEndpoints;
typedef struct { IMMDevice *device; IAudioClient *client; IAudioCaptureClient *capture; WAVEFORMATEX *format; float peak; } AudioMeter;
bool audio_enumerate(AudioEndpoints *out,char *error,size_t cap);
int audio_find(const AudioEndpoints *out,const wchar_t *id,EDataFlow flow);
bool audio_endpoint_active(const wchar_t *id,EDataFlow flow);
bool audio_process_routed(const wchar_t *playback_id,DWORD pid,int *other_active);
bool audio_meter_open(AudioMeter *m,const wchar_t *recording_id,char *err,size_t cap);
bool audio_meter_tick(AudioMeter *m,float *peak);
void audio_meter_close(AudioMeter *m);
#endif
