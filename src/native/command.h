#ifndef AWM_NATIVE_COMMAND_H
#define AWM_NATIVE_COMMAND_H
#include "capture_profile.h"
bool native_capture_args_profile(const wchar_t *exe,const wchar_t *pipe,const Device *d,const NativeCaptureProfile *p,const ToolFeatures *f,Arguments *a,char *error,size_t cap);
bool native_capture_args(const wchar_t *exe,const wchar_t *pipe,const Device *d,const Profile *p,const ToolFeatures *f,Arguments *a,char *error,size_t cap);
#endif
