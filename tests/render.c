/* Actual D3D11 execution on synthetic pixels. Not a physical camera test. */
#include "../src/native/preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static uint8_t clamp(int x){return (uint8_t)(x<0?0:x>255?255:x);}
static void ref_bgra_to_nv12(const uint8_t *p,uint32_t w,uint32_t h,uint8_t *d){
    uint8_t *uv=d+(size_t)w*h;size_t row_bytes=(size_t)w*4;
    for(uint32_t y=0;y<h;y+=2){
        const uint8_t *r0=p+(size_t)y*row_bytes,*r1=r0+row_bytes;
        uint8_t *y0=d+(size_t)y*w,*y1=y0+w,*uv_row=uv+(size_t)(y>>1)*w;
        for(uint32_t x=0;x<w;x+=2){
            const uint8_t *q00=r0+(size_t)x*4,*q01=q00+4,*q10=r1+(size_t)x*4,*q11=q10+4;
            y0[x]=clamp(((47*q00[2]+157*q00[1]+16*q00[0]+128)>>8)+16);
            y0[x+1]=clamp(((47*q01[2]+157*q01[1]+16*q01[0]+128)>>8)+16);
            y1[x]=clamp(((47*q10[2]+157*q10[1]+16*q10[0]+128)>>8)+16);
            y1[x+1]=clamp(((47*q11[2]+157*q11[1]+16*q11[0]+128)>>8)+16);
            int b=(q00[0]+q01[0]+q10[0]+q11[0])>>2,g=(q00[1]+q01[1]+q10[1]+q11[1])>>2,r=(q00[2]+q01[2]+q10[2]+q11[2])>>2;
            uv_row[x]=clamp(((-26*r-87*g+112*b+128)>>8)+128);
            uv_row[x+1]=clamp(((112*r-102*g-10*b+128)>>8)+128);
        }
    }
}
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
    /* Verify GPU rotation (0/90/180/270) + mirror on an asymmetric 32x16 frame. */
    BYTE ry[32*16],ruv[32*8],base_bgra[32*16*4],rot_bgra[32*16*4];
    for(unsigned row=0;row<16;row++)for(unsigned x=0;x<32;x++)ry[row*32+x]=(BYTE)(20+x*5+row*4);
    for(unsigned row=0;row<8;row++)for(unsigned x=0;x<16;x++){ruv[row*32+x*2]=(BYTE)(64+x*7);ruv[row*32+x*2+1]=(BYTE)(192-row*11);}
    NativeYuvFrame rf={0};rf.plane[0]=ry;rf.plane[1]=ruv;rf.stride[0]=32;rf.stride[1]=32;rf.width=32;rf.height=16;rf.nv12=true;rf.bt709=true;
    if(!native_yuv_bgra(&gpu,&rf,base_bgra))return false;
    const int rots[]={0,90,180,270};
    for(unsigned ri=0;ri<4;ri++)for(unsigned mi=0;mi<2;mi++){
        int rot=rots[ri];bool mir=mi!=0;rf.rotate=rot;rf.mirror=mir;
        uint32_t ow=(rot==90||rot==270)?16:32;
        if(!native_yuv_bgra(&gpu,&rf,rot_bgra))return false;
        const uint32_t *src32=(const uint32_t *)base_bgra,*dst32=(const uint32_t *)rot_bgra;
        for(uint32_t sy=0;sy<16;sy++)for(uint32_t sx=0;sx<32;sx++){
            uint32_t dx=sx,dy=sy;
            if(rot==90){dx=15-sy;dy=sx;}else if(rot==180){dx=31-sx;dy=15-sy;}else if(rot==270){dx=sy;dy=31-sx;}
            if(mir)dx=ow-1-dx;
            if(dst32[dy*ow+dx]!=src32[sy*32+sx]){printf("FAIL GPU rotate=%d mirror=%d at src=%u,%u dst=%u,%u\n",rot,mir?1:0,sx,sy,dx,dy);return false;}
        }
    }
    f.stride[0]=31;if(native_yuv_bgra(&gpu,&f,out))return false;f.stride[0]=48;f.width=31;if(native_yuv_bgra(&gpu,&f,out))return false;
    BYTE *luma=malloc(1920u*1080),*chroma=malloc(1920u*540),*converted=malloc(1920u*1080*4);if(!luma||!chroma||!converted)return false;memset(luma,64,1920u*1080);memset(chroma,128,1920u*540);
    f.width=1920;f.height=1080;f.nv12=true;f.full_range=false;f.bt709=true;f.rotate=0;f.mirror=false;f.plane[0]=luma;f.plane[1]=chroma;f.stride[0]=f.stride[1]=1920;
    if(!native_yuv_bgra(&gpu,&f,converted)||converted[0]!=56||converted[1]!=56||converted[2]!=56)return false;LARGE_INTEGER frequency,begin,end;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);for(unsigned n=0;n<12;n++)if(!native_yuv_bgra(&gpu,&f,converted))return false;QueryPerformanceCounter(&end);
    printf("GPU synthetic NV12 -> BGRA 1920x1080 upload+conversion+readback %.3f ms mean over 12 warm frames; not hardware decode or phone latency\n",1000.0*(end.QuadPart-begin.QuadPart)/frequency.QuadPart/12);
    free(luma);free(chroma);free(converted);native_preview_destroy(gpu);puts("PASS GPU YUV: 8 format/range/matrix combinations, GPU rotate/mirror (8 transforms), padded strides, clipping, black/white endpoints, rejected invalid shapes/strides, resource resize");return true;
}
static bool nv12_scale_tests(void){
    NativePreview *gpu=native_preview_create(NULL);if(!gpu)return false;
    /* Exact-size GPU BGRA -> NV12 must match integer BT.709 CPU reference. */
    const uint32_t tw=64,th=48;
    BYTE *bgra=malloc((size_t)tw*th*4),*gpu_nv12=malloc((size_t)tw*th*3/2),*cpu_nv12=malloc((size_t)tw*th*3/2);
    if(!bgra||!gpu_nv12||!cpu_nv12){free(bgra);free(gpu_nv12);free(cpu_nv12);native_preview_destroy(gpu);return false;}
    for(uint32_t y=0;y<th;y++)for(uint32_t x=0;x<tw;x++){
        size_t i=((size_t)y*tw+x)*4;
        bgra[i]=(BYTE)((x*4+y*3)&255);bgra[i+1]=(BYTE)((x*3+y*5)&255);bgra[i+2]=(BYTE)((x*7+y*2)&255);bgra[i+3]=255;
    }
    ref_bgra_to_nv12(bgra,tw,th,cpu_nv12);
    if(!native_scale_nv12(&gpu,bgra,tw,th,gpu_nv12,tw,th)){printf("FAIL GPU native_scale_nv12 exact\n");return false;}
    for(size_t i=0;i<(size_t)tw*th*3/2;i++){
        if(abs((int)gpu_nv12[i]-(int)cpu_nv12[i])>1){printf("FAIL GPU NV12 exact mismatch at %zu gpu=%u cpu=%u\n",i,gpu_nv12[i],cpu_nv12[i]);return false;}
    }
    free(bgra);free(gpu_nv12);free(cpu_nv12);
    /* Downscaled 1920x1080 -> 1280x720 GPU scale + GPU NV12 packing. */
    BYTE *src=malloc(1920u*1080*4),*dst_nv12=malloc(1280u*720*3/2),*dst_bgra=malloc(1280u*720*4),*ref_nv12=malloc(1280u*720*3/2);
    if(!src||!dst_nv12||!dst_bgra||!ref_nv12)return false;
    for(size_t i=0;i<1920u*1080;i++){src[i*4]=63;src[i*4+1]=127;src[i*4+2]=191;src[i*4+3]=255;}
    native_preview_set_scaler(gpu,AWM_SCALE_EWA_LANCZOS);
    if(!native_scale_bgra(&gpu,src,1920,1080,dst_bgra,1280,720))return false;
    ref_bgra_to_nv12(dst_bgra,1280,720,ref_nv12);
    if(!native_scale_nv12(&gpu,src,1920,1080,dst_nv12,1280,720))return false;
    for(size_t i=0;i<1280u*720*3/2;i++){
        if(abs((int)dst_nv12[i]-(int)ref_nv12[i])>1){printf("FAIL GPU scaled NV12 mismatch at %zu gpu=%u ref=%u\n",i,dst_nv12[i],ref_nv12[i]);return false;}
    }
    LARGE_INTEGER freq,start,end;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);
    for(int k=0;k<12;k++)if(!native_scale_nv12(&gpu,src,1920,1080,dst_nv12,1280,720))return false;
    QueryPerformanceCounter(&end);
    printf("GPU synthetic 1920x1080 -> 1280x720 EWA+NV12 mean %.3f ms (12 frames; zero CPU NV12 loop)\n",1000.0*(end.QuadPart-start.QuadPart)/freq.QuadPart/12);
    /* Extreme >3.5x EWA downscale fallback (1920x1080 -> 320x180, 6x). */
    BYTE *tiny=malloc(320u*180*4);if(!tiny)return false;
    if(!native_scale_bgra(&gpu,src,1920,1080,tiny,320,180))return false;
    for(size_t i=0;i<320u*180;i++)if(abs(tiny[i*4]-63)>1||abs(tiny[i*4+1]-127)>1||abs(tiny[i*4+2]-191)>1){printf("FAIL adaptive EWA 6x downscale at %zu\n",i);return false;}
    free(tiny);free(src);free(dst_nv12);free(dst_bgra);free(ref_nv12);native_preview_destroy(gpu);
    puts("PASS GPU NV12 packing (exact + scaled) and adaptive EWA >3.5x downscale");return true;
}
int main(void){
    uint8_t *src=malloc(1920*1080*4),*dst=malloc(1280*720*4);if(!src||!dst)return 2;
    for(size_t i=0;i<1920*1080;i++){src[i*4]=63;src[i*4+1]=127;src[i*4+2]=191;src[i*4+3]=255;}
    LARGE_INTEGER freq,start,end,cold;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&cold);
    NativePreview *p=native_preview_create(NULL);if(!p){puts("FAIL D3D11 initialization/precompiled shader load");return 3;}
    for(unsigned filter=0;filter<4;filter++){
        native_preview_set_scaler(p,filter);if(!native_scale_bgra(&p,src,1920,1080,dst,1280,720))return 4;
        if(filter==0){QueryPerformanceCounter(&end);printf("GPU initialization plus first EWA render/readback %.3f ms; shaders compiled at build time.\n",1000.0*(end.QuadPart-cold.QuadPart)/freq.QuadPart);}
        for(size_t i=0;i<1280*720;i++)if(abs(dst[i*4]-63)>1||abs(dst[i*4+1]-127)>1||abs(dst[i*4+2]-191)>1){printf("FAIL filter %u color normalization at %zu\n",filter,i);return 5;}
        QueryPerformanceCounter(&start);for(int k=0;k<12;k++)if(!native_scale_bgra(&p,src,1920,1080,dst,1280,720))return 6;QueryPerformanceCounter(&end);
        printf("GPU synthetic 1920x1080 -> 1280x720, filter=%u, mean render+readback %.3f ms (12 frames); not end-to-end latency.\n",filter,1000.0*(end.QuadPart-start.QuadPart)/freq.QuadPart/12);
    }
    /* Verify orientation and filtering don't corrupt a contrasting region. */
    for(unsigned y=0;y<1080;y++)for(unsigned x=0;x<1920;x++){size_t i=((size_t)y*1920+x)*4;src[i]=x<960?20:230;src[i+1]=y<540?20:230;src[i+2]=80;}
    native_preview_set_scaler(p,0);if(!native_scale_bgra(&p,src,1920,1080,dst,1280,720))return 7;size_t a=(100*1280+100)*4,b=(600*1280+1100)*4;if(abs(dst[a]-20)>1||abs(dst[b]-230)>1||abs(dst[a+1]-20)>1||abs(dst[b+1]-230)>1)return 8;
    /* Verify exact-size (1920x1080 -> 1920x1080) digital zoom + pan on both BGRA and NV12 paths (must NOT take unzoomed memcpy bypass). */
    uint8_t *zoomed_bgra=malloc(1920u*1080*4),*zoomed_nv12=malloc(1920u*1080*3/2);if(!zoomed_bgra||!zoomed_nv12)return 11;
    native_preview_set_scaler(p,AWM_SCALE_NEAREST);native_preview_set_zoom(p,2.0f,0.0f,0.0f);
    if(!native_scale_bgra(&p,src,1920,1080,zoomed_bgra,1920,1080))return 12;
    /* Top-left quadrant is (B=20,G=20,R=80); at 2x zoom panned to (0,0), bottom-right output pixel (1800,1000) maps inside top-left quadrant (900,500)! */
    size_t zbr=(1000u*1920+1800)*4;if(abs(zoomed_bgra[zbr]-20)>1||abs(zoomed_bgra[zbr+1]-20)>1||abs(zoomed_bgra[zbr+2]-80)>1){printf("FAIL zoomed exact-size BGRA at (1800,1000): %u,%u,%u\n",zoomed_bgra[zbr],zoomed_bgra[zbr+1],zoomed_bgra[zbr+2]);return 13;}
    if(!native_scale_nv12(&p,src,1920,1080,zoomed_nv12,1920,1080))return 14;
    /* Top-left quadrant luma Y = ((47*80+157*20+16*20+128)>>8)+16 = 44; unzoomed bottom-right would be Y=179! */
    if(abs((int)zoomed_nv12[1000u*1920+1800]-44)>2){printf("FAIL zoomed exact-size NV12 at (1800,1000): Y=%u\n",zoomed_nv12[1000u*1920+1800]);return 15;}
    native_preview_set_zoom(p,1.0f,0.5f,0.5f);free(zoomed_bgra);free(zoomed_nv12);
    native_preview_destroy(p);free(src);free(dst);puts("PASS actual GPU execution: all 4 selectable filters, flat-field color preservation, region orientation, zoomed exact-size BGRA & NV12.");
    if(!yuv_tests())return 9;
    return nv12_scale_tests()?0:10;
}
