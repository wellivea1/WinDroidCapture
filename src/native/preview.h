#ifndef AWM_NATIVE_PREVIEW_H
#define AWM_NATIVE_PREVIEW_H
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct NativePreview NativePreview;
NativePreview *native_preview_create(HWND window);
bool native_preview_wait(NativePreview *p,HANDLE cancel,DWORD timeout_ms);
enum { AWM_SCALE_EWA_LANCZOS=0,AWM_SCALE_LANCZOS3=1,AWM_SCALE_BILINEAR=2 };
void native_preview_set_scaler(NativePreview *p,unsigned scaler);
void native_preview_set_framing(NativePreview *p,unsigned framing);
bool native_preview_frame(NativePreview *p,const uint8_t *pixels,uint32_t width,uint32_t height);
void native_preview_destroy(NativePreview *p);
/* Offscreen GPU resampling for camera consumers, independent of any UI window. */
bool native_scale_bgra(NativePreview **p,const uint8_t *source,uint32_t sw,uint32_t sh,uint8_t *dest,uint32_t dw,uint32_t dh);
typedef struct NativeYuvFrame {const uint8_t *plane[3];int stride[3];uint32_t width,height;bool nv12,full_range,bt709;} NativeYuvFrame;
/* CPU-decoded 8-bit planes uploaded separately, converted on D3D11, read back
   once for the existing shared BGRA consumers. This is not hardware decoding. */
bool native_yuv_bgra(NativePreview **p,const NativeYuvFrame *frame,uint8_t *dest);
#endif
