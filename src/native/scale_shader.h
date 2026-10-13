#ifndef AWM_SCALE_SHADER_H
#define AWM_SCALE_SHADER_H
/* Separable Lanczos-3, widened during downsampling, floating-point intermediate.
 * The bounded 64-tap support covers the maximum supported 4K -> preview ratio.
 * This is a quality-oriented filter, not a claim that one filter wins every image. */
static const char awm_scale_shader[]=
"struct V{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};"
"cbuffer Params:register(b0){float4 axis;float4 region;};"
"V vs(uint id:SV_VertexID){V v;float2 base=float2(id&1,id>>1);v.uv=region.xy+base*region.zw;v.p=float4(base.x*2-1,1-base.y*2,0,1);return v;}"
"Texture2D image:register(t0);Texture1D<float> kernel:register(t1);SamplerState filter:register(s0);SamplerState linear_filter:register(s1);"
"float lanczos(float x){x=abs(x);if(x<0.00001)return 1;if(x>=3)return 0;float p=3.14159265359*x;return sin(p)*sin(p/3)/(p*p/3);}"
"float4 ps(V v):SV_TARGET{if(axis.w==2)return float4(image.SampleLevel(linear_filter,v.uv,0).rgb,1);uint w,h;image.GetDimensions(w,h);float2 size=float2(w,h);float2 pos=v.uv*size-0.5;"
"if(axis.w==1){float2 base=floor(pos);float2 scale=max(float2(1,1),axis.xy);float radius=3.2383154841662362*0.9812505837223707;"
"int2 bound=min(int2(32,32),(int2)ceil(radius*scale));float3 sum=0;float total=0;float3 lo=1,hi=0;"
"[loop]for(int yy=-bound.y+1;yy<=bound.y;yy++){[loop]for(int xx=-bound.x+1;xx<=bound.x;xx++){float2 at=base+float2(xx,yy);float d=length((pos-at)/scale);if(d>=radius)continue;"
"float weight=kernel.SampleLevel(linear_filter,(d/radius*2047+0.5)/2048,0);float3 pixel=image.SampleLevel(filter,(at+0.5)/size,0).rgb;sum+=weight*pixel;total+=weight;if(xx>=0&&xx<=1&&yy>=0&&yy<=1){lo=min(lo,pixel);hi=max(hi,pixel);}}}"
"float3 result=sum/max(total,0.00001);return float4(lerp(result,clamp(result,lo,hi),0.7),1);}"
"float at=dot(pos,axis.xy);float base=floor(at);float scale=max(1,axis.z);int radius=min(32,(int)ceil(3*scale));float4 sum=0;float weight=0;"
"[loop]for(int i=-radius+1;i<=radius;i++){float sample_at=base+i;float tap=lanczos((at-sample_at)/scale);"
"float2 uv=(pos+axis.xy*(sample_at-at)+0.5)/size;sum+=image.SampleLevel(filter,uv,0)*tap;weight+=tap;}"
"return float4((sum/max(weight,0.00001)).rgb,1);}";
#endif
