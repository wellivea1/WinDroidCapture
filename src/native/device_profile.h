#ifndef AWM_DEVICE_PROFILE_H
#define AWM_DEVICE_PROFILE_H
#include "../core.h"
/* Device profiles label reported hardware; identities always come from Camera2. */
const char *native_device_profile_name(const Device *device);
bool native_pixel_profile(const Device *device);
void native_camera_label(const Device *device,const Capabilities *capabilities,const Camera *camera,char *out,size_t size);
#endif
