/* Synthetic capability/transport/configuration tests. No phone or receiver assertions. */
#include "../src/native/command.h"
#include "../src/native/settings.h"
#include "../src/native/frame_hub.h"
#include "../src/native/composition.h"
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned checks,failures;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);failures++;}}while(0)
static bool fixture(const NativeSettings *s,DWORD bytes,const wchar_t *path){
    DATA_BLOB input={bytes,(BYTE *)s},output={0};if(!CryptProtectData(&input,L"Synthetic settings fixture",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&output))return false;
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD written=0;bool ok=f!=INVALID_HANDLE_VALUE&&WriteFile(f,output.pbData,output.cbData,&written,NULL)&&written==output.cbData;
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);LocalFree(output.pbData);return ok;
}
int main(void){
    ToolFeatures f;parse_features("--video-source= --camera-fps= --list-displays --max-fps= --capture-orientation= --no-downsize-on-error --require-audio --audio-source=mic-unprocessed","pair mdns services",&f);
    CHECK(f.list_displays&&f.max_fps&&f.capture_orientation);CHECK(!f.window_size&&!f.display_orientation);
    DisplayList displays;CHECK(native_parse_displays("INFO: List of displays:\n --display-id=0 (1344x2992)\n --display-id=2 (unknown size)\n --display-id=0 (999x999)\n --display-id=-1\n --display-id=70000",&displays));
    CHECK(displays.count==2&&displays.items[0].size_known&&displays.items[0].width==1344&&displays.items[1].id==2&&!displays.items[1].size_known);
    CHECK(!native_parse_displays("unrecognized tool error",&displays));CHECK(native_parse_displays("No displays",&displays)&&!displays.count);
    native_parse_displays("--display-id=0 (1344x2992)",&displays);
    Device d={0};d.sdk=37;d.state=DEV_AUTHORIZED;d.transport=TRANSPORT_USB;awm_copy(d.serial,sizeof(d.serial),"EXPLICIT_SECOND_PHONE");awm_copy(d.transport_id,sizeof(d.transport_id),"8");
    NativeCaptureProfile p;native_capture_default(&p,CAPTURE_SCREEN,0);CHECK(!p.audio&&!p.mirror&&p.screen.display_id==-1);p.screen.display_id=0;
    char error[1024];CHECK(native_capture_validate(&p,&d,&f,error,sizeof(error)));
    Arguments *args=calloc(1,sizeof(*args));wchar_t *command=calloc(32768,sizeof(*command));CHECK(args&&command);
    CHECK(native_capture_args_profile(L"C:\\tools with spaces\\日本語\\scrcpy-awm.exe",L"\\\\.\\pipe\\AWM.Export.synthetic",&d,&p,&f,args,error,sizeof(error))&&quote_windows_args(args,command,32768));
    CHECK(wcsstr(command,L"EXPLICIT_SECOND_PHONE")&&wcsstr(command,L"--video-source=display")&&wcsstr(command,L"--display-id=0"));
    CHECK(wcsstr(command,L"--max-size=1920")&&wcsstr(command,L"--max-fps=30")&&wcsstr(command,L"--no-audio"));
    CHECK(!wcsstr(command,L"--camera-id")&&!wcsstr(command,L"--camera-fps")&&!wcsstr(command,L"--camera-size")&&!wcsstr(command,L"--capture-orientation"));
    CHECK(!wcsstr(command,L"--window-width")&&wcsstr(command,L"--no-window")&&wcsstr(command,L"--no-playback"));
    p.screen.lock_orientation=true;CHECK(native_capture_args_profile(L"x",L"\\\\.\\pipe\\AWM.Export.synthetic",&d,&p,&f,args,error,sizeof(error))&&quote_windows_args(args,command,32768)&&wcsstr(command,L"--capture-orientation=@"));
    f.capture_orientation=false;CHECK(!native_capture_validate(&p,&d,&f,error,sizeof(error)));p.screen.lock_orientation=false;CHECK(native_capture_validate(&p,&d,&f,error,sizeof(error)));
    d.sdk=21;d.transport=TRANSPORT_EMULATOR;CHECK(native_capture_validate(&p,&d,&f,error,sizeof(error)));d.state=DEV_UNAUTHORIZED;CHECK(!native_capture_validate(&p,&d,&f,error,sizeof(error)));d.state=DEV_AUTHORIZED;
    p.screen.max_size=5000;CHECK(!native_capture_validate(&p,&d,&f,error,sizeof(error)));p.screen.max_size=1920;p.screen.fps_limit=0;CHECK(!native_capture_validate(&p,&d,&f,error,sizeof(error)));p.screen.fps_limit=30;
    p.audio=true;f.require_audio=false;CHECK(!native_capture_validate(&p,&d,&f,error,sizeof(error)));f.require_audio=true;p.audio=false;
    Capabilities *caps=calloc(1,sizeof(*caps));CHECK(native_capture_declared(&p,caps,&displays,error,sizeof(error)));p.screen.display_id=4;CHECK(!native_capture_declared(&p,caps,&displays,error,sizeof(error)));p.screen.display_id=0;
    awm_copy(p.encoding.encoder,sizeof(p.encoding.encoder),"not-enumerated");CHECK(!native_capture_declared(&p,caps,&displays,error,sizeof(error)));p.encoding.encoder[0]=0;
    native_capture_default(&p,CAPTURE_SCREEN,1);p.screen.display_id=0;CHECK(p.screen.max_size==0&&!strcmp(p.encoding.codec,"h265"));displays.items[0].height=5000;CHECK(!native_capture_declared(&p,caps,&displays,error,sizeof(error)));displays.items[0].height=2992;CHECK(native_capture_declared(&p,caps,&displays,error,sizeof(error)));
    native_capture_default(&p,CAPTURE_SCREEN,2);p.screen.display_id=0;CHECK(p.screen.fps_limit==60&&p.encoding.bitrate_mbps==24);
    NativeCaptureProfile camera;native_capture_default(&camera,CAPTURE_CAMERA,0);CHECK(!camera.audio);awm_copy(camera.camera.id,sizeof(camera.camera.id),"5");CHECK(!native_capture_validate(&camera,&d,&f,error,sizeof(error)));
    d.sdk=37;d.transport=TRANSPORT_USB;f.camera=f.camera_fps=true;CHECK(native_capture_validate(&camera,&d,&f,error,sizeof(error)));awm_copy(camera.camera.facing,sizeof(camera.camera.facing),"back");CHECK(!native_capture_validate(&camera,&d,&f,error,sizeof(error)));camera.camera.facing[0]=0;
    AwmComposition fit,fill,zoomed,panned;CHECK(awm_composition(862,1920,1920,1080,FRAME_FIT,&fit));CHECK(fit.height==1080&&fit.width<500&&fit.x>700&&fit.u==0&&fit.uw==1);
    CHECK(awm_composition(862,1920,1920,1080,FRAME_FILL,&fill));CHECK(fill.width==1920&&fill.height==1080&&fill.v>0&&fill.vh<0.3f&&fill.u==0);
    /* Zoom + pan composition checks: exact 2x crop, edge clamping, portrait FIT expansion, FILL vertical pan. */
    CHECK(awm_composition_zoom(1920,1080,1920,1080,FRAME_FIT,2.0f,0.5f,0.5f,&zoomed)&&zoomed.width==1920&&zoomed.height==1080&&fabsf(zoomed.uw-0.5f)<0.001f&&fabsf(zoomed.vh-0.5f)<0.001f&&fabsf(zoomed.u-0.25f)<0.001f&&fabsf(zoomed.v-0.25f)<0.001f);
    CHECK(awm_composition_zoom(1920,1080,1920,1080,FRAME_FIT,2.0f,0.0f,1.0f,&panned)&&fabsf(panned.u)<0.001f&&fabsf(panned.v-0.5f)<0.001f);
    CHECK(awm_composition_zoom(862,1920,1920,1080,FRAME_FIT,2.0f,0.5f,0.5f,&zoomed)&&zoomed.width>fit.width&&zoomed.width<1920&&zoomed.height==1080&&fabsf(zoomed.vh-0.5f)<0.001f);
    CHECK(awm_composition_zoom(862,1920,1920,1080,FRAME_FIT,4.0f,0.5f,0.5f,&zoomed)&&zoomed.width==1920&&zoomed.height==1080&&fabsf(zoomed.vh-0.25f)<0.001f);
    CHECK(awm_composition_zoom(862,1920,1920,1080,FRAME_FILL,1.0f,0.5f,0.0f,&panned)&&fabsf(panned.v)<0.001f);
    CHECK(awm_composition_zoom(862,1920,1920,1080,FRAME_FILL,1.0f,0.5f,1.0f,&panned)&&fabsf(panned.v-(1.0f-panned.vh))<0.001f);
    CHECK(!awm_composition(0,1920,1920,1080,0,&fit)&&!awm_composition(1920,1080,1920,1080,99,&fit));
    NativeSettings *saved=calloc(1,sizeof(*saved)),*loaded=calloc(1,sizeof(*loaded));saved->magic=0x324D5741;saved->version=6;saved->output_width=1920;saved->output_height=1080;saved->output_fps=60;saved->preview_scaler=3;saved->pipeline_scaler=3;saved->ui_theme=2;
    awm_wcopy(saved->adb,AWM_PATH,L"C:\\spaces\\日本語\\adb.exe");native_capture_set(saved,"DEVICE_ONE",&camera);native_capture_set(saved,"DEVICE_ONE",&p);native_capture_set(saved,"DEVICE_TWO",&camera);
    wchar_t tmp[AWM_PATH],path[AWM_PATH],backup[AWM_PATH];GetTempPathW(AWM_PATH,tmp);swprintf(path,AWM_PATH,L"%sAWM-screen-tests-%lu.dpapi",tmp,GetCurrentProcessId());
    CHECK(native_settings_save(saved,path)&&native_settings_load(loaded,path));CHECK(loaded->ui_theme==2&&loaded->output_fps==60&&loaded->preview_scaler==3&&loaded->pipeline_scaler==3&&!wcscmp(loaded->adb,saved->adb));
    saved->preview_scaler=1;saved->pipeline_scaler=2;CHECK(native_settings_save(saved,path)&&native_settings_load(loaded,path));
    NativeCaptureProfile read;CHECK(native_capture_get(loaded,"DEVICE_ONE",CAPTURE_SCREEN,&read)&&read.screen.fps_limit==60&&!read.audio);CHECK(native_capture_get(loaded,"DEVICE_ONE",CAPTURE_CAMERA,&read)&&!strcmp(read.camera.id,"5"));CHECK(native_last_kind(loaded,"DEVICE_ONE")==CAPTURE_SCREEN&&native_last_kind(loaded,"DEVICE_TWO")==CAPTURE_CAMERA);
    saved->ui_theme=9;CHECK(native_settings_save(saved,path)&&!native_settings_load(loaded,path)&&loaded->ui_theme==2);saved->ui_theme=0;saved->version=4;
    CHECK(fixture(saved,(DWORD)offsetof(NativeSettings,ui_theme),path)&&native_settings_load(loaded,path));CHECK(loaded->version==6&&loaded->ui_theme==0&&loaded->output_fps==60&&native_capture_get(loaded,"DEVICE_ONE",CAPTURE_SCREEN,&read));
    swprintf(backup,AWM_PATH,L"%s.pre-v5.bak",path);CHECK(GetFileAttributesW(backup)!=INVALID_FILE_ATTRIBUTES);DeleteFileW(backup);saved->version=3;
    CHECK(fixture(saved,(DWORD)offsetof(NativeSettings,output_width),path)&&native_settings_load(loaded,path));CHECK(loaded->version==6&&loaded->preview_scaler==1&&loaded->pipeline_scaler==2&&loaded->output_fps==30);swprintf(backup,AWM_PATH,L"%s.pre-v4.bak",path);DeleteFileW(backup);
    saved->version=5;saved->ui_theme=2;saved->video_buffer=0;CHECK(fixture(saved,(DWORD)AWM_SETTINGS_V5_BYTES,path)&&native_settings_load(loaded,path));CHECK(loaded->version==6&&loaded->video_buffer==1&&loaded->ui_theme==2&&loaded->output_fps==60);swprintf(backup,AWM_PATH,L"%s.pre-v6.bak",path);CHECK(GetFileAttributesW(backup)!=INVALID_FILE_ATTRIBUTES);DeleteFileW(backup);
    saved->version=6;saved->video_buffer=0;CHECK(fixture(saved,sizeof(*saved),path)&&native_settings_load(loaded,path)&&loaded->video_buffer==0);saved->video_buffer=2;CHECK(fixture(saved,sizeof(*saved),path)&&!native_settings_load(loaded,path)&&loaded->video_buffer==0);
    saved->version=7;CHECK(fixture(saved,sizeof(*saved),path)&&!native_settings_load(loaded,path)&&loaded->version==6);DeleteFileW(path);
    AwmHub writer={0},reader={0};wchar_t map[200];swprintf(map,200,L"Local\\AWM.Synthetic.Screen.%lu",GetCurrentProcessId());CHECK(awm_hub_open_named(&writer,true,map)&&awm_hub_open_named(&reader,false,map));BYTE pixels[16*16*4],copy[sizeof(pixels)];memset(pixels,73,sizeof(pixels));CHECK(awm_hub_publish(&writer,pixels,16,16,123));
    uint64_t now=GetTickCount64();int slot=(int)writer.view->published;writer.view->source_kind=CAPTURE_SCREEN;writer.view->slots[slot].heartbeat_ms=now-61000;writer.view->session_heartbeat_ms=now;uint32_t width,height;
    CHECK(awm_hub_live(&reader,now)&&awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL)&&writer.view->video_frames==1);
    /* Concurrent producer heartbeat ahead of consumer clock sample must not underflow. */
    writer.view->session_heartbeat_ms=now+25;writer.view->slots[slot].heartbeat_ms=now+25;writer.view->source_kind=CAPTURE_CAMERA;
    CHECK(awm_hub_live(&reader,now));
    /* Camera slot heartbeat tolerates brief 1.5s pauses without false disconnect, expires past 2.5s. */
    writer.view->session_heartbeat_ms=now;writer.view->slots[slot].heartbeat_ms=now-1500;CHECK(awm_hub_live(&reader,now));
    writer.view->slots[slot].heartbeat_ms=now-2600;CHECK(!awm_hub_live(&reader,now));
    writer.view->slots[slot].heartbeat_ms=now-61000;CHECK(!awm_hub_live(&reader,now));writer.view->source_kind=CAPTURE_SCREEN;writer.view->session_heartbeat_ms=now-3001;CHECK(!awm_hub_live(&reader,now));
    /* Verify single-pass 2x2 awm_bgra_to_nv12 matches reference math bit-for-bit. */
    BYTE bgra[16*16*4],nv12[16*16*3/2];for(unsigned i=0;i<16*16;i++){bgra[i*4]=(BYTE)((i*37)&255);bgra[i*4+1]=(BYTE)((i*73)&255);bgra[i*4+2]=(BYTE)((i*113)&255);bgra[i*4+3]=255;}
    awm_bgra_to_nv12(bgra,16,16,nv12);bool nv12_ok=true;
    for(unsigned y=0;y<16&&nv12_ok;y++)for(unsigned x=0;x<16;x++){const BYTE *q=bgra+(y*16+x)*4;int ey=((47*q[2]+157*q[1]+16*q[0]+128)>>8)+16;if(nv12[y*16+x]!=(BYTE)(ey<0?0:ey>255?255:ey))nv12_ok=false;}
    for(unsigned y=0;y<16&&nv12_ok;y+=2)for(unsigned x=0;x<16;x+=2){int b=0,g=0,r=0;for(unsigned j=0;j<2;j++)for(unsigned i=0;i<2;i++){const BYTE *q=bgra+((y+j)*16+x+i)*4;b+=q[0];g+=q[1];r+=q[2];}b/=4;g/=4;r/=4;int eu=((-26*r-87*g+112*b+128)>>8)+128,ev=((112*r-102*g-10*b+128)>>8)+128;if(nv12[16*16+(y/2)*16+x]!=(BYTE)(eu<0?0:eu>255?255:eu)||nv12[16*16+(y/2)*16+x+1]!=(BYTE)(ev<0?0:ev>255?255:ev))nv12_ok=false;}
    CHECK(nv12_ok);
    writer.view->session_heartbeat_ms=now;uint32_t epoch=writer.view->video_epoch;awm_hub_reconfigure(&writer);CHECK(writer.view->video_epoch==epoch+1&&!awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL));CHECK(awm_hub_publish(&writer,pixels,16,16,456)&&awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL));awm_hub_stop(&writer);CHECK(!awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL));awm_hub_close(&reader);awm_hub_close(&writer);
    free(args);free(command);free(caps);free(saved);free(loaded);printf("SYNTHETIC screen/profile/theme/persistence/liveness: %u checks, %u failures. No physical-device claim.\n",checks,failures);return failures?1:0;
}
