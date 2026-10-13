#ifndef AWM_COLOR_SHADER_H
#define AWM_COLOR_SHADER_H
/* Independent plane-to-BGRA conversion. Match the established integer 8-bit
   range/matrix math and chroma reconstruction; no nominal quality claim. */
static const char awm_color_shader[]=
"cbuffer Params:register(b0){float4 config;float4 region;};"
"Texture2D<float> Y:register(t0);Texture2D<float2> U:register(t1);Texture2D<float> V:register(t2);"
"float4 convert(float4 pos:SV_POSITION):SV_TARGET{int2 at=(int2)pos.xy;int2 chroma=at/2;"
"int yy=(int)round(Y.Load(int3(at,0))*255);float2 pair=U.Load(int3(chroma,0));"
"int u=(int)round(pair.r*255)-128;int v=(int)round((config.z>0?pair.g:V.Load(int3(chroma,0)))*255)-128;"
"bool full=config.x>0,bt709=config.y>0;int c=full?yy*256:298*(yy-16);"
"int r=c+(bt709?(full?403:459):(full?359:409))*v;"
"int g=c-(bt709?(full?48:55):(full?88:100))*u-(bt709?(full?120:136):(full?183:208))*v;"
"int b=c+(bt709?(full?475:541):(full?454:516))*u;"
"return float4(saturate(floor((float3(r,g,b)+128)/256)/255),1);}"
;
#endif
