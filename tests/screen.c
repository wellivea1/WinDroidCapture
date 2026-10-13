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
    NativeCaptureProfile camera;native_capture_default(&camera,CAPTURE_CAMERA,0);awm_copy(camera.camera.id,sizeof(camera.camera.id),"5");camera.audio=false;CHECK(!native_capture_validate(&camera,&d,&f,error,sizeof(error)));
    d.sdk=37;d.transport=TRANSPORT_USB;f.camera=f.camera_fps=true;CHECK(native_capture_validate(&camera,&d,&f,error,sizeof(error)));awm_copy(camera.camera.facing,sizeof(camera.camera.facing),"back");CHECK(!native_capture_validate(&camera,&d,&f,error,sizeof(error)));camera.camera.facing[0]=0;
    AwmComposition fit,fill;CHECK(awm_composition(862,1920,1920,1080,FRAME_FIT,&fit));CHECK(fit.height==1080&&fit.width<500&&fit.x>700&&fit.u==0&&fit.uw==1);
    CHECK(awm_composition(862,1920,1920,1080,FRAME_FILL,&fill));CHECK(fill.width==1920&&fill.height==1080&&fill.v>0&&fill.vh<0.3f&&fill.u==0);
    CHECK(!awm_composition(0,1920,1920,1080,0,&fit)&&!awm_composition(1920,1080,1920,1080,99,&fit));
    NativeSettings *saved=calloc(1,sizeof(*saved)),*loaded=calloc(1,sizeof(*loaded));saved->magic=0x324D5741;saved->version=6;saved->output_width=1920;saved->output_height=1080;saved->output_fps=60;saved->preview_scaler=1;saved->pipeline_scaler=2;saved->ui_theme=2;
    awm_wcopy(saved->adb,AWM_PATH,L"C:\\spaces\\日本語\\adb.exe");native_capture_set(saved,"DEVICE_ONE",&camera);native_capture_set(saved,"DEVICE_ONE",&p);native_capture_set(saved,"DEVICE_TWO",&camera);
    wchar_t tmp[AWM_PATH],path[AWM_PATH],backup[AWM_PATH];GetTempPathW(AWM_PATH,tmp);swprintf(path,AWM_PATH,L"%sAWM-screen-tests-%lu.dpapi",tmp,GetCurrentProcessId());
    CHECK(native_settings_save(saved,path)&&native_settings_load(loaded,path));CHECK(loaded->ui_theme==2&&loaded->output_fps==60&&loaded->pipeline_scaler==2&&!wcscmp(loaded->adb,saved->adb));
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
    CHECK(awm_hub_live(&reader,now)&&awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL)&&writer.view->video_frames==1);writer.view->source_kind=CAPTURE_CAMERA;CHECK(!awm_hub_live(&reader,now));writer.view->source_kind=CAPTURE_SCREEN;writer.view->session_heartbeat_ms=now-3001;CHECK(!awm_hub_live(&reader,now));
    writer.view->session_heartbeat_ms=now;uint32_t epoch=writer.view->video_epoch;awm_hub_reconfigure(&writer);CHECK(writer.view->video_epoch==epoch+1&&!awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL));CHECK(awm_hub_publish(&writer,pixels,16,16,456)&&awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL));awm_hub_stop(&writer);CHECK(!awm_hub_copy(&reader,copy,sizeof(copy),&width,&height,NULL));awm_hub_close(&reader);awm_hub_close(&writer);
    free(args);free(command);free(caps);free(saved);free(loaded);printf("SYNTHETIC screen/profile/theme/persistence/liveness: %u checks, %u failures. No physical-device claim.\n",checks,failures);return failures?1:0;
}
