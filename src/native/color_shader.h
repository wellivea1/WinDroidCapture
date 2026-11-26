#ifndef AWM_COLOR_SHADER_H
#define AWM_COLOR_SHADER_H
/* Independent plane-to-BGRA conversion (with optional clockwise rotation and
   horizontal mirroring) plus GPU BGRA-to-NV12 Y and UV packing shaders.
   Matches the established integer 8-bit BT.709/BT.601 range/matrix math. */
static const char awm_color_shader[]=
"cbuffer Params:register(b0){float4 config;float4 region;};"
"Texture2D<float> Y:register(t0);Texture2D<float2> U:register(t1);Texture2D<float> V:register(t2);"
"float4 convert(float4 pos:SV_POSITION):SV_TARGET{"
"uint sw,sh;Y.GetDimensions(sw,sh);"
"int flags=(int)round(config.w);int rot=flags&3;bool mir=(flags&4)!=0;"
"int ow=(rot==1||rot==3)?(int)sh:(int)sw;"
"int dx=(int)pos.x,dy=(int)pos.y;int rx=mir?(ow-1-dx):dx,ry=dy;"
"int2 at=int2(rx,ry);"
"if(rot==1)at=int2(ry,(int)sh-1-rx);"
"else if(rot==2)at=int2((int)sw-1-rx,(int)sh-1-ry);"
"else if(rot==3)at=int2((int)sw-1-ry,rx);"
"int2 chroma=at/2;"
"int yy=(int)round(Y.Load(int3(at,0))*255);float2 pair=U.Load(int3(chroma,0));"
"int u=(int)round(pair.r*255)-128;int v=(int)round((config.z>0?pair.g:V.Load(int3(chroma,0)))*255)-128;"
"bool full=config.x>0,bt709=config.y>0;int c=full?yy*256:298*(yy-16);"
"int r=c+(bt709?(full?403:459):(full?359:409))*v;"
"int g=c-(bt709?(full?48:55):(full?88:100))*u-(bt709?(full?120:136):(full?183:208))*v;"
"int b=c+(bt709?(full?475:541):(full?454:516))*u;"
"return float4(saturate(floor((float3(r,g,b)+128)/256)/255),1);}";
static const char awm_nv12_shader[]=
"Texture2D<float4> BgraSrc:register(t0);"
"float4 bgra_to_y(float4 pos:SV_POSITION):SV_TARGET{"
"int2 at=(int2)pos.xy;float4 px=BgraSrc.Load(int3(at,0));"
"int r=(int)round(px.r*255),g=(int)round(px.g*255),b=(int)round(px.b*255);"
"int y=((47*r+157*g+16*b+128)>>8)+16;"
"return float4(saturate((float)y/255.0),0,0,1);}"
"float4 bgra_to_uv(float4 pos:SV_POSITION):SV_TARGET{"
"int2 base=(int2)pos.xy*2;"
"float4 p00=BgraSrc.Load(int3(base,0)),p01=BgraSrc.Load(int3(base+int2(1,0),0));"
"float4 p10=BgraSrc.Load(int3(base+int2(0,1),0)),p11=BgraSrc.Load(int3(base+int2(1,1),0));"
"int r=((int)round(p00.r*255)+(int)round(p01.r*255)+(int)round(p10.r*255)+(int)round(p11.r*255))>>2;"
"int g=((int)round(p00.g*255)+(int)round(p01.g*255)+(int)round(p10.g*255)+(int)round(p11.g*255))>>2;"
"int b=((int)round(p00.b*255)+(int)round(p01.b*255)+(int)round(p10.b*255)+(int)round(p11.b*255))>>2;"
"int u=((-26*r-87*g+112*b+128)>>8)+128;int v=((112*r-102*g-10*b+128)>>8)+128;"
"return float4(saturate((float)u/255.0),saturate((float)v/255.0),0,1);}"
;
#endif
