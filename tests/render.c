/* Actual D3D11 execution on synthetic pixels. Not a physical camera test. */
#include "../src/native/preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static uint8_t clamp(int x){return (uint8_t)(x<0?0:x>255?255:x);}
static bool yuv_tests(void){
    NativePreview *gpu=NULL;BYTE y[48*32],u[48*16],v[32*16],out[32*32*4];
    NativeYuvFrame f={0};f.plane[0]=y;f.plane[1]=u;f.plane[2]=v;f.width=f.height=32;f.stride[0]=48;f.stride[1]=48;f.stride[2]=32;
    for(unsigned nv=0;nv<2;nv++)for(unsigned full=0;full<2;full++)for(unsigned bt=0;bt<2;bt++){
        memset(y,0xcc,sizeof(y));memset(u,0xcc,sizeof(u));memset(v,0xcc,sizeof(v));f.nv12=nv!=0;f.full_range=full!=0;f.bt709=bt!=0;
        for(unsigned row=0;row<32;row++)for(unsigned x=0;x<32;x++)y[row*48+x]=(BYTE)(x*7+row*3);
        for(unsigned row=0;row<16;row++)for(unsigned x=0;x<16;x++){u[row*48+x*(nv?2:1)]=(BYTE)(x*17+row*9);if(nv)u[row*48+x*2+1]=(BYTE)(x*9+row*17);else v[row*32+x]=(BYTE)(x*9+row*17);}
        if(!native_yuv_bgra(&gpu,&f,out))return false;
        /* Integer CPU reference plus all 8 format/range/matrix cases, padded
           rows, mixed chroma and saturation; tests actual compiled GPU code. */
        for(unsigned row=0;row<32;row++)for(unsigned x=0;x<32;x++){
            int yy=y[row*48+x],uu=u[row/2*48+x/2*(nv?2:1)]-128,vv=(nv?u[row/2*48+x/2*2+1]:v[row/2*32+x/2])-128,c=full?yy*256:298*(yy-16);
            int r=(c+(bt?(full?403:459):(full?359:409))*vv+128)>>8,g=(c-(bt?(full?48:55):(full?88:100))*uu-(bt?(full?120:136):(full?183:208))*vv+128)>>8,b=(c+(bt?(full?475:541):(full?454:516))*uu+128)>>8;BYTE *p=out+(row*32+x)*4;
            if(abs(p[0]-clamp(b))>1||abs(p[1]-clamp(g))>1||abs(p[2]-clamp(r))>1||p[3]!=255){printf("FAIL GPU YUV format=%u full=%u bt709=%u at=%u,%u\n",nv,full,bt,x,row);return false;}
        }
        /* Known range endpoints independently of the reference formula. */
        memset(y,full?0:16,sizeof(y));memset(u,128,sizeof(u));memset(v,128,sizeof(v));if(!native_yuv_bgra(&gpu,&f,out)||out[0]||out[1]||out[2])return false;memset(y,full?255:235,sizeof(y));if(!native_yuv_bgra(&gpu,&f,out)||out[0]!=255||out[1]!=255||out[2]!=255)return false;
    }
    f.stride[0]=31;if(native_yuv_bgra(&gpu,&f,out))return false;f.stride[0]=48;f.width=31;if(native_yuv_bgra(&gpu,&f,out))return false;
    BYTE *luma=malloc(1920u*1080),*chroma=malloc(1920u*540),*converted=malloc(1920u*1080*4);if(!luma||!chroma||!converted)return false;memset(luma,64,1920u*1080);memset(chroma,128,1920u*540);
    f.width=1920;f.height=1080;f.nv12=true;f.full_range=false;f.bt709=true;f.plane[0]=luma;f.plane[1]=chroma;f.stride[0]=f.stride[1]=1920;
    if(!native_yuv_bgra(&gpu,&f,converted)||converted[0]!=56||converted[1]!=56||converted[2]!=56)return false;LARGE_INTEGER frequency,begin,end;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);for(unsigned n=0;n<12;n++)if(!native_yuv_bgra(&gpu,&f,converted))return false;QueryPerformanceCounter(&end);
    printf("GPU synthetic NV12 -> BGRA 1920x1080 upload+conversion+readback %.3f ms mean over 12 warm frames; not hardware decode or phone latency\n",1000.0*(end.QuadPart-begin.QuadPart)/frequency.QuadPart/12);
    free(luma);free(chroma);free(converted);native_preview_destroy(gpu);puts("PASS GPU YUV: 8 format/range/matrix combinations, padded strides, clipping, black/white endpoints, rejected invalid shapes/strides, resource resize");return true;
}
int main(void){
    uint8_t *src=malloc(1920*1080*4),*dst=malloc(1280*720*4);if(!src||!dst)return 2;
    for(size_t i=0;i<1920*1080;i++){src[i*4]=63;src[i*4+1]=127;src[i*4+2]=191;src[i*4+3]=255;}
    LARGE_INTEGER freq,start,end,cold;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&cold);
    NativePreview *p=native_preview_create(NULL);if(!p){puts("FAIL D3D11 initialization/precompiled shader load");return 3;}
    for(unsigned filter=0;filter<3;filter++){
        native_preview_set_scaler(p,filter);if(!native_scale_bgra(&p,src,1920,1080,dst,1280,720))return 4;
        if(filter==0){QueryPerformanceCounter(&end);printf("GPU initialization plus first EWA render/readback %.3f ms; shaders compiled at build time.\n",1000.0*(end.QuadPart-cold.QuadPart)/freq.QuadPart);}
        for(size_t i=0;i<1280*720;i++)if(abs(dst[i*4]-63)>1||abs(dst[i*4+1]-127)>1||abs(dst[i*4+2]-191)>1){printf("FAIL filter %u color normalization at %zu\n",filter,i);return 5;}
        QueryPerformanceCounter(&start);for(int k=0;k<12;k++)if(!native_scale_bgra(&p,src,1920,1080,dst,1280,720))return 6;QueryPerformanceCounter(&end);
        printf("GPU synthetic 1920x1080 -> 1280x720, filter=%u, mean render+readback %.3f ms (12 frames); not end-to-end latency.\n",filter,1000.0*(end.QuadPart-start.QuadPart)/freq.QuadPart/12);
    }
    /* Verify orientation and filtering don't corrupt a contrasting region. */
    for(unsigned y=0;y<1080;y++)for(unsigned x=0;x<1920;x++){size_t i=((size_t)y*1920+x)*4;src[i]=x<960?20:230;src[i+1]=y<540?20:230;src[i+2]=80;}
    native_preview_set_scaler(p,0);if(!native_scale_bgra(&p,src,1920,1080,dst,1280,720))return 7;size_t a=(100*1280+100)*4,b=(600*1280+1100)*4;if(abs(dst[a]-20)>1||abs(dst[b]-230)>1||abs(dst[a+1]-20)>1||abs(dst[b+1]-230)>1)return 8;
    native_preview_destroy(p);free(src);free(dst);puts("PASS actual GPU execution: all selectable filters, flat-field color preservation, region orientation.");return yuv_tests()?0:9;
}
