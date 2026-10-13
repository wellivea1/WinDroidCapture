#include "device_profile.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
bool native_pixel_profile(const Device *d){
    if(!d)return false;char model[64];unsigned n=0;for(const unsigned char *p=(const unsigned char *)d->model;*p;p++)if(isalnum(*p)){if(n+1>=sizeof(model))return false;model[n++]=(char)tolower(*p);}model[n]=0;
    return !strcmp(model,"pixel10proxl")||!strcmp(model,"googlepixel10proxl");
}
const char *native_device_profile_name(const Device *d){return native_pixel_profile(d)?"Google Pixel 10 Pro XL":"Generic / reported camera capabilities";}
void native_camera_label(const Device *d,const Capabilities *caps,const Camera *c,char *out,size_t size){
    const char *direction=!strcmp(c->facing,"back")?"Rear":!strcmp(c->facing,"front")?"Front":"Camera";
    if(!c->physical){bool group=false;for(int i=0;i<caps->camera_count;i++)if(caps->cameras[i].physical&&!strcmp(caps->cameras[i].logical_id,c->id))group=true;
        snprintf(out,size,"%s%s - camera %s",direction,group?" automatic":"",c->id);return;}
    const char *lens="";bool crop=false;
    if(native_pixel_profile(d)&&!strcmp(c->facing,"back")&&isfinite(c->focal_mm)&&c->focal_mm>0){
        /* Known Pixel 10 Pro XL focal families. Match characteristics, never IDs.
           Virtual/crop members share a focal family and report a smaller sensor. */
        if(fabsf(c->focal_mm-2.02f)<.25f)lens="ultrawide";else if(fabsf(c->focal_mm-6.9f)<.7f)lens="wide";else if(fabsf(c->focal_mm-17.906f)<1.8f)lens="telephoto";
        if(c->sensor_width_mm>0&&isfinite(c->sensor_width_mm))for(int i=0;i<caps->camera_count;i++){const Camera *other=&caps->cameras[i];if(other->physical&&!strcmp(other->logical_id,c->logical_id)&&fabsf(other->focal_mm-c->focal_mm)<.05f&&other->sensor_width_mm>c->sensor_width_mm*1.4f)crop=true;}
    }
    if(lens[0])snprintf(out,size,"%s %s%s - physical %s",direction,lens,crop?" crop":"",c->physical_id);
    else snprintf(out,size,"%s physical %s via %s",direction,c->physical_id,c->logical_id);
}
