#ifndef AWM_THREAD_CLASS_H
#define AWM_THREAD_CLASS_H
#include <windows.h>
#include <avrt.h>
/* Cooperative Windows multimedia scheduling, without changing global timer,
   power or registry settings. Unavailable tasks retain normal scheduling. */
static inline HANDLE awm_multimedia_begin(const wchar_t *task){DWORD index=0;return AvSetMmThreadCharacteristicsW(task,&index);}
static inline void awm_multimedia_end(HANDLE task){if(task)AvRevertMmThreadCharacteristics(task);}
#endif
