#include "mic_ring.h"
#ifdef AWM_KERNEL
#include <wdm.h>
#else
#include <string.h>
#endif
void awm_ring_reset(AwmMicRing *r){memset(r->samples,0,sizeof(r->samples));r->head=r->count=r->active=0;r->last_write_ms=0;r->last_pts_us=-1;}
void awm_ring_begin(AwmMicRing *r,uint32_t generation){awm_ring_reset(r);r->generation=generation;r->active=1;}
int awm_ring_write(AwmMicRing *r,uint32_t generation,const int16_t *p,uint32_t n,int64_t pts,uint64_t now){
    if(!r->active||generation!=r->generation||!p||!n||n>AWM_RING_SAMPLES||pts<0||pts<r->last_pts_us)return 0;
    if(r->count+n>AWM_RING_SAMPLES){r->overruns+=r->count;r->head=r->count=0;}
    for(uint32_t i=0;i<n;i++)r->samples[(r->head+r->count+i)%AWM_RING_SAMPLES]=p[i];
    r->count+=n;r->last_pts_us=pts;r->last_write_ms=now;r->written+=n;return 1;
}
void awm_ring_read(AwmMicRing *r,int16_t *p,uint32_t n,uint64_t now){
    if(!r->active||!r->last_write_ms||now-r->last_write_ms>250){if(r->count){memset(r->samples,0,sizeof(r->samples));r->head=r->count=0;}}
    for(uint32_t i=0;i<n;i++){if(r->active&&r->count){p[i]=r->samples[r->head];r->samples[r->head]=0;r->head=(r->head+1)%AWM_RING_SAMPLES;r->count--;}else{p[i]=0;r->underruns++;}}
    r->delivered+=n;
}
