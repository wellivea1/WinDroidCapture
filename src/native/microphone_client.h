#ifndef AWM_MICROPHONE_CLIENT_H
#define AWM_MICROPHONE_CLIENT_H
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct AwmMicrophone {HANDLE device;uint32_t generation;} AwmMicrophone;
bool awm_microphone_open(AwmMicrophone *mic,uint32_t generation);
bool awm_microphone_write(AwmMicrophone *mic,const int16_t *samples,uint32_t count,int64_t pts_us);
void awm_microphone_reset(AwmMicrophone *mic);
void awm_microphone_close(AwmMicrophone *mic);
#endif
