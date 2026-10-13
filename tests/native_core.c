#include "../src/native/manager.h"
#include "../src/native/command.h"
#include "../drivers/microphone/mic_ring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <wincrypt.h>
static int checks,failures;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);failures++;}}while(0)
static ToolFeatures features(void){ToolFeatures f={0};f.camera=f.camera_fps=f.require_audio=f.no_downsize=f.display_orientation=f.window_size=f.zoom=true;return f;}
static bool protected_fixture(const void *p,DWORD bytes,const wchar_t *file){DATA_BLOB input={bytes,(BYTE *)p},output={0};if(!CryptProtectData(&input,L"TEST FIXTURE",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&output))return false;HANDLE h=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD n=0;bool ok=h!=INVALID_HANDLE_VALUE&&WriteFile(h,output.pbData,output.cbData,&n,NULL)&&n==output.cbData;if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);LocalFree(output.pbData);return ok;}
int wmain(int argc,wchar_t **argv){
    (void)argc;(void)argv;platform_init();CoInitializeEx(NULL,COINIT_MULTITHREADED);
    DeviceList *l=calloc(1,sizeof(*l)),*snap=calloc(1,sizeof(*snap));
    CHECK(parse_adb_devices("List of devices attached\nONE device usb:1 model:Same_Model transport_id:7\nTWO unauthorized model:Same_Model transport_id:8\n192.168.1.8:42000 offline transport_id:9\nemulator-5554 device model:Emulator\nTHREE future_state\n",l));
    CHECK(l->count==5&&l->items[0].state==DEV_AUTHORIZED&&l->items[1].state==DEV_UNAUTHORIZED&&l->items[2].state==DEV_OFFLINE&&l->items[4].state==DEV_UNKNOWN);
    CHECK(find_device(l,"ONE","7")==0&&find_device(l,"ONE","8")==-1);CHECK(l->items[2].transport==TRANSPORT_WIRELESS&&l->items[3].transport==TRANSPORT_EMULATOR);
    CHECK(parse_adb_devices("List of devices attached\nONE offline transport_id:7\n",snap));merge_devices(l,snap);CHECK(l->items[0].state==DEV_OFFLINE&&l->items[1].state==DEV_DISCONNECTED);
    CHECK(!valid_wireless_address(L"host:0")&&!valid_wireless_address(L"host:65536")&&!valid_wireless_address(L"host:5555 & del"));
    CHECK(valid_wireless_address(L"192.168.1.2:37123")&&valid_wireless_address(L"[::1]:41000"));CHECK(valid_pairing_code(L"123456")&&!valid_pairing_code(L"12345x"));
    Capabilities *caps=calloc(1,sizeof(*caps));parse_capabilities("--camera-id=5 (back, 4080x3072, fps={30, 60}, zoom-range=[1, 10])\n - 1920x1080\n --video-codec=h264 --video-encoder=c2.vendor.avc (hw)\n",caps);
    CHECK(caps->camera_count==1&&caps->cameras[0].size_count==1&&caps->encoder_count==1);
    Profile p;profile_default(&p,0);awm_copy(p.camera_id,80,"5");Device d={0};awm_copy(d.serial,256,"SELECTED");awm_copy(d.transport_id,32,"7");d.sdk=37;d.state=DEV_AUTHORIZED;d.transport=TRANSPORT_USB;ToolFeatures f=features();Arguments *a=calloc(1,sizeof(*a));char err[1024];wchar_t *quoted=calloc(32768,sizeof(wchar_t));
    CHECK(native_capture_args(L"C:\\folder with space\\日本語\\scrcpy-awm.exe",L"\\\\.\\pipe\\AWM.Export.test",&d,&p,&f,a,err,sizeof(err)));CHECK(quote_windows_args(a,quoted,32768));CHECK(wcsstr(quoted,L"--no-window")&&wcsstr(quoted,L"--no-playback")&&wcsstr(quoted,L"--audio-source=mic")&&wcsstr(quoted,L"--require-audio"));
    CHECK(!wcsstr(quoted,L"--window-width")&&!wcsstr(quoted,L"--display-orientation")&&!wcsstr(quoted,L"--audio-device")&&!wcsstr(quoted,L"--max-fps"));
    CHECK(profile_declared(&p,caps,err,sizeof(err)));p.camera_facing[0]='b';CHECK(!native_capture_args(L"x",L"\\\\.\\pipe\\AWM.Export.test",&d,&p,&f,a,err,sizeof(err)));p.camera_facing[0]=0;p.max_size=720;CHECK(!native_capture_args(L"x",L"\\\\.\\pipe\\AWM.Export.test",&d,&p,&f,a,err,sizeof(err)));p.max_size=0;p.width=4080;CHECK(!native_capture_args(L"x",L"\\\\.\\pipe\\AWM.Export.test",&d,&p,&f,a,err,sizeof(err)));p.width=1920;
    CHECK(!native_capture_args(L"x",L"arbitrary-pipe",&d,&p,&f,a,err,sizeof(err)));p.audio=false;CHECK(native_capture_args(L"x",L"\\\\.\\pipe\\AWM.Export.test",&d,&p,&f,a,err,sizeof(err)));CHECK(quote_windows_args(a,quoted,32768)&&wcsstr(quoted,L"--no-audio")&&!wcsstr(quoted,L"--require-audio"));
    SessionState race={0};uint32_t generation=session_start(&race);session_stop(&race);CHECK(!session_complete(&race,generation,true));uint32_t next=session_start(&race);CHECK(!session_complete(&race,generation,true)&&session_complete(&race,next,true));
    AwmPacketHeader header={AWM_EXPORT_MAGIC,AWM_EXPORT_VERSION,AWM_MSG_OPEN,0,0,1920,1080};CHECK(awm_packet_valid(&header));header.width=0xffffffff;CHECK(!awm_packet_valid(&header));header.width=1920;header.size=AWM_PACKET_MAX+1;CHECK(!awm_packet_valid(&header));
    AwmHub hub={0},reader={0};BYTE *pixels=calloc(AWM_FRAME_BYTES,1),*copied=malloc(AWM_FRAME_BYTES);CHECK(awm_hub_open(&hub,true));AwmHub collision={0};CHECK(!awm_hub_open(&collision,true));CHECK(awm_hub_open(&reader,false));CHECK(reader.ready&&WaitForSingleObject(reader.ready,0)==WAIT_TIMEOUT);memset(pixels,63,16*16*4);CHECK(awm_hub_publish(&hub,pixels,16,16,123));CHECK(WaitForSingleObject(reader.ready,100)==WAIT_OBJECT_0&&WaitForSingleObject(reader.ready,0)==WAIT_TIMEOUT);uint32_t w,h;int64_t pts;CHECK(awm_hub_copy(&reader,copied,AWM_FRAME_BYTES,&w,&h,&pts)&&w==16&&h==16&&pts==123&&!memcmp(pixels,copied,16*16*4));AwmFrameInfo info;CHECK(awm_hub_copy_frame(&reader,copied,AWM_FRAME_BYTES,&info)&&info.number==1&&info.epoch==1&&info.pts_us==123);int index=hub.view->published;InterlockedIncrement(&hub.view->slots[index].sequence);CHECK(!awm_hub_copy_frame(&reader,copied,AWM_FRAME_BYTES,&info));InterlockedIncrement(&hub.view->slots[index].sequence);awm_hub_stop(&hub);CHECK(WaitForSingleObject(reader.ready,100)==WAIT_OBJECT_0);CHECK(!awm_hub_copy(&reader,copied,AWM_FRAME_BYTES,&w,&h,&pts)&&!hub.view->microphone_live);awm_hub_close(&reader);awm_hub_close(&hub);
    AwmMicRing *ring=calloc(1,sizeof(*ring));int16_t *pcm=calloc(AWM_RING_SAMPLES,sizeof(*pcm)),*out=calloc(AWM_RING_SAMPLES,sizeof(*out));for(unsigned i=0;i<AWM_RING_SAMPLES;i++)pcm[i]=1234;
    awm_ring_begin(ring,7);CHECK(!awm_ring_write(ring,6,pcm,10,0,100));CHECK(awm_ring_write(ring,7,pcm,480,1000,100));CHECK(!awm_ring_write(ring,7,pcm,10,999,100));awm_ring_read(ring,out,480,101);CHECK(out[0]==1234&&out[479]==1234&&ring->count==0);CHECK(awm_ring_write(ring,7,pcm,AWM_RING_SAMPLES,2000,110));CHECK(awm_ring_write(ring,7,pcm,480,3000,120)&&ring->overruns==AWM_RING_SAMPLES);awm_ring_read(ring,out,480,371);CHECK(out[0]==0&&out[479]==0&&ring->count==0);CHECK(!awm_ring_write(ring,7,pcm,AWM_RING_SAMPLES+1,4000,400));awm_ring_reset(ring);CHECK(!awm_ring_write(ring,7,pcm,1,4000,400));awm_ring_read(ring,out,480,401);CHECK(out[0]==0&&out[479]==0);
    NativeSettings *s=calloc(1,sizeof(*s)),*loaded=calloc(1,sizeof(*loaded));s->magic=0x324D5741;s->version=6;s->output_width=1920;s->output_height=1080;s->output_fps=30;s->preview_scaler=1;s->pipeline_scaler=2;awm_wcopy(s->adb,AWM_PATH,L"C:\\tools with space\\日本語\\adb.exe");native_profile_set(s,"ONE",&p);wchar_t tmp[AWM_PATH],file[AWM_PATH];GetTempPathW(AWM_PATH,tmp);swprintf(file,AWM_PATH,L"%sAWM-test-%lu.dpapi",tmp,GetCurrentProcessId());CHECK(native_settings_save(s,file)&&native_settings_load(loaded,file));CHECK(!wcscmp(s->adb,loaded->adb)&&native_profile_get(loaded,"ONE",&p));CHECK(loaded->preview_scaler==1&&loaded->pipeline_scaler==2);
    s->preview_scaler=99;CHECK(native_settings_save(s,file)&&!native_settings_load(loaded,file)&&loaded->preview_scaler==1);
    BYTE tiny=0;CHECK(protected_fixture(&tiny,1,file)&&!native_settings_load(loaded,file)&&loaded->pipeline_scaler==2);
    s->version=2;CHECK(protected_fixture(s,(DWORD)offsetof(NativeSettings,preview_scaler),file)&&native_settings_load(loaded,file));CHECK(loaded->version==6&&loaded->preview_scaler==0&&loaded->pipeline_scaler==0&&native_profile_get(loaded,"ONE",&p));DeleteFileW(file);wchar_t backup[AWM_PATH];swprintf(backup,AWM_PATH,L"%s.pre-v4.bak",file);DeleteFileW(backup);
    AudioMeter meter;CHECK(!audio_meter_open(&meter,L"definitely-missing-owned-endpoint",err,sizeof(err)));
    /* Clearly simulated subprocess cleanup: only this test's child is killed. */
    wchar_t self[AWM_PATH];GetModuleFileNameW(NULL,self,AWM_PATH);
    printf("%d checks, %d failures. Ring/protocol tests are user-mode simulations; kernel and receiver tests are separate.\n",checks,failures);
    free(l);free(snap);free(caps);free(a);free(quoted);free(pixels);free(copied);free(ring);free(pcm);free(out);free(s);free(loaded);CoUninitialize();platform_shutdown();return failures?1:0;
}
