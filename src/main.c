#include "engine.h"
#include "app_icon.h"
static AwmAppIcons window_icons;
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { ID_TAB=100,ID_DEVICES,ID_REFRESH,ID_PROBE,ID_CAMERA,ID_SIZE,ID_FPS,ID_CODEC,ID_BITRATE,ID_ENCODER,ID_ROTATION,ID_MIRROR,ID_ZOOM,ID_AUDIO,ID_AUDIO_SOURCE,ID_RETRY,ID_STANDARD,ID_DETAIL,ID_MOTION,ID_SAVE_PROFILE,ID_PREVIEW,ID_START,ID_STOP,ID_COMMAND,ID_COPY_COMMAND,ID_ADB_PATH,ID_SCRCPY_PATH,ID_OBS_PATH,ID_BROWSE_ADB,ID_BROWSE_SCRCPY,ID_BROWSE_OBS,ID_FOLDER,ID_APPLY,ID_PORT,ID_PASSWORD,ID_OBS_CHECK,ID_REPAIR,ID_LINK_SCRCPY,ID_LINK_ADB,ID_LINK_OBS,ID_LINK_CABLE,ID_VERSIONS,ID_PLAYBACK,ID_RECORDING,ID_MIXER,ID_MIC_SETUP,ID_METER_ON,ID_METER_OFF,ID_CONFIRM_MIC,ID_AUDIO_SAVE,ID_PEAK,ID_PAIR_ADDRESS,ID_PAIR_CODE,ID_CONNECT_ADDRESS,ID_PAIR,ID_CONNECT,ID_DISCONNECT,ID_RECONNECT,ID_MDNS,ID_LOG,ID_EXPORT,ID_STATUS,ID_NOTICE,ID_CAPABILITY_NOTE,ID_WIRELESS_RESULT };
typedef struct { HWND handle;int page,x,y,w,h; } Control;
static struct {
    HINSTANCE instance;HWND window,tab;HFONT font,heading;Engine *engine;EngineView *view;
    Control controls[160];int control_count,page,dpi;
    char selected_serial[256],selected_transport[32],last_active_serial[256];Profile profile;
    Configuration *configuration;bool initialized,rendering,setup_dirty;
    uint32_t last_plan;DeviceList previous_devices;
    char last_capability[256];Capabilities *previous_capabilities;AudioEndpoints *previous_endpoints;
    wchar_t last_command[32768];char last_log[AWM_TEXT];
} ui;
static HWND control(int id){return GetDlgItem(ui.window,id);}
static int px(int n){return MulDiv(n,ui.dpi,96);}
static HWND add(int page,int id,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int w,int h){
    HWND c=CreateWindowExW(!wcscmp(cls,L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),ui.window,(HMENU)(INT_PTR)id,ui.instance,NULL);
    SendMessageW(c,WM_SETFONT,(WPARAM)ui.font,TRUE);if(ui.control_count<160)ui.controls[ui.control_count++]=(Control){c,page,x,y,w,h};return c;
}
static HWND label(int page,const wchar_t *s,int x,int y,int w,int h){return add(page,0,L"STATIC",s,SS_LEFT,x,y,w,h);}
static HWND edit(int page,int id,const wchar_t *s,int x,int y,int w,int h,DWORD extra){return add(page,id,L"EDIT",s,WS_TABSTOP|ES_AUTOHSCROLL|extra,x,y,w,h);}
static HWND button(int page,int id,const wchar_t *s,int x,int y,int w){return add(page,id,L"BUTTON",s,WS_TABSTOP|BS_PUSHBUTTON,x,y,w,30);}
static HWND combo(int page,int id,int x,int y,int w,bool editable){return add(page,id,L"COMBOBOX",L"",WS_TABSTOP|WS_VSCROLL|(editable?CBS_DROPDOWN:CBS_DROPDOWNLIST),x,y,w,260);}
static HWND check(int page,int id,const wchar_t *s,int x,int y,int w){return add(page,id,L"BUTTON",s,WS_TABSTOP|BS_AUTOCHECKBOX,x,y,w,24);}
static void set_utf8(int id,const char *s){wchar_t *w=calloc(strlen(s)+2,sizeof(wchar_t));if(w){utf8_to_wide(s,w,strlen(s)+2);HWND c=control(id);int len=GetWindowTextLengthW(c);wchar_t *old=calloc((size_t)len+1,sizeof(wchar_t));if(old){GetWindowTextW(c,old,len+1);if(wcscmp(old,w))SetWindowTextW(c,w);free(old);}else SetWindowTextW(c,w);free(w);}}
static void cb_add(int id,const wchar_t *text,LPARAM data){LRESULT n=SendMessageW(control(id),CB_ADDSTRING,0,(LPARAM)text);SendMessageW(control(id),CB_SETITEMDATA,(WPARAM)n,data);}
static void cb_reset(int id){SendMessageW(control(id),CB_RESETCONTENT,0,0);}
static void cb_select(int id,int i){SendMessageW(control(id),CB_SETCURSEL,(WPARAM)i,0);}
static int cb_index(int id){return (int)SendMessageW(control(id),CB_GETCURSEL,0,0);}
static void choose_page(int page){ui.page=page;for(int i=0;i<ui.control_count;i++){Control *c=&ui.controls[i];ShowWindow(c->handle,c->page<0||c->page==page?SW_SHOW:SW_HIDE);}TabCtrl_SetCurSel(ui.tab,page);}
static bool selected_device(Device *out){int i=find_device(&ui.view->devices,ui.selected_serial,ui.selected_transport);if(i<0){MessageBoxW(ui.window,L"Select a listed device explicitly. Each serial/transport has its own identity.",L"Select a device",MB_OK|MB_ICONINFORMATION);return false;}*out=ui.view->devices.items[i];return true;}
static bool ui_send(Command *c){bool ok=engine_send(ui.engine,c);if(!ok)MessageBoxW(ui.window,L"The operation queue is full. Wait for the current operation or press Stop.",L"WinDroid Capture",MB_OK|MB_ICONINFORMATION);return ok;}
static void simple(CommandType type){Command c={0};c.type=type;ui_send(&c);}
static void read_text(int id,wchar_t *out,int cap){GetWindowTextW(control(id),out,cap);}
static void profile_to_controls(void){
    ui.rendering=true;wchar_t text[256];swprintf(text,256,L"%dx%d",ui.profile.width,ui.profile.height);SetWindowTextW(control(ID_SIZE),text);swprintf(text,256,L"%d",ui.profile.fps);SetWindowTextW(control(ID_FPS),text);swprintf(text,256,L"%d",ui.profile.bitrate_mbps);SetWindowTextW(control(ID_BITRATE),text);swprintf(text,256,L"%.3f",ui.profile.zoom);SetWindowTextW(control(ID_ZOOM),text);
    cb_select(ID_CODEC,!strcmp(ui.profile.codec,"h265")?1:!strcmp(ui.profile.codec,"av1")?2:0);cb_select(ID_ROTATION,ui.profile.rotation/90);
    const char *sources[]={"mic","mic-unprocessed","mic-voice-communication","mic-voice-recognition"};int source=0;for(int i=0;i<4;i++)if(!strcmp(sources[i],ui.profile.audio_source))source=i;cb_select(ID_AUDIO_SOURCE,source);
    SendMessageW(control(ID_MIRROR),BM_SETCHECK,ui.profile.mirror?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(control(ID_AUDIO),BM_SETCHECK,ui.profile.audio?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(control(ID_RETRY),BM_SETCHECK,ui.profile.reconnect?BST_CHECKED:BST_UNCHECKED,0);ui.rendering=false;
}
static bool read_profile(bool validate){
    wchar_t s[256],tail;int w=0,h=0;read_text(ID_SIZE,s,256);if(swscanf(s,L"%dx%d %lc",&w,&h,&tail)!=2){if(validate)MessageBoxW(ui.window,L"Resolution must be WIDTHxHEIGHT, such as 1920x1080.",L"Camera settings",MB_OK|MB_ICONWARNING);return false;}ui.profile.width=w;ui.profile.height=h;
    read_text(ID_FPS,s,256);wchar_t *end=NULL;long fps=wcstol(s,&end,10);if(!s[0]||*end){if(validate)MessageBoxW(ui.window,L"FPS must be a whole number.",L"Camera settings",MB_OK);return false;}ui.profile.fps=(int)fps;
    read_text(ID_BITRATE,s,256);long bitrate=wcstol(s,&end,10);if(!s[0]||*end){if(validate)MessageBoxW(ui.window,L"Bitrate must be a whole number of Mbps.",L"Camera settings",MB_OK);return false;}ui.profile.bitrate_mbps=(int)bitrate;
    read_text(ID_ZOOM,s,256);double zoom=wcstod(s,&end);if(!s[0]||*end){if(validate)MessageBoxW(ui.window,L"Zoom must be a number within the camera's reported range.",L"Camera settings",MB_OK);return false;}ui.profile.zoom=(float)zoom;
    int index=cb_index(ID_CAMERA);if(index>=0){int cam=(int)SendMessageW(control(ID_CAMERA),CB_GETITEMDATA,(WPARAM)index,0);if(cam>=0&&cam<ui.view->capabilities.camera_count)awm_copy(ui.profile.camera_id,sizeof(ui.profile.camera_id),ui.view->capabilities.cameras[cam].id);}
    int codec=cb_index(ID_CODEC);awm_copy(ui.profile.codec,sizeof(ui.profile.codec),codec==1?"h265":codec==2?"av1":"h264");
    index=cb_index(ID_ENCODER);int enc=index>=0?(int)SendMessageW(control(ID_ENCODER),CB_GETITEMDATA,(WPARAM)index,0):-1;ui.profile.encoder[0]=0;if(enc>=0&&enc<ui.view->capabilities.encoder_count)awm_copy(ui.profile.encoder,sizeof(ui.profile.encoder),ui.view->capabilities.encoders[enc].name);
    ui.profile.rotation=cb_index(ID_ROTATION)*90;ui.profile.mirror=SendMessageW(control(ID_MIRROR),BM_GETCHECK,0,0)==BST_CHECKED;ui.profile.audio=SendMessageW(control(ID_AUDIO),BM_GETCHECK,0,0)==BST_CHECKED;ui.profile.reconnect=SendMessageW(control(ID_RETRY),BM_GETCHECK,0,0)==BST_CHECKED;
    const char *sources[]={"mic","mic-unprocessed","mic-voice-communication","mic-voice-recognition"};index=cb_index(ID_AUDIO_SOURCE);awm_copy(ui.profile.audio_source,sizeof(ui.profile.audio_source),sources[index>=0&&index<4?index:0]);
    if(validate){char err[1024];if(!validate_profile(&ui.profile,&ui.view->features,err,sizeof(err))){wchar_t wide[1024];utf8_to_wide(err,wide,1024);MessageBoxW(ui.window,wide,L"Camera settings",MB_OK|MB_ICONWARNING);return false;}}
    return true;
}
static void render_encoders(void){
    int sel=0;cb_reset(ID_ENCODER);cb_add(ID_ENCODER,L"Automatic device encoder",-1);for(int i=0;i<ui.view->capabilities.encoder_count;i++){Encoder *e=&ui.view->capabilities.encoders[i];if(strcmp(e->codec,ui.profile.codec))continue;wchar_t name[400],encoder[256];utf8_to_wide(e->name,encoder,256);swprintf(name,400,L"%s (%s)",encoder,e->hardware?L"hardware":L"software");int index=(int)SendMessageW(control(ID_ENCODER),CB_GETCOUNT,0,0);cb_add(ID_ENCODER,name,i);if(!strcmp(e->name,ui.profile.encoder))sel=index;}cb_select(ID_ENCODER,sel);
}
static void render_capabilities(void){
    ui.rendering=true;cb_reset(ID_CAMERA);cb_reset(ID_SIZE);cb_reset(ID_FPS);int chosen=-1;bool valid=!strcmp(ui.view->capability_serial,ui.selected_serial)&&ui.selected_serial[0];
    if(valid)for(int i=0;i<ui.view->capabilities.camera_count;i++){Camera *c=&ui.view->capabilities.cameras[i];wchar_t name[256],id[80],facing[32];utf8_to_wide(c->id,id,80);utf8_to_wide(c->facing,facing,32);swprintf(name,256,L"Camera %s - %s (sensor %dx%d)",id,facing,c->width,c->height);cb_add(ID_CAMERA,name,i);if(!strcmp(c->id,ui.profile.camera_id))chosen=i;}
    cb_select(ID_CAMERA,chosen);Camera *cam=chosen>=0?&ui.view->capabilities.cameras[chosen]:NULL;
    if(cam){for(int i=0;i<cam->size_count;i++){wchar_t s[64];swprintf(s,64,L"%dx%d",cam->sizes[i].width,cam->sizes[i].height);cb_add(ID_SIZE,s,i);}for(int i=0;i<cam->fps_count;i++){wchar_t s[32];swprintf(s,32,L"%d",cam->fps[i]);cb_add(ID_FPS,s,i);}}
    cb_reset(ID_AUDIO_SOURCE);cb_add(ID_AUDIO_SOURCE,L"Microphone (mic)",0);if(ui.view->features.mic_sources){cb_add(ID_AUDIO_SOURCE,L"Unprocessed request (device may process)",1);cb_add(ID_AUDIO_SOURCE,L"Voice communication",2);cb_add(ID_AUDIO_SOURCE,L"Voice recognition",3);}
    EnableWindow(control(ID_ZOOM),cam&&cam->zoom_min>0&&ui.view->features.zoom);render_encoders();profile_to_controls();
    char note[2048];if(cam){snprintf(note,sizeof(note),"Device declaration: %d size(s), %d frame rate(s), zoom %.2f..%.2f. Modes and combinations need real startup validation. Higher nominal resolution alone does not guarantee better image quality.",cam->size_count,cam->fps_count,cam->zoom_min,cam->zoom_max);}else awm_copy(note,sizeof(note),valid?"Select an enumerated camera; no camera ID is assumed to be the main rear lens.":"Select a device and click Probe capabilities. Authorization and discovery do not start capture.");set_utf8(ID_CAPABILITY_NOTE,note);ui.rendering=false;
}
static void select_device(int index){
    if(index<0||index>=ui.view->devices.count)return;Device *d=&ui.view->devices.items[index];awm_copy(ui.selected_serial,sizeof(ui.selected_serial),d->serial);awm_copy(ui.selected_transport,sizeof(ui.selected_transport),d->transport_id);profile_default(&ui.profile,0);config_profile_get(&ui.view->configuration,d->serial,&ui.profile);render_capabilities();
    wchar_t address[320];utf8_to_wide(d->serial,address,320);if(valid_wireless_address(address))SetWindowTextW(control(ID_CONNECT_ADDRESS),address);
}
static void render_devices(void){
    if(!memcmp(&ui.previous_devices,&ui.view->devices,sizeof(DeviceList))&&!strcmp(ui.last_active_serial,ui.view->active_serial))return;awm_copy(ui.last_active_serial,sizeof(ui.last_active_serial),ui.view->active_serial);ui.rendering=true;HWND list=control(ID_DEVICES);ListView_DeleteAllItems(list);
    for(int i=0;i<ui.view->devices.count;i++){Device *d=&ui.view->devices.items[i];wchar_t name[256],serial[256],transport[32],state[128],android[64],active[32];utf8_to_wide(d->model,name,256);utf8_to_wide(d->serial,serial,256);utf8_to_wide(transport_name(d->transport),transport,32);char state8[128];snprintf(state8,sizeof(state8),"%s%s%s",device_state_name(d->state),d->state==DEV_UNKNOWN?": ":"",d->state==DEV_UNKNOWN?d->raw_state:"");utf8_to_wide(state8,state,128);utf8_to_wide(d->android,android,64);awm_wcopy(active,32,!strcmp(ui.view->active_serial,d->serial)&&ui.view->active_serial[0]?L"ACTIVE":L"");
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=name;item.lParam=i;ListView_InsertItem(list,&item);ListView_SetItemText(list,i,1,serial);ListView_SetItemText(list,i,2,transport);ListView_SetItemText(list,i,3,state);ListView_SetItemText(list,i,4,android);ListView_SetItemText(list,i,5,active);
        if(!strcmp(d->serial,ui.selected_serial)&&!strcmp(d->transport_id,ui.selected_transport))ListView_SetItemState(list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    ui.previous_devices=ui.view->devices;ui.rendering=false;
}
static void render_audio(void){
    if(!memcmp(ui.previous_endpoints,&ui.view->endpoints,sizeof(AudioEndpoints)))return;ui.rendering=true;cb_reset(ID_PLAYBACK);cb_reset(ID_RECORDING);int ps=-1,rs=-1;
    for(int i=0;i<ui.view->endpoints.count;i++){AudioEndpoint *e=&ui.view->endpoints.items[i];wchar_t text[320];swprintf(text,320,L"%s%s",e->name,e->state==DEVICE_STATE_ACTIVE?L"":L" [unavailable]");int id=e->flow==eRender?ID_PLAYBACK:ID_RECORDING;int index=(int)SendMessageW(control(id),CB_GETCOUNT,0,0);cb_add(id,text,i);if(e->flow==eRender&&!wcscmp(e->id,ui.configuration->playback_id))ps=index;if(e->flow==eCapture&&!wcscmp(e->id,ui.configuration->recording_id))rs=index;}
    cb_select(ID_PLAYBACK,ps);cb_select(ID_RECORDING,rs);*ui.previous_endpoints=ui.view->endpoints;ui.rendering=false;
}
static void configuration_to_controls(void){
    ui.rendering=true;SetWindowTextW(control(ID_ADB_PATH),ui.configuration->adb);SetWindowTextW(control(ID_SCRCPY_PATH),ui.configuration->scrcpy);SetWindowTextW(control(ID_OBS_PATH),ui.configuration->obs);SetWindowTextW(control(ID_PASSWORD),ui.configuration->obs_password);wchar_t port[32];swprintf(port,32,L"%u",ui.configuration->obs_port);SetWindowTextW(control(ID_PORT),port);ui.rendering=false;
}
static bool read_configuration(void){
    *ui.configuration=ui.view->configuration;read_text(ID_ADB_PATH,ui.configuration->adb,AWM_PATH);read_text(ID_SCRCPY_PATH,ui.configuration->scrcpy,AWM_PATH);read_text(ID_OBS_PATH,ui.configuration->obs,AWM_PATH);read_text(ID_PASSWORD,ui.configuration->obs_password,256);
    wchar_t port[32],*end=NULL;read_text(ID_PORT,port,32);unsigned long n=wcstoul(port,&end,10);if(!port[0]||*end||n<1||n>65535){MessageBoxW(ui.window,L"OBS port must be a number from 1 to 65535.",L"Setup",MB_OK);return false;}ui.configuration->obs_port=(unsigned)n;
    int ps=cb_index(ID_PLAYBACK),rs=cb_index(ID_RECORDING);int p=ps>=0?(int)SendMessageW(control(ID_PLAYBACK),CB_GETITEMDATA,(WPARAM)ps,0):-1;int r=rs>=0?(int)SendMessageW(control(ID_RECORDING),CB_GETITEMDATA,(WPARAM)rs,0):-1;
    ui.configuration->playback_id[0]=ui.configuration->recording_id[0]=0;if(p>=0&&p<ui.view->endpoints.count)awm_wcopy(ui.configuration->playback_id,1024,ui.view->endpoints.items[p].id);if(r>=0&&r<ui.view->endpoints.count)awm_wcopy(ui.configuration->recording_id,1024,ui.view->endpoints.items[r].id);
    return true;
}
static bool apply_setup(void){if(!read_configuration())return false;Command c={0};c.type=CMD_CONFIG;c.configuration=ui.configuration;ui.setup_dirty=false;return ui_send(&c);}
static void copy_clipboard(const wchar_t *s){if(!OpenClipboard(ui.window))return;size_t bytes=(wcslen(s)+1)*sizeof(wchar_t);HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,bytes);if(data){void *p=GlobalLock(data);memcpy(p,s,bytes);GlobalUnlock(data);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data))GlobalFree(data);}CloseClipboard();}
static void browse_exe(int id){wchar_t path[AWM_PATH]={0};read_text(id,path,AWM_PATH);OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=ui.window;o.lpstrFilter=L"Windows executable (*.exe)\0*.exe\0\0";o.lpstrFile=path;o.nMaxFile=AWM_PATH;o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&o)){SetWindowTextW(control(id),path);ui.setup_dirty=true;}}
static void browse_folder(void){BROWSEINFOW b={0};b.hwndOwner=ui.window;b.lpszTitle=L"Select an extracted official scrcpy distribution containing scrcpy.exe and adb.exe";b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&b);if(pid){wchar_t dir[MAX_PATH],exe[AWM_PATH],adb[AWM_PATH];if(SHGetPathFromIDListW(pid,dir)){swprintf(exe,AWM_PATH,L"%s\\scrcpy.exe",dir);swprintf(adb,AWM_PATH,L"%s\\adb.exe",dir);if(path_is_file(exe)&&path_is_file(adb)){SetWindowTextW(control(ID_SCRCPY_PATH),exe);SetWindowTextW(control(ID_ADB_PATH),adb);ui.setup_dirty=true;}else MessageBoxW(ui.window,L"That folder does not contain both scrcpy.exe and adb.exe. Select the extracted distribution root.",L"scrcpy folder",MB_OK);}CoTaskMemFree(pid);}}
static void export_diagnostics(void){
    wchar_t path[AWM_PATH]=L"WinDroidCapture-diagnostics.txt";OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=ui.window;o.lpstrFilter=L"Text files\0*.txt\0\0";o.lpstrFile=path;o.nMaxFile=AWM_PATH;o.lpstrDefExt=L"txt";o.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetSaveFileNameW(&o))return;
    char *data=calloc(AWM_TEXT+4096,1);if(!data)return;snprintf(data,AWM_TEXT+4096,"WinDroid Capture 0.1.0\r\n%s\r\n%s\r\nOBS file version: %s\r\nPhysical receiver and synchronization results: NOT VERIFIED by this export.\r\n\r\n%s",ui.view->adb_version,ui.view->scrcpy_version,ui.view->obs_version,ui.view->diagnostics);redact_text(data,&ui.view->devices);
    wchar_t userpath[AWM_PATH];char user8[4096];if(GetEnvironmentVariableW(L"USERPROFILE",userpath,AWM_PATH)){wide_to_utf8(userpath,user8,sizeof(user8));size_t len=strlen(user8);char *p;while(len&&(p=strstr(data,user8))!=NULL)memset(p,'*',len);}
    /* Discovered mDNS instance names and IP addresses do not need to leave the PC. */
    for(char *p=data;*p;p++){if(!strncmp(p,"adb-",4)){char *end=p;while(*end&&*end!=' '&&*end!='\r'&&*end!='\n'&&*end!='\t')end++;memset(p,'*',(size_t)(end-p));}unsigned a,b,c,d;int len=0;if(*p>='0'&&*p<='9'&&sscanf(p,"%u.%u.%u.%u%n",&a,&b,&c,&d,&len)==4&&a<256&&b<256&&c<256&&d<256&&len>6){memset(p,'*',(size_t)len);p+=len-1;}}
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(file!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(file,data,(DWORD)strlen(data),&n,NULL);CloseHandle(file);}else MessageBoxW(ui.window,L"The diagnostics file could not be written.",L"Export",MB_OK|MB_ICONERROR);free(data);
}
static void session_action(CommandType type){
    Command c={0};c.type=type;if(!selected_device(&c.device)||!read_profile(true))return;c.profile=ui.profile;
    if(type!=CMD_SAVE_PROFILE){char note[1024];if(!profile_declared(&c.profile,&ui.view->capabilities,note,sizeof(note))){wchar_t text[1400];utf8_to_wide(note,text,1400);wcscat(text,L"\r\n\r\nExplicitly attempt this unverified mode without automatic downsize?");if(MessageBoxW(ui.window,text,L"Unverified camera mode",MB_YESNO|MB_ICONWARNING)!=IDYES)return;}}
    if((type==CMD_MIC_SETUP||type==CMD_OUTPUT_PLAN)&&ui.profile.audio&&!ui.view->microphone){
        if(!ui.configuration->playback_id[0]||!ui.configuration->recording_id[0]){choose_page(2);MessageBoxW(ui.window,L"Select and save both virtual-cable endpoints first. Scrcpy renders to the playback endpoint; the receiver uses its paired recording endpoint as microphone.",L"Virtual microphone setup",MB_OK|MB_ICONINFORMATION);return;}
        if(MessageBoxW(ui.window,L"This starts the selected phone's microphone with required-audio checking. Scrcpy uses its saved Windows per-app playback route.\r\n\r\nFor first-time setup, open Volume Mixer and route scrcpy to the selected virtual-cable playback endpoint. Use headphones or mute speakers during that manual step. Do not enable Listen to this device or route the cable back into itself.\r\n\r\nThe manager does not change system defaults. Confirm that these are a paired virtual-cable playback and recording endpoint, then start microphone setup?",L"Phone microphone route",MB_YESNO|MB_ICONINFORMATION)!=IDYES)return;
    }
    ui_send(&c);
}
static void on_button(int id){
    switch(id){
    case ID_REFRESH:simple(CMD_REFRESH);break;
    case ID_PROBE:{Command c={0};c.type=CMD_PROBE;if(selected_device(&c.device))ui_send(&c);break;}
    case ID_PREVIEW:session_action(CMD_PREVIEW);break;
    case ID_START:session_action(CMD_OUTPUT_PLAN);break;
    case ID_MIC_SETUP:session_action(CMD_MIC_SETUP);break;
    case ID_STOP:simple(CMD_STOP);break;
    case ID_SAVE_PROFILE:session_action(CMD_SAVE_PROFILE);break;
    case ID_STANDARD:case ID_DETAIL:case ID_MOTION:{char camera[80];awm_copy(camera,sizeof(camera),ui.profile.camera_id);bool audio=ui.profile.audio,mirror=ui.profile.mirror;int rotation=ui.profile.rotation;profile_default(&ui.profile,id==ID_DETAIL?1:id==ID_MOTION?2:0);awm_copy(ui.profile.camera_id,sizeof(ui.profile.camera_id),camera);ui.profile.audio=audio;ui.profile.mirror=mirror;ui.profile.rotation=rotation;render_capabilities();break;}
    case ID_COPY_COMMAND:copy_clipboard(ui.view->effective_command);break;
    case ID_BROWSE_ADB:browse_exe(ID_ADB_PATH);break;
    case ID_BROWSE_SCRCPY:browse_exe(ID_SCRCPY_PATH);break;
    case ID_BROWSE_OBS:browse_exe(ID_OBS_PATH);break;
    case ID_FOLDER:browse_folder();break;
    case ID_APPLY:case ID_AUDIO_SAVE:apply_setup();break;
    case ID_OBS_CHECK:if(apply_setup())simple(CMD_OBS_CHECK);break;
    case ID_REPAIR:{if(MessageBoxW(ui.window,L"Restart the SHARED ADB server using the selected ADB executable?\r\n\r\nThis will stop the manager's capture and interrupt other ADB clients (Android Studio, debugging sessions, other scrcpy sessions). It is never done automatically.",L"Explicit shared ADB repair",MB_YESNO|MB_ICONWARNING)==IDYES){Command c={0};c.type=CMD_ADB_REPAIR;c.flag=true;ui_send(&c);}break;}
    case ID_LINK_SCRCPY:ShellExecuteW(ui.window,L"open",L"https://github.com/Genymobile/scrcpy/releases/latest",NULL,NULL,SW_SHOWNORMAL);break;
    case ID_LINK_ADB:ShellExecuteW(ui.window,L"open",L"https://developer.android.com/tools/releases/platform-tools",NULL,NULL,SW_SHOWNORMAL);break;
    case ID_LINK_OBS:ShellExecuteW(ui.window,L"open",L"https://obsproject.com/download",NULL,NULL,SW_SHOWNORMAL);break;
    case ID_LINK_CABLE:ShellExecuteW(ui.window,L"open",L"https://vb-audio.com/Cable/",NULL,NULL,SW_SHOWNORMAL);break;
    case ID_MIXER:ShellExecuteW(ui.window,L"open",L"ms-settings:apps-volume",NULL,NULL,SW_SHOWNORMAL);break;
    case ID_METER_ON:case ID_METER_OFF:{Command c={0};c.type=CMD_METER;c.flag=id==ID_METER_ON;ui_send(&c);break;}
    case ID_CONFIRM_MIC:{if(MessageBoxW(ui.window,L"Phone-specific microphone test:\r\n\r\n1. Silence other apps rendering to this cable and leave Windows Listen to this device off.\r\n2. Move the phone away from PC microphones. Speak a distinct phrase into the PHONE and lightly tap near its microphone. The selected cable meter should follow that pattern.\r\n3. Press Stop and verify that the phone signal ends. Restart microphone setup and repeat the phone test.\r\n4. In the local receiver harness, explicitly choose OBS Virtual Camera and the paired cable RECORDING endpoint; verify the same phone signal there.\r\n\r\nHave you verified the phone-specific signal on the cable for the CURRENT session? This is a user confirmation, not an automatic hardware-test result.",L"Phone speak / tap test",MB_YESNO|MB_ICONINFORMATION)==IDYES){Command c={0};c.type=CMD_MIC_CONFIRM;c.flag=true;ui_send(&c);}break;}
    case ID_PAIR:case ID_CONNECT:case ID_DISCONNECT:case ID_MDNS:case ID_RECONNECT:{Command c={0};c.type=id==ID_PAIR?CMD_PAIR:id==ID_CONNECT?CMD_CONNECT:id==ID_DISCONNECT?CMD_DISCONNECT:id==ID_MDNS?CMD_MDNS:CMD_RECONNECT;
        read_text(id==ID_PAIR?ID_PAIR_ADDRESS:ID_CONNECT_ADDRESS,c.address,320);if(id==ID_PAIR){read_text(ID_PAIR_CODE,c.pairing_code,16);SetWindowTextW(control(ID_PAIR_CODE),L"");}
        if(id==ID_RECONNECT&&!selected_device(&c.device))break;ui_send(&c);SecureZeroMemory(c.pairing_code,sizeof(c.pairing_code));break;}
    case ID_EXPORT:export_diagnostics();break;
    }
}
static void update(void){
    if(!ui.engine)return;engine_snapshot(ui.engine,ui.view);
    if(!ui.initialized&&ui.view->configuration.magic){*ui.configuration=ui.view->configuration;configuration_to_controls();ui.initialized=true;}
    if(!ui.setup_dirty&&ui.initialized){*ui.configuration=ui.view->configuration;}
    render_devices();render_audio();
    if(strcmp(ui.last_capability,ui.view->capability_serial)||memcmp(ui.previous_capabilities,&ui.view->capabilities,sizeof(Capabilities))){awm_copy(ui.last_capability,sizeof(ui.last_capability),ui.view->capability_serial);*ui.previous_capabilities=ui.view->capabilities;render_capabilities();}
    char status[8192];snprintf(status,sizeof(status),"ADB: %s\r\nCamera: %s\r\nPhone microphone: %s\r\nOBS webcam: %s\r\nVirtual microphone: %s\r\nReceiving application: NOT VERIFIED here; use the local receiver harness with explicit device choices.",ui.view->adb_status,ui.view->camera_status,ui.view->mic_status,ui.view->obs_status,ui.view->route_status);set_utf8(ID_STATUS,status);set_utf8(ID_NOTICE,ui.view->notice);set_utf8(ID_WIRELESS_RESULT,ui.view->notice);
    if(wcscmp(ui.last_command,ui.view->effective_command)){awm_wcopy(ui.last_command,32768,ui.view->effective_command);SetWindowTextW(control(ID_COMMAND),ui.last_command);}
    char versions[4096];snprintf(versions,sizeof(versions),"Selected ADB: %s\r\nSelected scrcpy: %s\r\nOBS executable version: %s\r\nDetected Windows video devices:\r\n%s\r\nThe controller is an ordinary user process. OBS, its Virtual Camera registration, and a virtual audio cable are separate installed dependencies.",ui.view->adb_version,ui.view->scrcpy_version,ui.view->obs_version,ui.view->webcams);set_utf8(ID_VERSIONS,versions);
    if(strcmp(ui.last_log,ui.view->diagnostics)){awm_copy(ui.last_log,sizeof(ui.last_log),ui.view->diagnostics);set_utf8(ID_LOG,ui.last_log);SendMessageW(control(ID_LOG),EM_SETSEL,(WPARAM)-1,(LPARAM)-1);SendMessageW(control(ID_LOG),EM_SCROLLCARET,0,0);}
    SendMessageW(control(ID_PEAK),PBM_SETPOS,(WPARAM)(ui.view->peak*1000),0);
    bool active=ui.view->active_serial[0]!=0;EnableWindow(control(ID_PROBE),!active&&!ui.view->busy);EnableWindow(control(ID_PREVIEW),!active&&!ui.view->busy);EnableWindow(control(ID_MIC_SETUP),!active&&!ui.view->busy);EnableWindow(control(ID_APPLY),!active);EnableWindow(control(ID_AUDIO_SAVE),!active);
    if(ui.view->output_plan&&ui.last_plan!=ui.view->plan_generation){ui.last_plan=ui.view->plan_generation;wchar_t plan[4096];utf8_to_wide(ui.view->plan,plan,4096);Command c={0};c.type=CMD_OUTPUT_COMMIT;c.generation=ui.view->plan_generation;c.flag=MessageBoxW(ui.window,plan,L"Approve the inspected OBS change",MB_YESNO|MB_ICONINFORMATION)==IDYES;ui_send(&c);}
}
static void create_controls(void){
    HWND title=label(-1,L"WinDroid Capture",24,14,640,38);SendMessageW(title,WM_SETFONT,(WPARAM)ui.heading,TRUE);label(-1,L"One selected phone. Separate capture, webcam, and microphone status.",26,54,950,23);
    ui.tab=add(-1,ID_TAB,WC_TABCONTROLW,L"",WS_TABSTOP,20,84,1136,566);const wchar_t *pages[]={L"Capture",L"Dependencies & OBS",L"Audio setup",L"Wireless",L"Diagnostics"};for(int i=0;i<5;i++){TCITEMW t={0};t.mask=TCIF_TEXT;t.pszText=(wchar_t *)pages[i];TabCtrl_InsertItem(ui.tab,i,&t);}
    HWND list=add(0,ID_DEVICES,WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,36,124,1102,142);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    const wchar_t *columns[]={L"Device",L"Serial / transport identity",L"Transport",L"State",L"Android",L"Session"};int widths[]={230,290,110,175,95,105};for(int i=0;i<6;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(wchar_t *)columns[i];c.cx=px(widths[i]);ListView_InsertColumn(list,i,&c);}
    button(0,ID_REFRESH,L"Refresh",36,277,105);button(0,ID_PROBE,L"Probe capabilities",153,277,180);button(0,ID_STANDARD,L"Standard: 1080p30",550,277,183);button(0,ID_DETAIL,L"Detail: 2160p30",745,277,180);button(0,ID_MOTION,L"Motion: 1080p60",937,277,201);
    label(0,L"Camera",36,319,315,20);combo(0,ID_CAMERA,36,342,315,false);label(0,L"Resolution (editable)",367,319,183,20);combo(0,ID_SIZE,367,342,183,true);label(0,L"Camera FPS",566,319,118,20);combo(0,ID_FPS,566,342,118,true);label(0,L"Video codec",700,319,142,20);combo(0,ID_CODEC,700,342,142,false);cb_add(ID_CODEC,L"H.264",0);cb_add(ID_CODEC,L"H.265",1);cb_add(ID_CODEC,L"AV1",2);label(0,L"Bitrate (Mbps)",858,319,128,20);edit(0,ID_BITRATE,L"16",858,342,128,26,0);label(0,L"Zoom",1002,319,136,20);edit(0,ID_ZOOM,L"1.000",1002,342,136,26,0);
    label(0,L"Encoder (advanced)",36,381,370,20);combo(0,ID_ENCODER,36,404,370,false);label(0,L"Output rotation",422,381,154,20);combo(0,ID_ROTATION,422,404,154,false);cb_add(ID_ROTATION,L"0 degrees",0);cb_add(ID_ROTATION,L"90 clockwise",1);cb_add(ID_ROTATION,L"180 degrees",2);cb_add(ID_ROTATION,L"270 clockwise",3);check(0,ID_MIRROR,L"Mirror output",594,405,160);check(0,ID_AUDIO,L"Phone microphone",777,405,182);check(0,ID_RETRY,L"Reconnect (3 tries)",965,405,177);
    add(0,ID_CAPABILITY_NOTE,L"STATIC",L"",SS_LEFT,36,445,1102,43);
    label(0,L"Transforms affect the scrcpy pixels captured by OBS. Receiver self-view can mirror separately. Settings apply at the next start.",36,493,1102,25);
    button(0,ID_PREVIEW,L"Camera preview",36,528,177);button(0,ID_START,L"Start webcam",227,528,177);button(0,ID_SAVE_PROFILE,L"Save profile",418,528,160);button(-1,ID_STOP,L"Stop camera + mic",944,14,194);
    edit(0,ID_COMMAND,L"Effective command appears after Start. Preview disables microphone capture.",36,574,914,55,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);button(0,ID_COPY_COMMAND,L"Copy command",966,583,172);
    label(1,L"Executable paths (the same ADB is explicitly passed to scrcpy)",36,126,1050,23);
    label(1,L"scrcpy.exe",36,162,128,24);edit(1,ID_SCRCPY_PATH,L"",165,159,843,27,0);button(1,ID_BROWSE_SCRCPY,L"Browse",1020,158,118);
    label(1,L"adb.exe",36,204,128,24);edit(1,ID_ADB_PATH,L"",165,201,843,27,0);button(1,ID_BROWSE_ADB,L"Browse",1020,200,118);
    label(1,L"obs64.exe",36,246,128,24);edit(1,ID_OBS_PATH,L"",165,243,843,27,0);button(1,ID_BROWSE_OBS,L"Browse",1020,242,118);
    button(1,ID_FOLDER,L"Use extracted scrcpy folder",36,283,265);button(1,ID_APPLY,L"Save & verify setup",317,283,220);button(1,ID_REPAIR,L"Explicit shared ADB repair",553,283,263);
    label(1,L"Local OBS WebSocket port",36,329,225,23);edit(1,ID_PORT,L"4455",266,326,100,27,0);label(1,L"OBS password",389,329,134,23);edit(1,ID_PASSWORD,L"",529,326,353,27,ES_PASSWORD);button(1,ID_OBS_CHECK,L"Test OBS connection",898,325,240);
    label(1,L"In OBS: Tools > WebSocket Server Settings > Enable WebSocket server. Keep authentication on. Saved credentials use Windows DPAPI.",36,366,1102,43);
    edit(1,ID_VERSIONS,L"Detecting selected dependency versions...",36,420,1102,129,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);
    label(1,L"Official installation sources (drivers may require elevation / reboot; the controller does not):",36,559,1102,22);button(1,ID_LINK_SCRCPY,L"scrcpy releases",36,590,255);button(1,ID_LINK_ADB,L"Android platform tools",308,590,260);button(1,ID_LINK_OBS,L"OBS Studio",585,590,255);button(1,ID_LINK_CABLE,L"VB-CABLE",857,590,281);
    label(2,L"Choose the intended virtual-cable pair. Nothing is inferred from the first cable or the device names.",36,126,1102,24);
    label(2,L"scrcpy renders TO this playback endpoint",36,166,530,22);combo(2,ID_PLAYBACK,36,191,1102,false);
    label(2,L"Receiving application's microphone: paired recording endpoint",36,235,700,22);combo(2,ID_RECORDING,36,260,1102,false);
    label(2,L"VB-CABLE example: playback CABLE Input -> recording CABLE Output. Confirm your actual pair; names can differ.",36,302,1102,22);
    label(2,L"Microphone source",36,340,240,23);combo(2,ID_AUDIO_SOURCE,278,336,540,false);button(2,ID_AUDIO_SAVE,L"Save audio / setup",850,335,288);
    label(2,L"One-time manual routing: start camera + mic, open Windows Volume Mixer, and set scrcpy's Output device to the cable PLAYBACK endpoint. Restart capture if Windows requires it. System-wide playback defaults are never changed.",36,380,1102,54);
    button(2,ID_MIC_SETUP,L"Start camera + mic for setup",36,449,284);button(2,ID_MIXER,L"Open Volume Mixer",338,449,220);button(2,ID_METER_ON,L"Start cable meter",576,449,220);button(2,ID_METER_OFF,L"Stop meter",814,449,150);button(2,ID_CONFIRM_MIC,L"Phone test",982,449,156);
    label(2,L"Selected recording endpoint signal (shared-cable level does not identify its source)",36,496,1102,23);HWND meter=add(2,ID_PEAK,PROGRESS_CLASSW,L"",0,36,526,1102,24);SendMessageW(meter,PBM_SETRANGE32,0,1000);
    label(2,L"Speaker monitoring is off in this app. Leave Windows Listen to this device off. The cable must carry only the intended phone signal; do not capture desktop audio or feed the cable back into itself. The local receiver test uses explicit endpoint selection and no speaker monitoring.",36,570,1102,57);
    label(3,L"USB: enable Developer options > USB debugging, unlock the phone, and accept its RSA authorization prompt. This app cannot bypass it.",36,128,1102,48);
    label(3,L"Modern wireless debugging (Android 11+): use the phone's Pair using pairing code address and port below.",36,193,1102,23);label(3,L"Pairing address:port",36,237,230,23);edit(3,ID_PAIR_ADDRESS,L"",270,233,457,27,0);label(3,L"Six-digit code",747,237,142,23);edit(3,ID_PAIR_CODE,L"",895,233,100,27,ES_PASSWORD);button(3,ID_PAIR,L"Pair",1013,232,125);
    label(3,L"Connection address:port",36,292,230,23);edit(3,ID_CONNECT_ADDRESS,L"",270,288,457,27,0);button(3,ID_CONNECT,L"Connect",747,287,182);button(3,ID_DISCONNECT,L"Disconnect address",946,287,192);
    label(3,L"The pairing and connection ports may differ. Do not assume 5555. IPv6 uses [address]:port. mDNS discovery uses the selected ADB's supported services command; manual entry remains available.",36,340,1102,52);
    button(3,ID_MDNS,L"Discover ADB mDNS services",36,410,311);button(3,ID_RECONNECT,L"Reconnect selected transport",365,410,311);
    label(3,L"Legacy USB-to-TCP/IP is not enabled by this application. No arbitrary network scanning is performed. Pairing codes are sent via stdin and never logged or saved.",36,462,1102,50);
    edit(3,ID_WIRELESS_RESULT,L"",36,531,1102,98,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);
    label(4,L"Bounded local diagnostics. Device serials are masked; exported diagnostics also mask the user-profile path and discovered network identifiers.",36,128,1102,43);edit(4,ID_LOG,L"",36,187,1102,392,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);button(4,ID_EXPORT,L"Export redacted diagnostics",36,595,312);
    edit(-1,ID_STATUS,L"Detecting dependencies; capture is stopped.",24,666,1126,153,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);add(-1,ID_NOTICE,L"STATIC",L"",SS_LEFT,24,832,1126,48);
    profile_to_controls();choose_page(0);
}
static LRESULT CALLBACK window_proc(HWND window,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_CREATE:ui.window=window;ui.dpi=(int)GetDpiForWindow(window);awm_app_apply_icons(window,ui.instance,(UINT)ui.dpi,&window_icons);ui.font=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");ui.heading=CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");create_controls();ui.engine=engine_create(window);SetTimer(window,1,250,NULL);return 0;
    case WM_COMMAND:{int id=LOWORD(w),event=HIWORD(w);if(ui.rendering)return 0;if(event==BN_CLICKED)on_button(id);else if(event==CBN_SELCHANGE){if(id==ID_CAMERA){read_profile(false);render_capabilities();}else if(id==ID_CODEC){int codec=cb_index(ID_CODEC);awm_copy(ui.profile.codec,sizeof(ui.profile.codec),codec==1?"h265":codec==2?"av1":"h264");ui.profile.encoder[0]=0;render_encoders();}else if(id==ID_PLAYBACK||id==ID_RECORDING)ui.setup_dirty=true;}else if(event==EN_CHANGE&&(id==ID_ADB_PATH||id==ID_SCRCPY_PATH||id==ID_OBS_PATH||id==ID_PORT||id==ID_PASSWORD))ui.setup_dirty=true;return 0;}
    case WM_NOTIFY:{NMHDR *n=(NMHDR *)l;if(n->idFrom==ID_TAB&&n->code==TCN_SELCHANGE)choose_page(TabCtrl_GetCurSel(ui.tab));if(n->idFrom==ID_DEVICES&&n->code==LVN_ITEMCHANGED&&!ui.rendering){NMLISTVIEW *v=(NMLISTVIEW *)l;if((v->uNewState&LVIS_SELECTED)&&!(v->uOldState&LVIS_SELECTED))select_device(v->iItem);}return 0;}
    case WM_TIMER:case AWM_UPDATE:update();return 0;
    case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO *)l;m->ptMinTrackSize.x=px(1190);m->ptMinTrackSize.y=px(937);return 0;}
    case WM_DPICHANGED:{ui.dpi=HIWORD(w);awm_app_apply_icons(window,ui.instance,(UINT)ui.dpi,&window_icons);RECT *r=(RECT *)l;SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);for(int i=0;i<ui.control_count;i++){Control *c=&ui.controls[i];MoveWindow(c->handle,px(c->x),px(c->y),px(c->w),px(c->h),TRUE);}return 0;}
    case WM_CLOSE:KillTimer(window,1);ShowWindow(window,SW_HIDE);awm_app_release_icons(&window_icons);engine_destroy(ui.engine);ui.engine=NULL;DestroyWindow(window);return 0;
    case WM_DESTROY:if(ui.font)DeleteObject(ui.font);if(ui.heading)DeleteObject(ui.heading);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR cmd,int show){
    (void)previous;(void)cmd;ui.instance=instance;ui.dpi=96;ui.view=calloc(1,sizeof(*ui.view));ui.configuration=calloc(1,sizeof(*ui.configuration));ui.previous_capabilities=calloc(1,sizeof(Capabilities));ui.previous_endpoints=calloc(1,sizeof(AudioEndpoints));if(!ui.view||!ui.configuration||!ui.previous_capabilities||!ui.previous_endpoints)return 1;profile_default(&ui.profile,0);CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX init={sizeof(init),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&init);WNDCLASSEXW c={0};c.cbSize=sizeof(c);c.hInstance=instance;c.lpfnWndProc=window_proc;c.lpszClassName=L"WinDroidCaptureWindow";c.hCursor=LoadCursorW(NULL,IDC_ARROW);c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON));c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassExW(&c);
    HWND window=CreateWindowExW(0,c.lpszClassName,L"WinDroid Capture",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1190,937,NULL,NULL,instance,NULL);if(!window)return 1;SetWindowPos(window,NULL,0,0,px(1190),px(937),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(window,show);UpdateWindow(window);
    MSG message;while(GetMessageW(&message,NULL,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    SecureZeroMemory(ui.configuration,sizeof(*ui.configuration));free(ui.configuration);free(ui.view);free(ui.previous_capabilities);free(ui.previous_endpoints);CoUninitialize();return (int)message.wParam;
}
