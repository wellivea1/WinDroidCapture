#include "composition.h"
#include <math.h>
#include <string.h>
static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
bool awm_composition_zoom(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,unsigned framing,float zoom,float pan_x,float pan_y,AwmComposition *r){
    if(!sw||!sh||!dw||!dh||sw>3840||sh>3840||dw>3840||dh>3840||framing>1||!r)return false;
    if(!(zoom>=1.0f)||zoom>8.0f)zoom=zoom>8.0f?8.0f:1.0f;
    if(!(pan_x>=0.0f&&pan_x<=1.0f))pan_x=0.5f;
    if(!(pan_y>=0.0f&&pan_y<=1.0f))pan_y=0.5f;
    memset(r,0,sizeof(*r));r->width=dw;r->height=dh;r->uw=r->vh=1.0f;
    if(framing==0){
        if(zoom<=1.0001f){
            if((uint64_t)dw*sh>(uint64_t)dh*sw)r->width=(uint32_t)((uint64_t)dh*sw/sh);else r->height=(uint32_t)((uint64_t)dw*sh/sw);
            if(!r->width)r->width=1;if(!r->height)r->height=1;r->x=(dw-r->width)/2;r->y=(dh-r->height)/2;
            return true;
        }
        double sx=(double)dw/sw,sy=(double)dh/sh,s=(sx<sy?sx:sy)*(double)zoom;
        double full_w=(double)sw*s,full_h=(double)sh*s;
        uint32_t vw=full_w>=(double)dw?dw:(uint32_t)(full_w+0.5),vh=full_h>=(double)dh?dh:(uint32_t)(full_h+0.5);
        if(!vw)vw=1;if(!vh)vh=1;
        r->width=vw;r->height=vh;r->x=(dw-vw)/2;r->y=(dh-vh)/2;
        r->uw=clampf((float)(vw/full_w),0.001f,1.0f);
        r->vh=clampf((float)(vh/full_h),0.001f,1.0f);
    }else{
        double sa=(double)sw/sh,da=(double)dw/dh;
        double base_uw=sa>da?da/sa:1.0,base_vh=sa>da?1.0:sa/da;
        r->uw=clampf((float)(base_uw/zoom),0.001f,1.0f);
        r->vh=clampf((float)(base_vh/zoom),0.001f,1.0f);
    }
    r->u=clampf(pan_x-r->uw*0.5f,0.0f,1.0f-r->uw);
    r->v=clampf(pan_y-r->vh*0.5f,0.0f,1.0f-r->vh);
    return true;
}
bool awm_composition(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,unsigned framing,AwmComposition *r){
    return awm_composition_zoom(sw,sh,dw,dh,framing,1.0f,0.5f,0.5f,r);
}
