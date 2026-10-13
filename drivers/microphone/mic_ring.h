#ifndef AWM_MIC_RING_H
#define AWM_MIC_RING_H
#ifdef AWM_KERNEL
typedef unsigned int uint32_t;
typedef unsigned __int64 uint64_t;
typedef short int16_t;
typedef __int64 int64_t;
#else
#include <stdint.h>
#endif
#define AWM_RING_SAMPLES 9600u
typedef struct AwmMicRing {
    int16_t samples[AWM_RING_SAMPLES];
    uint32_t head,count,generation,active;
    uint64_t last_write_ms,written,delivered,underruns,overruns;
    int64_t last_pts_us;
} AwmMicRing;
void awm_ring_begin(AwmMicRing *ring,uint32_t generation);
void awm_ring_reset(AwmMicRing *ring);
int awm_ring_write(AwmMicRing *ring,uint32_t generation,const int16_t *samples,uint32_t count,int64_t pts,uint64_t now_ms);
void awm_ring_read(AwmMicRing *ring,int16_t *samples,uint32_t count,uint64_t now_ms);
#endif
