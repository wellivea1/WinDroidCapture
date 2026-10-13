#include "composition.h"
#include <string.h>
bool awm_composition(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,unsigned framing,AwmComposition *r){
    if(!sw||!sh||!dw||!dh||sw>3840||sh>3840||dw>3840||dh>3840||framing>1)return false;
    memset(r,0,sizeof(*r));r->width=dw;r->height=dh;r->uw=r->vh=1;
    if(framing==0){
        if((uint64_t)dw*sh>(uint64_t)dh*sw)r->width=(uint32_t)((uint64_t)dh*sw/sh);else r->height=(uint32_t)((uint64_t)dw*sh/sw);
        if(!r->width)r->width=1;if(!r->height)r->height=1;r->x=(dw-r->width)/2;r->y=(dh-r->height)/2;
    }else{
        double sa=(double)sw/sh,da=(double)dw/dh;
        if(sa>da){r->uw=(float)(da/sa);r->u=(1-r->uw)/2;}else{r->vh=(float)(sa/da);r->v=(1-r->vh)/2;}
    }return true;
}
