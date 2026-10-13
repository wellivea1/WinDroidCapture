#ifndef AWM_TIMING_H
#define AWM_TIMING_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
/* Bounded, aggregate developer measurements. Never contains pixels, device
   identities or phone clock values. Intervals are local QPC milliseconds. */
typedef struct AwmTiming { double values[1024],sum,max; unsigned count; } AwmTiming;
static inline double awm_clock_ms(void){LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);return 1000.0*q.QuadPart/f.QuadPart;}
static inline void awm_timing_add(AwmTiming *t,double ms){if(ms<0||!isfinite(ms)||t->count>=1024)return;t->values[t->count++]=ms;t->sum+=ms;if(ms>t->max)t->max=ms;}
static inline int awm_timing_compare(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return x<y?-1:x>y?1:0;}
static inline void awm_timing_print(FILE *out,const char *name,AwmTiming *t){
    if(t->count){qsort(t->values,t->count,sizeof(double),awm_timing_compare);fprintf(out,"%s n=%u mean=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f ms\n",name,t->count,t->sum/t->count,t->values[t->count/2],t->values[(t->count-1)*95/100],t->values[(t->count-1)*99/100],t->max);}memset(t,0,sizeof(*t));
}
static inline FILE *awm_timing_open(const wchar_t *variable){wchar_t path[32768];DWORD n=GetEnvironmentVariableW(variable,path,32768);if(!n||n>=32768)return NULL;return _wfopen(path,L"a");}
/* Opt-in CSV uses the same monotonic PC clock in each process. Buffer writes
   and flush periodically, not once per frame. An anonymous per-session tag
   prevents joining frame numbers from different sessions. No phone IDs. */
static inline FILE *awm_csv_open(const wchar_t *variable,const char *header){
    FILE *out=awm_timing_open(variable);if(!out)return NULL;setvbuf(out,NULL,_IOFBF,65536);
    if(!fseek(out,0,SEEK_END)&&ftell(out)==0)fprintf(out,"%s\n",header);return out;
}
static inline FILE *awm_csv_companion(const wchar_t *variable,const wchar_t *suffix,const char *header){
    wchar_t path[32768];DWORD n=GetEnvironmentVariableW(variable,path,32768);if(!n||n+wcslen(suffix)>=32768)return NULL;
    wcscat_s(path,32768,suffix);FILE *out=_wfopen(path,L"a");if(!out)return NULL;setvbuf(out,NULL,_IOFBF,65536);if(!fseek(out,0,SEEK_END)&&ftell(out)==0)fprintf(out,"%s\n",header);return out;
}
static inline uint64_t awm_trace_session(const wchar_t *mapping){
    uint64_t tag=1469598103934665603ull;for(;*mapping;mapping++){tag^=(uint16_t)*mapping;tag*=1099511628211ull;}return tag;
}
#endif
