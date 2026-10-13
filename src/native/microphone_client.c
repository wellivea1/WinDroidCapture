#include "microphone_client.h"
#include <winioctl.h>
#include "../../drivers/microphone/awm_mic_ioctl.h"
#include <string.h>
bool awm_microphone_open(AwmMicrophone *m,uint32_t generation){
    memset(m,0,sizeof(*m));m->device=CreateFileW(L"\\\\.\\AWMPhoneMic",GENERIC_WRITE|GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL);
    if(m->device==INVALID_HANDLE_VALUE){m->device=NULL;return false;}m->generation=generation;AwmMicBegin b={AWM_MIC_VERSION,generation,AWM_MIC_RATE,AWM_MIC_CHANNELS,AWM_MIC_BITS};DWORD n;
    if(!DeviceIoControl(m->device,IOCTL_AWM_MIC_BEGIN,&b,sizeof(b),NULL,0,&n,NULL)){CloseHandle(m->device);m->device=NULL;return false;}return true;
}
bool awm_microphone_write(AwmMicrophone *m,const int16_t *p,uint32_t samples,int64_t pts){
    if(!m->device||!samples||samples>AWM_MIC_MAX_WRITE)return false;AwmMicWrite w={AWM_MIC_VERSION,m->generation,samples,0,pts};DWORD n=0;
    return DeviceIoControl(m->device,IOCTL_AWM_MIC_WRITE,&w,sizeof(w),(void *)p,samples*sizeof(*p),&n,NULL)!=0;
}
void awm_microphone_reset(AwmMicrophone *m){if(m->device){AwmMicBegin b={AWM_MIC_VERSION,m->generation,AWM_MIC_RATE,1,16};DWORD n;DeviceIoControl(m->device,IOCTL_AWM_MIC_RESET,&b,sizeof(b),NULL,0,&n,NULL);}}
void awm_microphone_close(AwmMicrophone *m){if(m->device){awm_microphone_reset(m);CloseHandle(m->device);}memset(m,0,sizeof(*m));}
