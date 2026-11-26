#ifndef AWM_COMPOSITION_H
#define AWM_COMPOSITION_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t x,y,width,height;float u,v,uw,vh;} AwmComposition;
bool awm_composition(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,unsigned framing,AwmComposition *out);
bool awm_composition_zoom(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,unsigned framing,float zoom,float pan_x,float pan_y,AwmComposition *out);
#endif
