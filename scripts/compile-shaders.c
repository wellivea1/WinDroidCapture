/* Build-time shader compilation: never runs in the capture/preview pipeline. */
#include <windows.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include "../src/native/scale_shader.h"
#include "../src/native/color_shader.h"
static HRESULT emit(FILE *file,const char *source,SIZE_T length,const char *entry,const char *profile,const char *name){
    ID3DBlob *shader=NULL,*errors=NULL;
    HRESULT hr=D3DCompile(source,length,name,NULL,NULL,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    if(FAILED(hr)){if(errors)fwrite(ID3D10Blob_GetBufferPointer(errors),1,ID3D10Blob_GetBufferSize(errors),stderr);}
    else{const BYTE *bytes=ID3D10Blob_GetBufferPointer(shader);SIZE_T size=ID3D10Blob_GetBufferSize(shader);fprintf(file,"static const unsigned char %s[]={\n",name);for(SIZE_T i=0;i<size;i++){fprintf(file,"0x%02x,",bytes[i]);if((i+1)%24==0)fputc('\n',file);}fputs("\n};\n",file);}
    if(errors)ID3D10Blob_Release(errors);if(shader)ID3D10Blob_Release(shader);return hr;
}
int wmain(int argc,wchar_t **argv){
    if(argc!=2)return 2;FILE *file=NULL;if(_wfopen_s(&file,argv[1],L"wb")||!file)return 3;
    fputs("/* Generated from scale_shader.h and color_shader.h; edit HLSL, not this file. */\n",file);
    HRESULT hr=emit(file,awm_scale_shader,sizeof(awm_scale_shader),"vs","vs_4_0","awm_vertex_bytecode");if(SUCCEEDED(hr))hr=emit(file,awm_scale_shader,sizeof(awm_scale_shader),"ps","ps_4_0","awm_pixel_bytecode");if(SUCCEEDED(hr))hr=emit(file,awm_color_shader,sizeof(awm_color_shader),"convert","ps_4_0","awm_color_bytecode");
    int error=ferror(file);fclose(file);if(FAILED(hr)||error){DeleteFileW(argv[1]);return 4;}return 0;
}
