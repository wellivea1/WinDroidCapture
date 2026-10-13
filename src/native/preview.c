#include "preview.h"
#include "composition.h"
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "scale_bytecode.h"
#define DROP(p) do{if(p){IUnknown_Release((IUnknown *)p);p=NULL;}}while(0)
struct NativePreview {HWND window;ID3D11Device *device;ID3D11DeviceContext *context;ID3D11DeviceContext1 *context1;IDXGISwapChain *swap;HANDLE latency;bool prepared;UINT swap_flags;ID3D11RenderTargetView *target,*inter_target;ID3D11VertexShader *vertex;ID3D11PixelShader *pixel,*color;ID3D11SamplerState *sampler,*linear;ID3D11Texture2D *texture,*inter,*output,*staging,*planes[3];ID3D11Texture1D *lut;ID3D11ShaderResourceView *view,*inter_view,*lut_view,*plane_views[3];ID3D11Buffer *params;UINT width,height,cw,ch,iw,ih,scaler,framing,yw,yh;bool yuv_nv12;};
void native_preview_set_scaler(NativePreview *p,unsigned s){if(p&&s<=2)p->scaler=s;}
void native_preview_set_framing(NativePreview *p,unsigned f){if(p&&f<=1)p->framing=f;}
static double jinc(double x){if(fabs(x)<1e-8)return 1;double v=3.14159265358979323846*x;return 2*_j1(v)/v;}
static bool target(NativePreview *p){ID3D11Texture2D *back=NULL;HRESULT hr=IDXGISwapChain_GetBuffer(p->swap,0,&IID_ID3D11Texture2D,(void **)&back);if(SUCCEEDED(hr))hr=ID3D11Device_CreateRenderTargetView(p->device,(ID3D11Resource *)back,NULL,&p->target);DROP(back);return SUCCEEDED(hr);}
static HRESULT create_swap(NativePreview *p){
    IDXGIDevice *dxgi=NULL;IDXGIAdapter *adapter=NULL;IDXGIFactory2 *factory=NULL;IDXGISwapChain1 *swap=NULL;IDXGISwapChain2 *swap2=NULL;
    HRESULT hr=ID3D11Device_QueryInterface(p->device,&IID_IDXGIDevice,(void **)&dxgi);if(SUCCEEDED(hr))hr=IDXGIDevice_GetAdapter(dxgi,&adapter);if(SUCCEEDED(hr))hr=IDXGIAdapter_GetParent(adapter,&IID_IDXGIFactory2,(void **)&factory);
    DXGI_SWAP_CHAIN_DESC1 d={0};d.Width=p->cw;d.Height=p->ch;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=2;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;d.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if(SUCCEEDED(hr))hr=IDXGIFactory2_CreateSwapChainForHwnd(factory,(IUnknown *)p->device,p->window,&d,NULL,NULL,&swap);
    if(SUCCEEDED(hr))hr=IDXGISwapChain1_QueryInterface(swap,&IID_IDXGISwapChain,(void **)&p->swap);if(SUCCEEDED(hr))hr=IDXGISwapChain1_QueryInterface(swap,&IID_IDXGISwapChain2,(void **)&swap2);
    if(SUCCEEDED(hr))hr=IDXGISwapChain2_SetMaximumFrameLatency(swap2,1);if(SUCCEEDED(hr)){p->latency=IDXGISwapChain2_GetFrameLatencyWaitableObject(swap2);if(!p->latency)hr=E_FAIL;}
    if(SUCCEEDED(hr)){p->swap_flags=d.Flags;IDXGIFactory2_MakeWindowAssociation(factory,p->window,DXGI_MWA_NO_ALT_ENTER);}
    DROP(swap2);DROP(swap);DROP(factory);DROP(adapter);DROP(dxgi);return hr;
}
bool native_preview_wait(NativePreview *p,HANDLE cancel,DWORD timeout){
    if(!p){SetLastError(ERROR_INVALID_PARAMETER);return false;}if(!p->swap||p->prepared)return true;
    HANDLE waits[]={p->latency,cancel};DWORD result=WaitForMultipleObjects(cancel?2:1,waits,FALSE,timeout);
    if(result==WAIT_OBJECT_0){p->prepared=true;return true;}if(result==WAIT_TIMEOUT){SetLastError(FAILED(ID3D11Device_GetDeviceRemovedReason(p->device))?ERROR_DEVICE_NOT_AVAILABLE:ERROR_TIMEOUT);}else if(result==WAIT_OBJECT_0+1)SetLastError(ERROR_CANCELLED);return false;
}
static bool present(NativePreview *p){if(!p->swap)return true;p->prepared=false;return SUCCEEDED(IDXGISwapChain_Present(p->swap,1,0));}
NativePreview *native_preview_create(HWND h){
    NativePreview *p=calloc(1,sizeof(*p));if(!p)return NULL;p->window=h;RECT r={0};if(h)GetClientRect(h,&r);p->cw=r.right?r.right:1;p->ch=r.bottom?r.bottom:1;
    HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,D3D11_CREATE_DEVICE_BGRA_SUPPORT,NULL,0,D3D11_SDK_VERSION,&p->device,NULL,&p->context);
    if(FAILED(hr))hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,D3D11_CREATE_DEVICE_BGRA_SUPPORT,NULL,0,D3D11_SDK_VERSION,&p->device,NULL,&p->context);
    if(SUCCEEDED(hr)){ID3D11DeviceContext_QueryInterface(p->context,&IID_ID3D11DeviceContext1,(void **)&p->context1);if(h)hr=create_swap(p);}
    if(SUCCEEDED(hr)){IDXGIDevice1 *dxgi=NULL;if(SUCCEEDED(ID3D11Device_QueryInterface(p->device,&IID_IDXGIDevice1,(void **)&dxgi))){IDXGIDevice1_SetMaximumFrameLatency(dxgi,1);DROP(dxgi);}if(p->swap){IDXGISwapChain3 *sw=NULL;if(SUCCEEDED(IDXGISwapChain_QueryInterface(p->swap,&IID_IDXGISwapChain3,(void **)&sw))){UINT support=0;if(SUCCEEDED(IDXGISwapChain3_CheckColorSpaceSupport(sw,DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,&support))&&(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))IDXGISwapChain3_SetColorSpace1(sw,DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);DROP(sw);}}}
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateVertexShader(p->device,awm_vertex_bytecode,sizeof(awm_vertex_bytecode),NULL,&p->vertex);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreatePixelShader(p->device,awm_pixel_bytecode,sizeof(awm_pixel_bytecode),NULL,&p->pixel);
    D3D11_SAMPLER_DESC sampler={0};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    D3D11_BUFFER_DESC cb={0};cb.ByteWidth=32;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(SUCCEEDED(hr))hr=ID3D11Device_CreateBuffer(p->device,&cb,NULL,&p->params);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateSamplerState(p->device,&sampler,&p->sampler);sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;if(SUCCEEDED(hr))hr=ID3D11Device_CreateSamplerState(p->device,&sampler,&p->linear);
    /* Same Jinc radius/window/sharpening parameters as mpv's ewa_lanczossharp.
     * Mathematical LUT generation; independent C/HLSL implementation.
     * Reference revision and licensing context: docs/RENDERING.md. */
    float lut[2048];double radius=3.2383154841662362*0.9812505837223707;
    for(int k=0;k<2048;k++){double x=radius*k/2047;lut[k]=(float)(jinc(x/0.9812505837223707)*jinc(x/radius*1.2196698912665045));}
    D3D11_TEXTURE1D_DESC td={0};td.Width=2048;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA initial={lut,sizeof(lut),0};
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateTexture1D(p->device,&td,&initial,&p->lut);if(SUCCEEDED(hr))hr=ID3D11Device_CreateShaderResourceView(p->device,(ID3D11Resource *)p->lut,NULL,&p->lut_view);
    if(FAILED(hr)||(h&&!target(p))){native_preview_destroy(p);return NULL;}return p;
}
bool native_preview_frame(NativePreview *p,const uint8_t *pixels,uint32_t w,uint32_t h){
    if(!p||!pixels||!w||!h||!native_preview_wait(p,NULL,500))return false;RECT r={0,0,(LONG)p->cw,(LONG)p->ch};if(p->window)GetClientRect(p->window,&r);if(!r.right||!r.bottom)return true;
    if(p->cw!=(UINT)r.right||p->ch!=(UINT)r.bottom){ID3D11DeviceContext_OMSetRenderTargets(p->context,0,NULL,NULL);DROP(p->target);p->cw=r.right;p->ch=r.bottom;if(FAILED(IDXGISwapChain_ResizeBuffers(p->swap,2,p->cw,p->ch,DXGI_FORMAT_UNKNOWN,p->swap_flags))||!target(p))return false;}
    if(p->width!=w||p->height!=h){DROP(p->view);DROP(p->texture);D3D11_TEXTURE2D_DESC d={0};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(ID3D11Device_CreateTexture2D(p->device,&d,NULL,&p->texture))||FAILED(ID3D11Device_CreateShaderResourceView(p->device,(ID3D11Resource *)p->texture,NULL,&p->view)))return false;p->width=w;p->height=h;}
    /* Like mpv's D3D11 upload strategy, declare full texture replacement so
       queued draws do not force preservation of the preceding image. */
    if(p->context1)ID3D11DeviceContext1_UpdateSubresource1(p->context1,(ID3D11Resource *)p->texture,0,NULL,pixels,w*4,0,D3D11_COPY_DISCARD);else ID3D11DeviceContext_UpdateSubresource(p->context,(ID3D11Resource *)p->texture,0,NULL,pixels,w*4,0);
    float bg[]={0,0,0,1};ID3D11DeviceContext_ClearRenderTargetView(p->context,p->target,bg);
    AwmComposition framing;if(!awm_composition(w,h,p->cw,p->ch,p->framing,&framing))return false;UINT vw=framing.width,vh=framing.height;
    ID3D11ShaderResourceView *none=NULL;ID3D11DeviceContext_PSSetShaderResources(p->context,0,1,&none);ID3D11DeviceContext_OMSetRenderTargets(p->context,0,NULL,NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(p->context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);ID3D11DeviceContext_VSSetShader(p->context,p->vertex,NULL,0);ID3D11DeviceContext_PSSetShader(p->context,p->pixel,NULL,0);ID3D11DeviceContext_PSSetSamplers(p->context,0,1,&p->sampler);ID3D11DeviceContext_PSSetSamplers(p->context,1,1,&p->linear);ID3D11DeviceContext_PSSetConstantBuffers(p->context,0,1,&p->params);ID3D11DeviceContext_VSSetConstantBuffers(p->context,0,1,&p->params);ID3D11DeviceContext_PSSetShaderResources(p->context,1,1,&p->lut_view);
    if(p->scaler!=AWM_SCALE_LANCZOS3){D3D11_VIEWPORT viewport={(float)framing.x,(float)framing.y,(float)vw,(float)vh,0,1};float axes[]={(float)w*framing.uw/vw,(float)h*framing.vh/vh,0,p->scaler==AWM_SCALE_EWA_LANCZOS?1.0f:2.0f,framing.u,framing.v,framing.uw,framing.vh};ID3D11DeviceContext_UpdateSubresource(p->context,(ID3D11Resource *)p->params,0,NULL,axes,0,0);ID3D11DeviceContext_RSSetViewports(p->context,1,&viewport);ID3D11DeviceContext_OMSetRenderTargets(p->context,1,&p->target,NULL);ID3D11DeviceContext_PSSetShaderResources(p->context,0,1,&p->view);ID3D11DeviceContext_Draw(p->context,4,0);return present(p);}
    if(p->iw!=vw||p->ih!=h){DROP(p->inter_target);DROP(p->inter_view);DROP(p->inter);D3D11_TEXTURE2D_DESC d={0};d.Width=vw;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;if(FAILED(ID3D11Device_CreateTexture2D(p->device,&d,NULL,&p->inter))||FAILED(ID3D11Device_CreateRenderTargetView(p->device,(ID3D11Resource *)p->inter,NULL,&p->inter_target))||FAILED(ID3D11Device_CreateShaderResourceView(p->device,(ID3D11Resource *)p->inter,NULL,&p->inter_view)))return false;p->iw=vw;p->ih=h;}
    ID3D11DeviceContext_IASetPrimitiveTopology(p->context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);ID3D11DeviceContext_VSSetShader(p->context,p->vertex,NULL,0);ID3D11DeviceContext_PSSetShader(p->context,p->pixel,NULL,0);ID3D11DeviceContext_PSSetSamplers(p->context,0,1,&p->sampler);ID3D11DeviceContext_PSSetConstantBuffers(p->context,0,1,&p->params);ID3D11DeviceContext_VSSetConstantBuffers(p->context,0,1,&p->params);
    D3D11_VIEWPORT horizontal={0,0,(float)vw,(float)h,0,1};float axis[]={1,0,(float)w*framing.uw/vw,0,framing.u,0,framing.uw,1};ID3D11DeviceContext_UpdateSubresource(p->context,(ID3D11Resource *)p->params,0,NULL,axis,0,0);ID3D11DeviceContext_RSSetViewports(p->context,1,&horizontal);ID3D11DeviceContext_OMSetRenderTargets(p->context,1,&p->inter_target,NULL);ID3D11DeviceContext_PSSetShaderResources(p->context,0,1,&p->view);ID3D11DeviceContext_Draw(p->context,4,0);ID3D11DeviceContext_PSSetShaderResources(p->context,0,1,&none);
    D3D11_VIEWPORT vertical={(float)framing.x,(float)framing.y,(float)vw,(float)vh,0,1};axis[0]=0;axis[1]=1;axis[2]=(float)h*framing.vh/vh;axis[4]=0;axis[5]=framing.v;axis[6]=1;axis[7]=framing.vh;ID3D11DeviceContext_UpdateSubresource(p->context,(ID3D11Resource *)p->params,0,NULL,axis,0,0);ID3D11DeviceContext_RSSetViewports(p->context,1,&vertical);ID3D11DeviceContext_OMSetRenderTargets(p->context,1,&p->target,NULL);ID3D11DeviceContext_PSSetShaderResources(p->context,0,1,&p->inter_view);ID3D11DeviceContext_Draw(p->context,4,0);return present(p);
}
static bool output_size(NativePreview *p,uint32_t dw,uint32_t dh){
    if(!p->output||p->cw!=dw||p->ch!=dh){ID3D11DeviceContext_OMSetRenderTargets(p->context,0,NULL,NULL);DROP(p->target);DROP(p->output);DROP(p->staging);D3D11_TEXTURE2D_DESC d={0};d.Width=dw;d.Height=dh;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(FAILED(ID3D11Device_CreateTexture2D(p->device,&d,NULL,&p->output))||FAILED(ID3D11Device_CreateRenderTargetView(p->device,(ID3D11Resource *)p->output,NULL,&p->target)))return false;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(ID3D11Device_CreateTexture2D(p->device,&d,NULL,&p->staging)))return false;p->cw=dw;p->ch=dh;}
    return true;
}
static bool readback(NativePreview *p,uint8_t *dest,uint32_t w,uint32_t h){
    ID3D11DeviceContext_CopyResource(p->context,(ID3D11Resource *)p->staging,(ID3D11Resource *)p->output);D3D11_MAPPED_SUBRESOURCE m={0};if(FAILED(ID3D11DeviceContext_Map(p->context,(ID3D11Resource *)p->staging,0,D3D11_MAP_READ,0,&m)))return false;for(UINT y=0;y<h;y++)memcpy(dest+(size_t)y*w*4,(BYTE *)m.pData+(size_t)y*m.RowPitch,w*4);ID3D11DeviceContext_Unmap(p->context,(ID3D11Resource *)p->staging,0);return true;
}
bool native_scale_bgra(NativePreview **slot,const uint8_t *source,uint32_t sw,uint32_t sh,uint8_t *dest,uint32_t dw,uint32_t dh){
    if(sw==dw&&sh==dh){memcpy(dest,source,(size_t)dw*dh*4);return true;}if(!*slot)*slot=native_preview_create(NULL);NativePreview *p=*slot;if(!p||p->window)return false;
    return output_size(p,dw,dh)&&native_preview_frame(p,source,sw,sh)&&readback(p,dest,dw,dh);
}
bool native_yuv_bgra(NativePreview **slot,const NativeYuvFrame *f,uint8_t *dest){
    if(!slot||!f)return false;uint32_t w=f->width,h=f->height;if(!dest||!f->plane[0]||!f->plane[1]||(!f->nv12&&!f->plane[2])||w<16||h<16||w>3840||h>3840||(uint64_t)w*h>3840u*2160u||(w&1)||(h&1)||f->stride[0]<(int)w||f->stride[1]<(int)(f->nv12?w:w/2)||(!f->nv12&&f->stride[2]<(int)w/2))return false;
    if(!*slot)*slot=native_preview_create(NULL);NativePreview *p=*slot;if(!p||p->window||!output_size(p,w,h))return false;
    if(!p->color&&FAILED(ID3D11Device_CreatePixelShader(p->device,awm_color_bytecode,sizeof(awm_color_bytecode),NULL,&p->color)))return false;
    ID3D11ShaderResourceView *none[3]={0};ID3D11DeviceContext_PSSetShaderResources(p->context,0,3,none);
    if(p->yw!=w||p->yh!=h||p->yuv_nv12!=f->nv12){
        for(unsigned i=0;i<3;i++){DROP(p->plane_views[i]);DROP(p->planes[i]);}
        for(unsigned i=0;i<(f->nv12?2u:3u);i++){
            D3D11_TEXTURE2D_DESC d={0};d.Width=i?w/2:w;d.Height=i?h/2:h;d.MipLevels=d.ArraySize=1;d.Format=i==1&&f->nv12?DXGI_FORMAT_R8G8_UNORM:DXGI_FORMAT_R8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(ID3D11Device_CreateTexture2D(p->device,&d,NULL,&p->planes[i]))||FAILED(ID3D11Device_CreateShaderResourceView(p->device,(ID3D11Resource *)p->planes[i],NULL,&p->plane_views[i])))return false;
        }
        p->yw=w;p->yh=h;p->yuv_nv12=f->nv12;
    }
    for(unsigned i=0;i<(f->nv12?2u:3u);i++){if(p->context1)ID3D11DeviceContext1_UpdateSubresource1(p->context1,(ID3D11Resource *)p->planes[i],0,NULL,f->plane[i],(UINT)f->stride[i],0,D3D11_COPY_DISCARD);else ID3D11DeviceContext_UpdateSubresource(p->context,(ID3D11Resource *)p->planes[i],0,NULL,f->plane[i],(UINT)f->stride[i],0);}
    float params[]={f->full_range?1.f:0.f,f->bt709?1.f:0.f,f->nv12?1.f:0.f,0,0,0,1,1};D3D11_VIEWPORT viewport={0,0,(float)w,(float)h,0,1};
    ID3D11DeviceContext_UpdateSubresource(p->context,(ID3D11Resource *)p->params,0,NULL,params,0,0);ID3D11DeviceContext_IASetPrimitiveTopology(p->context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);ID3D11DeviceContext_VSSetShader(p->context,p->vertex,NULL,0);ID3D11DeviceContext_VSSetConstantBuffers(p->context,0,1,&p->params);ID3D11DeviceContext_PSSetShader(p->context,p->color,NULL,0);ID3D11DeviceContext_PSSetConstantBuffers(p->context,0,1,&p->params);ID3D11DeviceContext_RSSetViewports(p->context,1,&viewport);ID3D11DeviceContext_OMSetRenderTargets(p->context,1,&p->target,NULL);ID3D11DeviceContext_PSSetShaderResources(p->context,0,3,p->plane_views);ID3D11DeviceContext_Draw(p->context,4,0);ID3D11DeviceContext_PSSetShaderResources(p->context,0,3,none);
    return readback(p,dest,w,h);
}
void native_preview_destroy(NativePreview *p){if(!p)return;if(p->context){ID3D11DeviceContext_ClearState(p->context);ID3D11DeviceContext_Flush(p->context);}if(p->latency)CloseHandle(p->latency);for(unsigned i=0;i<3;i++){DROP(p->plane_views[i]);DROP(p->planes[i]);}DROP(p->color);DROP(p->lut_view);DROP(p->lut);DROP(p->linear);DROP(p->staging);DROP(p->output);DROP(p->params);DROP(p->inter_target);DROP(p->inter_view);DROP(p->inter);DROP(p->view);DROP(p->texture);DROP(p->sampler);DROP(p->pixel);DROP(p->vertex);DROP(p->target);DROP(p->swap);DROP(p->context1);DROP(p->context);DROP(p->device);free(p);}
