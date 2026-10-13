#include "app_ui.h"
#include "../device_profile.h"
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int number(HWND h){wchar_t s[64],*end;GetWindowTextW(h,s,64);long n=wcstol(s,&end,10);return end==s||*end||n<0||n>100000?0:(int)n;}
static void integer(HWND h,int value){wchar_t s[64];swprintf(s,64,L"%d",value);ui_set(h,s);}
void capture_page_create(AppUI *a){
    HWND p=a->pages[0];ui_field(a,p,UI_DEVICE,L"&Device");ui_combo(a,p,UI_DEVICE);ui_item(a->controls[UI_DEVICE],L"Select an authorized device");ui_select(a->controls[UI_DEVICE],0);ui_control(a,p,L"STATIC",L"No selection",0,UI_TRANSPORT);ui_button(a,p,L"Refresh",UI_CAPTURE_REFRESH);
    ui_field(a,p,UI_SOURCE,L"&Video source");ui_combo(a,p,UI_SOURCE);ui_item(a->controls[UI_SOURCE],L"Phone camera");ui_item(a->controls[UI_SOURCE],L"Phone screen");ui_select(a->controls[UI_SOURCE],(int)(0));
    a->canvas=CreateWindowExW(0,L"AWMCompactCanvas",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,p,NULL,a->instance,NULL);
    a->placeholder=CreateWindowExW(0,L"AWMCompactPlaceholder",L"Output preview\nSelect a device and source",WS_CHILD|WS_VISIBLE,0,0,1,1,p,NULL,a->instance,NULL);
    a->property=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_COMPOSITED,L"AWMCompactPane",L"Source settings",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,p,NULL,a->instance,NULL);
    a->property_viewport=CreateWindowExW(WS_EX_CONTROLPARENT,L"AWMCompactPane",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,a->property,NULL,a->instance,NULL);
    a->property_content=CreateWindowExW(WS_EX_CONTROLPARENT,L"AWMCompactPane",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,a->property_viewport,NULL,a->instance,NULL);
    ui_control(a,p,L"STATIC",L"Output preview",0,UI_PREVIEW_TITLE);ui_control(a,p,L"STATIC",L"Source settings",0,UI_SOURCE_TITLE);ui_control(a,p,L"STATIC",L"Windows inputs",0,UI_OUTPUT_TITLE);
    ui_control(a,a->property,L"SCROLLBAR",L"Source settings scroll",SBS_VERT|WS_TABSTOP,UI_PROPERTY_SCROLL);
    int ids[]={UI_SELECTOR,UI_PRESET,UI_ORIENTATION,UI_FRAMING,UI_AUDIO};const wchar_t *labels[]={L"Camera",L"Profile",L"Output rotation",L"Framing",L"Audio"};for(int i=0;i<5;i++){ui_field(a,a->property_content,ids[i],labels[i]);ui_combo(a,a->property_content,ids[i]);}
    ui_item(a->controls[UI_FRAMING],L"Fit — preserve all content");ui_item(a->controls[UI_FRAMING],L"Crop to fill — edges removed");ui_item(a->controls[UI_AUDIO],L"Off");ui_item(a->controls[UI_AUDIO],L"Phone microphone");
    ui_button(a,p,L"Advanced",UI_ADVANCED);ui_control(a,p,L"STATIC",L"",0,UI_SAVED);ui_show_mode(a->controls[UI_SAVED],SW_HIDE);
    int edit_ids[]={UI_WIDTH,UI_HEIGHT,UI_FPS,UI_BITRATE,UI_ENCODER,UI_ZOOM};const wchar_t *edit_labels[]={L"Camera width / Screen long-edge limit (0 = Native)",L"Camera height",L"Camera FPS / Screen FPS limit",L"Video bitrate (Mbps)",L"Encoder (blank = device choice)",L"Camera zoom (reported range)"};for(int i=0;i<6;i++){ui_field(a,a->property_content,edit_ids[i],edit_labels[i]);ui_edit(a,a->property_content,edit_ids[i],0);}
    ui_field(a,a->property_content,UI_CODEC,L"Video codec");ui_combo(a,a->property_content,UI_CODEC);ui_item(a->controls[UI_CODEC],L"H.264");ui_item(a->controls[UI_CODEC],L"H.265");ui_item(a->controls[UI_CODEC],L"AV1");
    ui_field(a,a->property_content,UI_ROTATION,L"Rotate output clockwise");ui_combo(a,a->property_content,UI_ROTATION);const wchar_t *rot[]={L"0°",L"90°",L"180°",L"270°"};for(int i=0;i<4;i++)ui_item(a->controls[UI_ROTATION],rot[i]);
    ui_control(a,a->property_content,L"BUTTON",L"Mirror output horizontally",BS_AUTOCHECKBOX|WS_TABSTOP,UI_MIRROR);
    ui_button(a,p,L"Hide preview",UI_HIDE);
    ui_field(a,p,UI_CAMERA_NAME,L"Windows camera");ui_edit(a,p,UI_CAMERA_NAME,ES_READONLY);ui_button(a,p,L"Copy name",UI_COPY_CAMERA);
    ui_field(a,p,UI_MIC_NAME,L"Phone microphone");ui_edit(a,p,UI_MIC_NAME,ES_READONLY);ui_button(a,p,L"Copy name",UI_COPY_MIC);
    ui_control(a,p,L"STATIC",L"Receiving app: Not checked",0,UI_RECEIVER);ui_button(a,p,L"Test input",UI_TEST);ui_profile_controls(a);
}
void capture_page_layout(AppUI *a,int w,int h){
    int right=w-304,body_top=110,body_height=h-252;if(body_height<190)body_height=190;
    ui_move(a,a->controls[UI_DEVICE+128],0,1,55,23);ui_move(a,a->controls[UI_DEVICE],64,0,w-386,230);ui_move(a,a->controls[UI_TRANSPORT],w-310,4,196,24);
    ui_move(a,a->controls[UI_SOURCE+128],0,39,83,24);ui_move(a,a->controls[UI_SOURCE],92,36,240,200);ui_move(a,a->controls[UI_CAPTURE_REFRESH],w-102,0,102,28);
    ui_move(a,a->controls[UI_PREVIEW_TITLE],0,78,right-146,20);ui_move(a,a->controls[UI_HIDE],right-134,74,110,28);
    ui_move(a,a->controls[UI_SOURCE_TITLE],right+10,78,146,20);ui_move(a,a->controls[UI_ADVANCED],w-128,74,118,28);ui_move(a,a->controls[UI_OUTPUT_TITLE],0,h-125,right-24,20);
    int vw=right-24,vh=vw*9/16;if(vh>body_height){vh=body_height;vw=vh*16/9;}int ox=(right-24-vw)/2;
    ui_move(a,a->canvas,ox,body_top,vw,vh);ui_move(a,a->placeholder,ox,body_top,vw,vh);
    /* Windows inputs belong below the preview. The settings column can use the
       full remaining height, including the space alongside those output rows. */
    ui_move(a,a->property,right+10,110,284,h-128);capture_property_layout(a,h-128);
    int out=h-102;ui_move(a,a->controls[UI_CAMERA_NAME+128],0,out+4,118,24);ui_move(a,a->controls[UI_CAMERA_NAME],126,out,right-258,28);ui_move(a,a->controls[UI_COPY_CAMERA],right-122,out,98,28);
    ui_move(a,a->controls[UI_MIC_NAME+128],0,out+37,118,24);ui_move(a,a->controls[UI_MIC_NAME],126,out+33,right-258,28);ui_move(a,a->controls[UI_COPY_MIC],right-122,out+33,98,28);
    ui_move(a,a->controls[UI_RECEIVER],0,out+71,right-136,22);ui_move(a,a->controls[UI_TEST],right-122,out+67,98,28);
}
void capture_property_layout(AppUI *a,int body_height){
    RECT bounds;GetClientRect(a->property,&bounds);int width=MulDiv(bounds.right,96,a->dpi),fields_width=width-28;
    bool zoom=a->profile.kind==CAPTURE_CAMERA&&a->view->features.zoom;
    Camera *camera=NULL;for(int i=0;i<a->view->capabilities.camera_count;i++)if(!strcmp(a->view->capabilities.cameras[i].id,a->profile.camera.id))camera=&a->view->capabilities.cameras[i];
    zoom=zoom&&camera&&!camera->physical&&camera->zoom_min>0&&camera->zoom_max>camera->zoom_min;
    int content=a->advanced?(zoom?687:632):264;int maximum=content-body_height;if(maximum<0)maximum=0;if(a->property_scroll>maximum)a->property_scroll=maximum;
    int s=a->property_scroll;int main_ids[]={UI_SELECTOR,UI_PRESET,UI_ORIENTATION,UI_FRAMING,UI_AUDIO};for(int i=0;i<5;i++)ui_field_layout(a,main_ids[i],0,4+i*52,fields_width);
    int y=270;int fields[]={UI_WIDTH,UI_HEIGHT,UI_FPS,UI_CODEC,UI_BITRATE,UI_ENCODER,UI_ROTATION,UI_ZOOM};for(int i=0;i<8;i++){
        int id=fields[i];bool visible=a->advanced&&!(id==UI_HEIGHT&&a->profile.kind==CAPTURE_SCREEN)&&!(id==UI_ZOOM&&!zoom)&&!(id==UI_ROTATION&&a->profile.kind==CAPTURE_CAMERA);
        ui_show_mode(a->controls[id],visible?SW_SHOW:SW_HIDE);ui_show_mode(a->controls[id+128],visible?SW_SHOW:SW_HIDE);if(visible){ui_field_layout(a,id,0,y,fields_width);y+=55;}
    }
    ui_show_mode(a->controls[UI_MIRROR],a->advanced?SW_SHOW:SW_HIDE);if(a->advanced){ui_move(a,a->controls[UI_MIRROR],0,y,fields_width,26);y+=32;}else y=264;
    a->property_fields_width=fields_width;a->property_content_height=y;
    ui_move(a,a->property_viewport,0,0,fields_width,body_height);ui_move(a,a->property_content,0,-s,fields_width,y);
    SCROLLINFO info={0};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;info.nMax=y-1;info.nPage=(UINT)body_height;info.nPos=s;SetScrollInfo(a->controls[UI_PROPERTY_SCROLL],SB_CTL,&info,FALSE);ui_show_mode(a->controls[UI_PROPERTY_SCROLL],y>body_height?SW_SHOW:SW_HIDE);ui_move(a,a->controls[UI_PROPERTY_SCROLL],width-18,2,18,body_height-4);InvalidateRect(a->controls[UI_PROPERTY_SCROLL],NULL,FALSE);
}
void capture_property_scroll(AppUI *a){
    /* Only one clipped content window moves. Its native fields keep stable
       coordinates; no partial sequence of individually shifted controls. */
    BOOL moved=SetWindowPos(a->property_content,NULL,0,-MulDiv(a->property_scroll,a->dpi,96),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_DEFERERASE);
    SCROLLINFO info={0};info.cbSize=sizeof(info);info.fMask=SIF_POS;info.nPos=a->property_scroll;SetScrollInfo(a->controls[UI_PROPERTY_SCROLL],SB_CTL,&info,FALSE);
    RedrawWindow(a->property,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
    wchar_t trace_path[AWM_PATH];if(GetEnvironmentVariableW(L"AWM_UI_LAYOUT_TRACE",trace_path,AWM_PATH)){
        RECT actual,field;GetWindowRect(a->property_content,&actual);MapWindowPoints(NULL,a->property_viewport,(POINT *)&actual,2);GetWindowRect(a->controls[UI_SELECTOR],&field);MapWindowPoints(NULL,a->property_content,(POINT *)&field,2);
        FILE *trace=_wfopen(trace_path,L"a");if(trace){fprintf(trace,"SCROLL position=%d moved=%d content_top=%ld field_top=%ld\n",a->property_scroll,moved,actual.top,field.top);fclose(trace);}
    }
}
void ui_profile_controls(AppUI *a){
    a->loading=true;NativeCaptureProfile *p=&a->profile;
    ui_set(a->controls[UI_WIDTH+128],p->kind==CAPTURE_SCREEN?L"Long-edge limit (0 = Native)":L"Requested camera width");
    ui_set(a->controls[UI_FPS+128],p->kind==CAPTURE_SCREEN?L"Frame-rate limit":L"Requested camera FPS");ui_select(a->controls[UI_SOURCE],(int)(p->kind));SendMessageW(a->controls[UI_PRESET],CB_RESETCONTENT,0,0);
    const wchar_t *camera[]={L"Standard · 1080p / 30 requested",L"Detail · 4K / 30 requested",L"Motion · 1080p / 60 requested",L"Custom"};const wchar_t *screen[]={L"Balanced · 1920 limit / 30 cap",L"Native · display size / 30 cap",L"Motion · 1920 limit / 60 cap",L"Custom"};for(int i=0;i<4;i++)ui_item(a->controls[UI_PRESET],p->kind==CAPTURE_SCREEN?screen[i]:camera[i]);
    int preset=3;for(int i=0;i<3;i++){NativeCaptureProfile candidate;native_capture_default(&candidate,p->kind,i);bool mode=p->kind==CAPTURE_CAMERA?candidate.camera.width==p->camera.width&&candidate.camera.height==p->camera.height&&candidate.camera.fps==p->camera.fps:candidate.screen.max_size==p->screen.max_size&&candidate.screen.fps_limit==p->screen.fps_limit;if(mode&&candidate.encoding.bitrate_mbps==p->encoding.bitrate_mbps&&!strcmp(candidate.encoding.codec,p->encoding.codec))preset=i;}
    ui_select(a->controls[UI_PRESET],(int)(preset));ui_set(a->controls[UI_SELECTOR+128],p->kind==CAPTURE_SCREEN?L"Display":L"Camera");ui_set(a->controls[UI_ORIENTATION+128],p->kind==CAPTURE_SCREEN?L"Orientation":L"Output rotation");SendMessageW(a->controls[UI_ORIENTATION],CB_RESETCONTENT,0,0);
    const wchar_t *rot[]={L"0°",L"90° clockwise",L"180°",L"270° clockwise"};if(p->kind==CAPTURE_SCREEN){ui_item(a->controls[UI_ORIENTATION],L"Follow phone");ui_item(a->controls[UI_ORIENTATION],L"Lock current orientation");}else for(int i=0;i<4;i++)ui_item(a->controls[UI_ORIENTATION],rot[i]);
    ui_select(a->controls[UI_ORIENTATION],(int)(p->kind==CAPTURE_SCREEN?p->screen.lock_orientation:p->rotation/90));ui_select(a->controls[UI_FRAMING],(int)(p->framing));ui_select(a->controls[UI_AUDIO],(int)(p->audio?1:0));ui_check(a->controls[UI_MIRROR],p->mirror);ui_select(a->controls[UI_ROTATION],(int)(p->rotation/90));
    integer(a->controls[UI_WIDTH],p->kind==CAPTURE_SCREEN?p->screen.max_size:p->camera.width);integer(a->controls[UI_HEIGHT],p->camera.height);integer(a->controls[UI_FPS],p->kind==CAPTURE_SCREEN?p->screen.fps_limit:p->camera.fps);integer(a->controls[UI_BITRATE],p->encoding.bitrate_mbps);
    ui_select(a->controls[UI_CODEC],(int)(!strcmp(p->encoding.codec,"h265")?1:!strcmp(p->encoding.codec,"av1")?2:0));ui_text(a->controls[UI_ENCODER],p->encoding.encoder);wchar_t zoom[64];swprintf(zoom,64,L"%.3f",p->camera.zoom);ui_set(a->controls[UI_ZOOM],zoom);a->loading=false;
}
void capture_page_update(AppUI *a){
    bool locked=a->view->active||a->view->phase==N_RECONNECTING||a->view->busy;int disabled[]={UI_DEVICE,UI_SOURCE,UI_SELECTOR,UI_PRESET,UI_ORIENTATION,UI_FRAMING,UI_AUDIO,UI_WIDTH,UI_HEIGHT,UI_FPS,UI_BITRATE,UI_CODEC,UI_ENCODER,UI_ROTATION,UI_MIRROR,UI_ZOOM};for(unsigned i=0;i<ARRAYSIZE(disabled);i++)ui_enable(a->controls[disabled[i]],!locked);
    bool match=!strcmp(a->view->capabilities_serial,a->selected.serial)&&!strcmp(a->view->capabilities_transport,a->selected.transport_id)&&a->view->capabilities_kind==a->profile.kind&&a->view->capabilities_serial[0];
    char key[1024];snprintf(key,sizeof(key),"%.255s/%.31s/%d/%.255s/%d/%d",match?a->view->capabilities_serial:"",a->view->capabilities_transport,a->profile.kind,a->view->capabilities_version,a->view->capabilities.camera_count,a->view->displays.count);
    if(strcmp(key,a->caps_key)){awm_copy(a->caps_key,sizeof(a->caps_key),key);a->loading=true;SendMessageW(a->controls[UI_SELECTOR],CB_RESETCONTENT,0,0);ui_item(a->controls[UI_SELECTOR],a->profile.kind==CAPTURE_SCREEN?L"Select an enumerated display":L"Select an enumerated camera");int selection=0;
        if(match&&a->profile.kind==CAPTURE_SCREEN){for(int i=0;i<a->view->displays.count;i++){PhoneDisplay *d=&a->view->displays.items[i];wchar_t label[256];if(d->size_known)swprintf(label,256,L"Display %d · %d × %d reported",d->id,d->width,d->height);else swprintf(label,256,L"Display %d · size unknown",d->id);ui_item(a->controls[UI_SELECTOR],label);if(d->id==a->profile.screen.display_id)selection=i+1;if(a->profile.screen.display_id<0&&d->id==0){a->profile.screen.display_id=0;selection=i+1;}}}
        else if(match){for(int i=0;i<a->view->capabilities.camera_count;i++){Camera *c=&a->view->capabilities.cameras[i];wchar_t label[256];char text[256];native_camera_label(&a->selected,&a->view->capabilities,c,text,sizeof(text));utf8_to_wide(text,label,256);ui_item(a->controls[UI_SELECTOR],label);if(!strcmp(c->id,a->profile.camera.id))selection=i+1;}}
        ui_select(a->controls[UI_SELECTOR],(int)(selection));a->loading=false;if(selection)ui_save_profile(a);
    }
    char transport[512];snprintf(transport,sizeof(transport),"%s · %s",transport_name(a->selected.transport),a->selected.serial[0]?device_state_name(a->selected.state):"not selected");ui_text(a->controls[UI_TRANSPORT],transport);
    ui_set(a->controls[UI_CAMERA_NAME],a->view->camera_registered?a->view->camera_name:L"Not installed / not registered");ui_set(a->controls[UI_MIC_NAME],a->view->microphone_installed?a->view->microphone_name:L"Not installed — signed driver required");ui_enable(a->controls[UI_COPY_CAMERA],a->view->camera_registered);ui_set(a->controls[UI_COPY_MIC],a->view->microphone_installed?L"Copy name":L"Setup");ui_enable(a->controls[UI_COPY_MIC],TRUE);ui_enable(a->controls[UI_TEST],a->view->active&&a->view->phase==N_RUNNING&&!a->receiver_started);
    NativeCaptureProfile saved;if(a->profile_dirty&&a->view->profile_saved&&native_capture_get(&a->view->settings,a->selected.serial,a->profile.kind,&saved)&&!memcmp(&saved,&a->profile,sizeof(saved)))a->profile_dirty=false;
    ui_set(a->controls[UI_SAVED],a->profile_dirty?L"Not saved / pending":a->view->profile_saved?L"Saved":a->selected.serial[0]?L"Not saved yet":L"");
}
bool capture_page_command(AppUI *a,int id,int code){
    if(id==UI_CAPTURE_REFRESH){ui_send(a,N_REFRESH);return true;}
    if(id==UI_DEVICE&&code==CBN_SELCHANGE){int i=(int)SendMessageW(a->controls[id],CB_GETCURSEL,0,0)-1;if(i>=0&&i<a->view->devices.count)ui_select_device(a,&a->view->devices.items[i]);else{memset(&a->selected,0,sizeof(a->selected));a->probing=false;a->caps_key[0]=0;native_capture_default(&a->profile,a->profile.kind,0);ui_profile_controls(a);ui_layout(a);}return true;}
    if(id==UI_SOURCE&&code==CBN_SELCHANGE){CaptureKind kind=(CaptureKind)SendMessageW(a->controls[id],CB_GETCURSEL,0,0);native_capture_get(&a->view->settings,a->selected.serial,kind,&a->profile);a->caps_key[0]=0;a->property_scroll=0;ui_profile_controls(a);a->probe_revision=a->view->probe_revision;a->probing=a->selected.state==DEV_AUTHORIZED;if(a->probing)ui_send(a,N_PROBE);ui_layout(a);return true;}
    if(id==UI_SELECTOR&&code==CBN_SELCHANGE){int i=(int)SendMessageW(a->controls[id],CB_GETCURSEL,0,0)-1;if(i>=0){if(a->profile.kind==CAPTURE_SCREEN&&i<a->view->displays.count)a->profile.screen.display_id=a->view->displays.items[i].id;else if(a->profile.kind==CAPTURE_CAMERA&&i<a->view->capabilities.camera_count){Camera *c=&a->view->capabilities.cameras[i];awm_copy(a->profile.camera.id,80,c->id);if(c->physical||(c->zoom_min>0&&(a->profile.camera.zoom<c->zoom_min||a->profile.camera.zoom>c->zoom_max)))a->profile.camera.zoom=c->zoom_min<=1&&c->zoom_max>=1?1:c->zoom_min;ui_profile_controls(a);ui_layout(a);}}ui_save_profile(a);return true;}
    if(id==UI_PRESET&&code==CBN_SELCHANGE){int i=(int)SendMessageW(a->controls[id],CB_GETCURSEL,0,0);if(i<3&&i>=0){NativeCaptureProfile old=a->profile;native_capture_default(&a->profile,old.kind,i);a->profile.camera=old.camera;if(old.kind==CAPTURE_CAMERA){NativeCaptureProfile request;native_capture_default(&request,old.kind,i);a->profile.camera.width=request.camera.width;a->profile.camera.height=request.camera.height;a->profile.camera.fps=request.camera.fps;}a->profile.screen.display_id=old.screen.display_id;a->profile.screen.lock_orientation=old.screen.lock_orientation;a->profile.audio=old.audio;a->profile.rotation=old.rotation;a->profile.framing=old.framing;a->profile.mirror=old.mirror;a->profile.reconnect=old.reconnect;ui_profile_controls(a);ui_save_profile(a);}return true;}
    if((id==UI_ORIENTATION||id==UI_FRAMING||id==UI_AUDIO||id==UI_CODEC||id==UI_ROTATION)&&code==CBN_SELCHANGE){int i=(int)SendMessageW(a->controls[id],CB_GETCURSEL,0,0);if(id==UI_ORIENTATION){if(a->profile.kind==CAPTURE_SCREEN)a->profile.screen.lock_orientation=i==1;else a->profile.rotation=i*90;}else if(id==UI_FRAMING)a->profile.framing=(CaptureFraming)i;else if(id==UI_AUDIO)a->profile.audio=i==1;else if(id==UI_ROTATION)a->profile.rotation=i*90;else awm_copy(a->profile.encoding.codec,32,i==1?"h265":i==2?"av1":"h264");ui_save_profile(a);return true;}
    if(id==UI_MIRROR){a->profile.mirror=SendMessageW(a->controls[id],BM_GETCHECK,0,0)==BST_CHECKED;ui_save_profile(a);return true;}
    if((id==UI_WIDTH||id==UI_HEIGHT||id==UI_FPS||id==UI_BITRATE||id==UI_ENCODER||id==UI_ZOOM)&&code==EN_KILLFOCUS){
        if(id==UI_WIDTH){if(a->profile.kind==CAPTURE_SCREEN)a->profile.screen.max_size=number(a->controls[id]);else a->profile.camera.width=number(a->controls[id]);}else if(id==UI_HEIGHT)a->profile.camera.height=number(a->controls[id]);else if(id==UI_FPS){if(a->profile.kind==CAPTURE_SCREEN)a->profile.screen.fps_limit=number(a->controls[id]);else a->profile.camera.fps=number(a->controls[id]);}else if(id==UI_BITRATE)a->profile.encoding.bitrate_mbps=number(a->controls[id]);else{wchar_t s[256];GetWindowTextW(a->controls[id],s,256);if(id==UI_ENCODER)wide_to_utf8(s,a->profile.encoding.encoder,256);else a->profile.camera.zoom=(float)wcstod(s,NULL);}ui_save_profile(a);return true;
    }
    if(id==UI_ADVANCED){a->advanced=!a->advanced;a->property_scroll=0;InvalidateRect(a->controls[id],NULL,FALSE);ui_layout(a);return true;}
    if(id==UI_HIDE){a->hide=!a->hide;ui_set(a->controls[id],a->hide?L"Show preview":L"Hide preview");ui_preview_state(a);return true;}
    if(id==UI_COPY_MIC&&!a->view->microphone_installed){a->page=2;a->settings_page=0;TabCtrl_SetCurSel(a->controls[UI_TABS],2);TabCtrl_SetCurSel(a->controls[UI_SETTINGS_TABS],0);ui_layout(a);SetFocus(a->controls[UI_INSTALL_MIC]);return true;}
    if(id==UI_COPY_CAMERA||id==UI_COPY_MIC){ui_copy(a,id==UI_COPY_CAMERA?a->view->camera_name:a->view->microphone_name);return true;}
    if(id==UI_TEST){wchar_t exe[AWM_PATH],b[64];GetModuleFileNameW(NULL,exe,AWM_PATH);PathRemoveFileSpecW(exe);PathAppendW(exe,L"awm-native-receiver.exe");Arguments *args=calloc(1,sizeof(*args));if(!args)return true;args_add(args,exe);args_add(args,L"--duration");args_add(args,L"90");if(!a->profile.audio)args_add(args,L"--video-only");args_add(args,L"--width");swprintf(b,64,L"%u",a->view->settings.output_width);args_add(args,b);args_add(args,L"--height");swprintf(b,64,L"%u",a->view->settings.output_height);args_add(args,b);args_add(args,L"--fps");swprintf(b,64,L"%u",a->view->settings.output_fps);args_add(args,b);char error[1024];a->receiver_started=process_start(&a->receiver,args,a->view->settings.adb,NULL,true,error,sizeof(error));free(args);ui_set(a->controls[UI_NOTICE],L"Local receiver only. Confirm the phone image; close other camera tests if it cannot start.");swprintf(a->receiver_state,256,L"Receiving app: %s",a->receiver_started?L"local test running":L"test could not start");return true;}
    return false;
}
