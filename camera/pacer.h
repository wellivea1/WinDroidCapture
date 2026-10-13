#ifndef AWM_CAMERA_PACER_H
#define AWM_CAMERA_PACER_H
#include <windows.h>
/* QPC deadlines, in 100 ns units, are independent of wall-clock adjustments.
   Sample timestamps remain in Media Foundation's clock domain. */
static inline LONGLONG awm_pacer_now(void){
    LARGE_INTEGER counter,frequency;QueryPerformanceCounter(&counter);QueryPerformanceFrequency(&frequency);
    return counter.QuadPart/frequency.QuadPart*10000000+(counter.QuadPart%frequency.QuadPart)*10000000/frequency.QuadPart;
}
/* 1: due, 0: cancelled, -1: timer error. No millisecond rounding or busy spin. */
static inline int awm_pacer_wait(HANDLE timer,HANDLE cancel,LONGLONG due){
    if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)return 0;
    LONGLONG delay=due-awm_pacer_now();if(delay<=0)return 1;
    LARGE_INTEGER relative;relative.QuadPart=-delay;
    if(!SetWaitableTimerEx(timer,&relative,0,NULL,NULL,NULL,0))return -1;
    HANDLE waits[]={cancel,timer};DWORD result=WaitForMultipleObjects(2,waits,FALSE,INFINITE);
    if(result==WAIT_OBJECT_0)return 0;return result==WAIT_OBJECT_0+1?1:-1;
}
#endif
