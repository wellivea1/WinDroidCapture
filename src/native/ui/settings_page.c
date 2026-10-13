#include "app_ui.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
static void browse(AppUI *a,int id){wchar_t file[AWM_PATH]=L"";OPENFILENAMEW f={0};f.lStructSize=sizeof(f);f.hwndOwner=a->window;f.lpstrFilter=L"Executables\0*.exe\0\0";f.lpstrFile=file;f.nMaxFile=AWM_PATH;f.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;if(GetOpenFileNameW(&f))ui_set(a->controls[id],file);}
static RECT section_rect(AppUI *a,int left,int top,int right,int bottom){RECT r={MulDiv(left,a->dpi,96),MulDiv(top,a->dpi,96),MulDiv(right,a->dpi,96),MulDiv(bottom,a->dpi,96)};return r;}
void settings_page_create(AppUI *a){
    HWND p=a->pages[2];ui_control(a,p,WC_TABCONTROLW,L"Settings sections",WS_TABSTOP|TCS_FIXEDWIDTH,UI_SETTINGS_TABS);const wchar_t *names[]={L"Output",L"Dependencies",L"Advanced"};for(int i=0;i<3;i++){TCITEMW item={0};item.mask=TCIF_TEXT;item.pszText=(LPWSTR)names[i];TabCtrl_InsertItem(a->controls[UI_SETTINGS_TABS],i,&item);a->settings_pages[i]=CreateWindowExW(WS_EX_CONTROLPARENT,L"AWMCompactPane",names[i],WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,p,NULL,a->instance,NULL);}
    p=a->settings_pages[0];ui_field(a,p,UI_OUTPUT_FORMAT,L"Preferred output / local receiver request");ui_combo(a,p,UI_OUTPUT_FORMAT);const wchar_t *formats[]={L"1920 × 1080 / 30",L"1280 × 720 / 30",L"3840 × 2160 / 30",L"1920 × 1080 / 60"};for(int i=0;i<4;i++)ui_item(a->controls[UI_OUTPUT_FORMAT],formats[i]);
    ui_field(a,p,UI_PREVIEW_SCALE,L"Preview scaling — applies immediately");ui_combo(a,p,UI_PREVIEW_SCALE);ui_field(a,p,UI_PIPELINE_SCALE,L"Pipeline scaling — next Start");ui_combo(a,p,UI_PIPELINE_SCALE);const wchar_t *filters[]={L"EWA Lanczos sharp",L"Lanczos 3",L"Bilinear"};for(int i=0;i<3;i++){ui_item(a->controls[UI_PREVIEW_SCALE],filters[i]);ui_item(a->controls[UI_PIPELINE_SCALE],filters[i]);}
    ui_control(a,p,L"STATIC",L"Output and appearance",0,UI_OUTPUT_SETTINGS_TITLE);ui_control(a,p,L"STATIC",L"Windows inputs",0,UI_COMPONENT_TITLE);
    ui_field(a,p,UI_THEME,L"Appearance — applies immediately");ui_combo(a,p,UI_THEME);ui_item(a->controls[UI_THEME],L"System");ui_item(a->controls[UI_THEME],L"Light");ui_item(a->controls[UI_THEME],L"Dark");
    ui_field(a,p,UI_VIDEO_PACING,L"Presentation pacing — next Start");ui_combo(a,p,UI_VIDEO_PACING);ui_item(a->controls[UI_VIDEO_PACING],L"Smooth priority — buffer one frame");ui_item(a->controls[UI_VIDEO_PACING],L"Minimum latency — present on arrival");
    ui_control(a,p,L"STATIC",L"Checking output components…",0,UI_COMPONENTS);ui_button(a,p,L"Install camera source",UI_INSTALL_CAMERA);ui_button(a,p,L"Register camera",UI_REGISTER_CAMERA);ui_button(a,p,L"Microphone setup",UI_INSTALL_MIC);
    ui_control(a,p,L"STATIC",L"Virtual camera",0,UI_CAMERA_SETUP_TITLE);ui_control(a,p,L"STATIC",L"Phone microphone",0,UI_MIC_SETUP_TITLE);ui_control(a,p,L"STATIC",L"Checking microphone…",0,UI_MIC_SETUP_TEXT);
    ui_control(a,p,L"STATIC",L"A listed input still needs a receiving-app test. Preferred output is the format requested by the local test; receiving apps negotiate their own format.",0,UI_COMPONENT_HELP);
    p=a->settings_pages[1];ui_field(a,p,UI_ADB,L"Selected ADB executable (used for discovery and scrcpy)");ui_edit(a,p,UI_ADB,0);ui_button(a,p,L"Browse…",UI_BROWSE_ADB);ui_field(a,p,UI_SCRCPY,L"Stock scrcpy executable (or scrcpy.exe inside extracted distribution)");ui_edit(a,p,UI_SCRCPY,0);ui_button(a,p,L"Browse…",UI_BROWSE_SCRCPY);ui_button(a,p,L"Validate and Apply",UI_APPLY_PATHS);
    ui_field(a,p,UI_CAPTURE_PATH,L"Managed capture client — private export support required");ui_edit(a,p,UI_CAPTURE_PATH,ES_READONLY);ui_control(a,p,L"STATIC",L"Exact versions appear after validation.",0,UI_VERSIONS);ui_button(a,p,L"Official ADB setup",UI_ADB_LINK);ui_button(a,p,L"Official scrcpy releases",UI_SCRCPY_LINK);
    p=a->settings_pages[2];ui_control(a,p,L"BUTTON",L"Reconnect the same selected transport (up to 3 retries)",BS_AUTOCHECKBOX|WS_TABSTOP,UI_RECONNECT);ui_button(a,p,L"Driver signing information",UI_DRIVER_LINK);
    ui_control(a,p,L"STATIC",L"Session behavior",0,UI_BEHAVIOR_TITLE);
    ui_control(a,p,L"STATIC",L"Capture, framing and encoding changes apply on the next Start.\nSource-specific controls are in Capture → Advanced.\n\nCapture does not start automatically. Reconnection is opt-in and stays on the selected transport.",0,UI_BEHAVIOR_INFO);
    ui_control(a,p,L"STATIC",L"Microphone driver",0,UI_DRIVER_TITLE);
    ui_control(a,p,L"STATIC",L"The microphone requires a validated, signed kernel driver. The current development package is unsigned and is not installed.",0,UI_DRIVER_HELP);
}
void settings_page_layout(AppUI *a,int w,int h){
    TabCtrl_SetItemSize(a->controls[UI_SETTINGS_TABS],MulDiv(112,a->dpi,96),MulDiv(28,a->dpi,96));ui_move(a,a->controls[UI_SETTINGS_TABS],0,0,w,30);for(int i=0;i<3;i++){ui_move(a,a->settings_pages[i],0,40,w,h-40);ui_show_mode(a->settings_pages[i],a->settings_page==i?SW_SHOW:SW_HIDE);}
    int form=w<980?420:460,setup=form+40,setup_width=w-setup-12;
    a->settings_sections[0]=section_rect(a,form+28,6,w,420);
    ui_move(a,a->controls[UI_OUTPUT_SETTINGS_TITLE],0,6,form,20);ui_move(a,a->controls[UI_COMPONENT_TITLE],setup,18,setup_width,20);ui_field_layout(a,UI_OUTPUT_FORMAT,0,36,form);ui_field_layout(a,UI_PREVIEW_SCALE,0,104,form);ui_field_layout(a,UI_PIPELINE_SCALE,0,172,form);ui_field_layout(a,UI_VIDEO_PACING,0,240,form);ui_field_layout(a,UI_THEME,0,308,form);
    ui_move(a,a->controls[UI_CAMERA_SETUP_TITLE],setup,54,setup_width,20);ui_move(a,a->controls[UI_COMPONENTS],setup,82,setup_width,44);
    ui_move(a,a->controls[UI_INSTALL_CAMERA],setup,134,180,30);ui_move(a,a->controls[UI_REGISTER_CAMERA],setup+192,134,150,30);
    ui_move(a,a->controls[UI_MIC_SETUP_TITLE],setup,194,setup_width,20);ui_move(a,a->controls[UI_MIC_SETUP_TEXT],setup,222,setup_width,48);ui_move(a,a->controls[UI_INSTALL_MIC],setup,280,180,30);ui_move(a,a->controls[UI_COMPONENT_HELP],setup,330,setup_width,72);
    ui_field_layout(a,UI_ADB,0,6,w-110);ui_move(a,a->controls[UI_BROWSE_ADB],w-98,27,98,28);ui_field_layout(a,UI_SCRCPY,0,77,w-110);ui_move(a,a->controls[UI_BROWSE_SCRCPY],w-98,98,98,28);ui_move(a,a->controls[UI_APPLY_PATHS],0,144,170,30);ui_field_layout(a,UI_CAPTURE_PATH,0,195,w);
    ui_move(a,a->controls[UI_VERSIONS],0,262,w,h-366);ui_move(a,a->controls[UI_ADB_LINK],0,h-83,174,30);ui_move(a,a->controls[UI_SCRCPY_LINK],186,h-83,192,30);
    int advanced_width=w<740?w:740;
    a->settings_sections[1]=section_rect(a,0,6,advanced_width,188);a->settings_sections[2]=section_rect(a,0,200,advanced_width,354);
    ui_move(a,a->controls[UI_BEHAVIOR_TITLE],12,18,advanced_width-24,20);ui_move(a,a->controls[UI_RECONNECT],12,50,advanced_width-24,28);ui_move(a,a->controls[UI_BEHAVIOR_INFO],12,92,advanced_width-24,80);
    ui_move(a,a->controls[UI_DRIVER_TITLE],12,212,advanced_width-24,20);ui_move(a,a->controls[UI_DRIVER_HELP],12,244,advanced_width-24,48);ui_move(a,a->controls[UI_DRIVER_LINK],12,308,216,30);
}
void settings_page_update(AppUI *a){
    if(!a->settings_loaded&&a->view->adb_version[0]){a->loading=true;ui_set(a->controls[UI_ADB],a->view->settings.adb);ui_set(a->controls[UI_SCRCPY],a->view->settings.scrcpy);a->settings_loaded=true;a->loading=false;}
    if(a->settings_pending&&a->view->settings.preview_scaler==a->pending_preview&&a->view->settings.pipeline_scaler==a->pending_pipeline&&a->view->settings.output_width==a->pending_width&&a->view->settings.output_fps==a->pending_fps&&a->view->settings.ui_theme==a->pending_theme&&a->view->settings.video_buffer==a->pending_pacing)a->settings_pending=false;
    if(!a->settings_pending){a->loading=true;ui_select(a->controls[UI_THEME],(int)(a->view->settings.ui_theme));ui_select(a->controls[UI_VIDEO_PACING],a->view->settings.video_buffer?0:1);ui_select(a->controls[UI_PREVIEW_SCALE],(int)(a->view->settings.preview_scaler));ui_select(a->controls[UI_PIPELINE_SCALE],(int)(a->view->settings.pipeline_scaler));unsigned width=a->view->settings.output_width,fps=a->view->settings.output_fps;int format=width==1280?1:width==3840?2:fps==60?3:0;ui_select(a->controls[UI_OUTPUT_FORMAT],(int)(format));a->loading=false;}a->loading=true;ui_check(a->controls[UI_RECONNECT],a->profile.reconnect);a->loading=false;
    wchar_t status[2400];swprintf(status,2400,L"%s\n%s",a->view->camera_registered?L"Installed / enumerated":L"Not registered",a->view->camera_registered?a->view->camera_name:L"Install the camera source, then register its Windows input.");ui_set(a->controls[UI_COMPONENTS],status);
    swprintf(status,2400,L"%s\n%s",a->view->microphone_installed?L"Endpoint available":L"Not installed — signed driver required",a->view->microphone_installed?a->view->microphone_name:L"Setup explains driver validation and signing.");ui_set(a->controls[UI_MIC_SETUP_TEXT],status);ui_set(a->controls[UI_CAPTURE_PATH],a->view->capture_client);
    swprintf(status,2400,L"ADB: %hs\nscrcpy probe client: %hs\nManaged client: %hs\n\nThe same selected ADB is passed to each scrcpy process. A version conflict requires an explicit repair; the shared server is never restarted automatically.",a->view->adb_version,a->view->scrcpy_version,a->view->capture_version);ui_set(a->controls[UI_VERSIONS],status);
    ui_enable(a->controls[UI_APPLY_PATHS],!a->view->active&&!a->view->busy);ui_enable(a->controls[UI_REGISTER_CAMERA],!a->view->active&&!a->view->busy);ui_enable(a->controls[UI_INSTALL_CAMERA],!a->view->active&&!a->view->busy);ui_enable(a->controls[UI_RECONNECT],!a->view->active&&a->selected.serial[0]);
}
bool settings_page_command(AppUI *a,int id,int code){
    bool output_choice=id==UI_PREVIEW_SCALE||id==UI_PIPELINE_SCALE||id==UI_OUTPUT_FORMAT||id==UI_THEME||id==UI_VIDEO_PACING;
    /* Arrow navigation in an open list is provisional. Mouse/Enter acceptance
       sends SELENDOK; closed-list keyboard changes send SELCHANGE. */
    if(output_choice&&(code==CBN_SELENDOK||(code==CBN_SELCHANGE&&!SendMessageW(a->controls[id],CB_GETDROPPEDSTATE,0,0)))){
        unsigned preview=(unsigned)SendMessageW(a->controls[UI_PREVIEW_SCALE],CB_GETCURSEL,0,0),pipeline=(unsigned)SendMessageW(a->controls[UI_PIPELINE_SCALE],CB_GETCURSEL,0,0),theme=(unsigned)SendMessageW(a->controls[UI_THEME],CB_GETCURSEL,0,0);
        int choice=(int)SendMessageW(a->controls[UI_OUTPUT_FORMAT],CB_GETCURSEL,0,0),pacing=(int)SendMessageW(a->controls[UI_VIDEO_PACING],CB_GETCURSEL,0,0);
        if(preview>2||pipeline>2||theme>2||choice<0||choice>3||pacing<0||pacing>1)return true;
        unsigned width=choice==1?1280:choice==2?3840:1920,fps=choice==3?60:30,buffer=pacing==0?1:0;
        /* Native controls may send both acceptance and change notifications. */
        if(a->settings_pending){if(preview==a->pending_preview&&pipeline==a->pending_pipeline&&theme==a->pending_theme&&width==a->pending_width&&fps==a->pending_fps&&buffer==a->pending_pacing)return true;}
        else{NativeSettings *s=&a->view->settings;if(preview==s->preview_scaler&&pipeline==s->pipeline_scaler&&theme==s->ui_theme&&width==s->output_width&&fps==s->output_fps&&buffer==s->video_buffer)return true;}
        NativeCommand *c=calloc(1,sizeof(*c));if(!c)return true;c->action=N_SCALERS;c->settings=a->view->settings;
        c->settings.video_buffer=buffer;c->settings.ui_theme=theme;c->settings.preview_scaler=preview;c->settings.pipeline_scaler=pipeline;c->settings.output_width=width;c->settings.output_height=width*9/16;c->settings.output_fps=fps;
        if(!native_manager_send(a->manager,c)){ui_set(a->controls[UI_NOTICE],L"The operation queue is full; retry after the current operation.");free(c);return true;}
        a->settings_pending=true;a->pending_preview=preview;a->pending_pipeline=pipeline;a->pending_width=width;a->pending_fps=fps;a->pending_theme=theme;a->pending_pacing=buffer;
        ui_theme_update(a,a->pending_theme,false);free(c);return true;
    }
    if(id==UI_BROWSE_ADB||id==UI_BROWSE_SCRCPY){browse(a,id==UI_BROWSE_ADB?UI_ADB:UI_SCRCPY);return true;}
    if(id==UI_APPLY_PATHS){NativeCommand *c=calloc(1,sizeof(*c));if(!c)return true;c->action=N_SETTINGS;c->settings=a->view->settings;GetWindowTextW(a->controls[UI_ADB],c->settings.adb,AWM_PATH);GetWindowTextW(a->controls[UI_SCRCPY],c->settings.scrcpy,AWM_PATH);native_manager_send(a->manager,c);free(c);return true;}
    if(id==UI_REGISTER_CAMERA){ui_send(a,N_REGISTER);return true;}
    if(id==UI_INSTALL_CAMERA){wchar_t exe[AWM_PATH];GetModuleFileNameW(NULL,exe,AWM_PATH);PathRemoveFileSpecW(exe);PathAppendW(exe,L"awm-camera-control.exe");ShellExecuteW(a->window,L"runas",exe,L"install-source",NULL,SW_HIDE);return true;}
    if(id==UI_INSTALL_MIC){MessageBoxW(a->window,L"Our own microphone requires a trusted signed driver package. The current development package is unsigned and cannot be installed on ordinary Windows.\n\nThe manager does not weaken boot/signature enforcement or substitute a cable/PC microphone. Kernel validation and publisher signing must be completed before installing this input.",L"Phone microphone setup",MB_OK|MB_ICONINFORMATION);return true;}
    if(id==UI_RECONNECT){a->profile.reconnect=SendMessageW(a->controls[id],BM_GETCHECK,0,0)==BST_CHECKED;ui_save_profile(a);return true;}
    if(id==UI_ADB_LINK||id==UI_SCRCPY_LINK||id==UI_DRIVER_LINK){const wchar_t *url=id==UI_ADB_LINK?L"https://developer.android.com/tools/adb":id==UI_SCRCPY_LINK?L"https://github.com/Genymobile/scrcpy/releases/latest":L"https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing";ShellExecuteW(a->window,L"open",url,NULL,NULL,SW_SHOWNORMAL);return true;}
    return false;
}
