#include "app_ui.h"
#include "app_icon.h"
#include "../composition.h"
static AwmAppIcons window_icons;
#include <math.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>
static AppUI *ui;
static void status_item(UIStatusItem *item,const wchar_t *label,const wchar_t *value,UIStatusTone tone){awm_wcopy(item->label,16,label);awm_wcopy(item->value,48,value);item->tone=tone;}
static void status_update(AppUI *a){
    NativeView *v=a->view;UIStatusItem next[4]={0};
    const wchar_t *value=L"Unselected";UIStatusTone tone=UI_TONE_NEUTRAL;
    if(a->selected.serial[0])switch(a->selected.state){
        case DEV_AUTHORIZED:value=L"Connected";tone=UI_TONE_READY;break;
        case DEV_UNAUTHORIZED:value=L"Unauthorized";tone=UI_TONE_WARNING;break;
        case DEV_OFFLINE:value=L"Offline";tone=UI_TONE_FAILURE;break;
        case DEV_DISCONNECTED:value=L"Disconnected";tone=UI_TONE_FAILURE;break;
        case DEV_UNSUPPORTED:value=L"Unsupported";tone=UI_TONE_WARNING;break;
        default:value=L"Unknown";tone=UI_TONE_WARNING;break;
    }
    status_item(&next[0],L"ADB:",value,tone);
    value=L"Stopped";tone=UI_TONE_NEUTRAL;
    switch(v->phase){
        case N_STARTING:value=L"Starting";tone=UI_TONE_PENDING;break;
        case N_STOPPING:value=L"Stopping";tone=UI_TONE_PENDING;break;
        case N_RECONNECTING:value=L"Retrying";tone=UI_TONE_WARNING;break;
        case N_ERROR:value=L"Failed";tone=UI_TONE_FAILURE;break;
        case N_RUNNING:value=v->active&&v->source_live?L"Live":L"Waiting";tone=v->active&&v->source_live?UI_TONE_READY:UI_TONE_WARNING;break;
        default:break;
    }
    status_item(&next[1],L"Capture:",value,tone);
    status_item(&next[2],L"Camera:",!v->outputs_checked?L"Checking":v->camera_registered?L"Available":L"Unavailable",!v->outputs_checked?UI_TONE_PENDING:v->camera_registered?UI_TONE_READY:UI_TONE_FAILURE);
    value=L"Checking";tone=UI_TONE_PENDING;
    if(v->outputs_checked){
        if(!v->microphone_installed){value=L"Unavailable";tone=UI_TONE_FAILURE;}
        else if(!a->profile.audio){value=L"Off";tone=UI_TONE_NEUTRAL;}
        else if(v->active){value=v->microphone_forwarding?L"Forwarding":L"Waiting";tone=v->microphone_forwarding?UI_TONE_READY:UI_TONE_WARNING;}
        else{value=L"Available";tone=UI_TONE_READY;}
    }
    status_item(&next[3],L"Mic:",value,tone);
    if(memcmp(a->status,next,sizeof(next))){
        memcpy(a->status,next,sizeof(next));wchar_t summary[384];swprintf(summary,384,L"%s %s   %s %s   %s %s   %s %s",next[0].label,next[0].value,next[1].label,next[1].value,next[2].label,next[2].value,next[3].label,next[3].value);
        ui_set(a->controls[UI_STATUS],summary);InvalidateRect(a->controls[UI_STATUS],NULL,FALSE);
    }
}
void ui_select(HWND h,int index){
    /* An open native list owns its temporary highlight. Periodic model updates
       must not replace it with the saved value while the user is browsing. */
    if(h&&!SendMessageW(h,CB_GETDROPPEDSTATE,0,0)&&(int)SendMessageW(h,CB_GETCURSEL,0,0)!=index)SendMessageW(h,CB_SETCURSEL,index,0);
}
void ui_check(HWND h,bool checked){if(h&&(SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED)!=checked)SendMessageW(h,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);}
void ui_enable(HWND h,bool enabled){if(h&&(IsWindowEnabled(h)!=FALSE)!=enabled)EnableWindow(h,enabled);}
void ui_show_mode(HWND h,int mode){
    if(!h)return;bool visible=(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)!=0;
    if((mode==SW_SHOW&&visible)||(mode==SW_HIDE&&!visible))return;ShowWindow(h,mode);
}
void ui_set(HWND h,const wchar_t *s){if(h){int n=GetWindowTextLengthW(h);wchar_t stack_buf[512];wchar_t *old=n<512?stack_buf:malloc(((size_t)n+1)*sizeof(wchar_t));if(old){GetWindowTextW(h,old,n+1);if(wcscmp(old,s))SetWindowTextW(h,s);if(old!=stack_buf)free(old);}else SetWindowTextW(h,s);}}
void ui_text(HWND h,const char *s){
    if(!h||!s)return;int needed=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0);if(needed<=0){ui_set(h,L"");return;}
    wchar_t stack_buf[1024];wchar_t *w=needed<=1024?stack_buf:malloc((size_t)needed*sizeof(wchar_t));
    if(w){if(MultiByteToWideChar(CP_UTF8,0,s,-1,w,needed)>0)ui_set(h,w);else ui_set(h,L"");if(w!=stack_buf)free(w);}
}
HWND ui_control(AppUI *a,HWND parent,const wchar_t *class,const wchar_t *name,DWORD style,int id){
    HWND h=CreateWindowExW(!wcscmp(class,L"EDIT")?WS_EX_CLIENTEDGE:0,class,name,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,(HMENU)(INT_PTR)id,a->instance,NULL);
    SendMessageW(h,WM_SETFONT,(WPARAM)(ui_heading_id(id)?a->heading:a->font),TRUE);ui_theme_attach(a,h,class,style);if(id>0&&id<256)a->controls[id]=h;return h;
}
HWND ui_button(AppUI *a,HWND p,const wchar_t *s,int id){return ui_control(a,p,L"BUTTON",s,BS_OWNERDRAW|WS_TABSTOP,id);}
HWND ui_combo(AppUI *a,HWND p,int id){return ui_control(a,p,L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL|WS_TABSTOP,id);}
HWND ui_edit(AppUI *a,HWND p,int id,DWORD style){return ui_control(a,p,L"EDIT",L"",(style&ES_MULTILINE?0:ES_AUTOHSCROLL)|WS_TABSTOP|style,id);}
void ui_field(AppUI *a,HWND p,int id,const wchar_t *name){ui_control(a,p,L"STATIC",name,SS_LEFT,id+128);}
void ui_item(HWND h,const wchar_t *s){SendMessageW(h,CB_ADDSTRING,0,(LPARAM)s);}
void ui_move(AppUI *a,HWND h,int x,int y,int w,int height){
    if(!h)return;RECT r;GetWindowRect(h,&r);MapWindowPoints(NULL,GetParent(h),(POINT *)&r,2);
    /* Scale rectangle edges together. Independent width rounding clips the
       right border of edge-aligned controls at fractional display scaling. */
    int left=MulDiv(x,a->dpi,96),top=MulDiv(y,a->dpi,96);
    int width=MulDiv(x+w,a->dpi,96)-left,tall=MulDiv(y+height,a->dpi,96)-top;
    wchar_t name[32];GetClassNameW(h,name,32);
    bool combo=!_wcsicmp(name,L"COMBOBOX");
    if(r.left==left&&r.top==top&&r.right-r.left==width&&(combo||r.bottom-r.top==tall))return;
    UINT flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_DEFERERASE;
    if(a->layout_batch)a->layout_batch=DeferWindowPos(a->layout_batch,h,NULL,left,top,width,tall,flags|SWP_NOREDRAW);
    else SetWindowPos(h,NULL,left,top,width,tall,flags);
}
void ui_field_layout(AppUI *a,int id,int x,int y,int w){
    ui_move(a,a->controls[id+128],x,y,w,20);wchar_t class[32];GetClassNameW(a->controls[id],class,32);
    ui_move(a,a->controls[id],x,y+21,w,!_wcsicmp(class,L"COMBOBOX")?240:28);
}
void ui_copy(AppUI *a,const wchar_t *s){if(!s[0])return;size_t bytes=(wcslen(s)+1)*sizeof(wchar_t);HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!block)return;void *p=GlobalLock(block);if(!p){GlobalFree(block);return;}memcpy(p,s,bytes);GlobalUnlock(block);if(OpenClipboard(a->window)){EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,block))GlobalFree(block);CloseClipboard();}else GlobalFree(block);}
void ui_send(AppUI *a,NativeAction action){NativeCommand *c=calloc(1,sizeof(*c));if(!c)return;c->action=action;c->device=a->selected;c->capture=a->profile;c->use_capture=true;if(!native_manager_send(a->manager,c))ui_set(a->controls[UI_NOTICE],L"The operation queue is full; retry after the current operation.");free(c);}
void ui_save_profile(AppUI *a){char error[1024];if(a->loading||!a->selected.serial[0])return;a->profile_dirty=true;if(native_capture_validate(&a->profile,&a->selected,&a->view->features,error,sizeof(error)))ui_send(a,N_PROFILE);else{ui_set(a->controls[UI_SAVED],L"Not saved — invalid settings");ui_text(a->controls[UI_NOTICE],error);}}
void ui_select_device(AppUI *a,const Device *d){
    if(a->view->active||a->view->phase==N_RECONNECTING){ui_set(a->controls[UI_NOTICE],L"Stop capture before changing device or source.");return;}
    a->user_deselected=false;a->selected=*d;int index=find_device(&a->view->devices,d->serial,d->transport_id);ui_select(a->controls[UI_DEVICE],(int)(index<0?0:index+1));CaptureKind kind=native_last_kind(&a->view->settings,d->serial);native_capture_get(&a->view->settings,d->serial,kind,&a->profile);a->caps_key[0]=0;
    ui_profile_controls(a);
    bool cached=!strcmp(a->view->capabilities_serial,d->serial)&&!strcmp(a->view->capabilities_transport,d->transport_id)&&!strcmp(a->view->capabilities_version,a->view->scrcpy_version)&&(kind==CAPTURE_CAMERA?a->view->capabilities_camera_ok:a->view->capabilities_screen_ok);
    a->probe_revision=a->view->probe_revision;a->probing=d->state==DEV_AUTHORIZED&&!cached;if(a->probing)ui_send(a,N_PROBE);ui_layout(a);
}
static void canvas_source_size(AppUI *a,uint32_t *sw,uint32_t *sh){
    if(a->view&&awm_frame_shape(a->view->width,a->view->height)){*sw=a->view->width;*sh=a->view->height;}
    else{*sw=1920;*sh=1080;}
}
void ui_set_zoom(AppUI *a,float zoom,float pan_x,float pan_y){
    if(!a)return;
    if(!(zoom>=1.0f))zoom=1.0f;if(zoom>8.0f)zoom=8.0f;
    if(zoom<1.015f)zoom=1.0f;
    if(!(pan_x>=0.0f&&pan_x<=1.0f))pan_x=0.5f;if(!(pan_y>=0.0f&&pan_y<=1.0f))pan_y=0.5f;
    unsigned framing=a->view&&a->view->active?a->active_framing:(unsigned)a->profile.framing;
    if(zoom<=1.0001f&&framing==FRAME_FIT){pan_x=0.5f;pan_y=0.5f;}
    else{
        uint32_t sw,sh;canvas_source_size(a,&sw,&sh);
        RECT r={0};if(a->canvas)GetClientRect(a->canvas,&r);
        uint32_t cw=r.right>0?(uint32_t)r.right:1920,ch=r.bottom>0?(uint32_t)r.bottom:1080;
        AwmComposition comp;
        if(awm_composition_zoom(sw,sh,cw,ch,framing,zoom,pan_x,pan_y,&comp)){
            pan_x=comp.u+comp.uw*0.5f;pan_y=comp.v+comp.vh*0.5f;
        }
    }
    a->active_zoom=zoom;a->active_pan_x=pan_x;a->active_pan_y=pan_y;
    preview_worker_set_zoom(a->renderer,zoom,pan_x,pan_y);
    native_manager_set_zoom(a->manager,zoom,pan_x,pan_y);
    capture_page_sync_zoom(a);
}
void ui_preview_state(AppUI *a){
    bool enabled=!a->closing&&a->page==0&&!a->hide&&!IsIconic(a->window)&&a->view->active;
    preview_worker_update(a->renderer,a->view->mapping_name,enabled,a->settings_pending?a->pending_preview:a->view->settings.preview_scaler,a->active_framing);
    preview_worker_set_zoom(a->renderer,a->active_zoom,a->active_pan_x,a->active_pan_y);
    bool live=enabled&&preview_worker_live(a->renderer);
    /* A flip-model D3D surface can outlive its swap chain's last Present.
       Hide its HWND while the GDI placeholder owns the preview rectangle. */
    bool canvas_visible=(GetWindowLongPtrW(a->canvas,GWL_STYLE)&WS_VISIBLE)!=0;
    ui_show_mode(a->canvas,live?SW_SHOW:SW_HIDE);ui_show_mode(a->placeholder,live?SW_HIDE:SW_SHOW);
    if(live&&!canvas_visible)preview_worker_redraw(a->renderer);
    const wchar_t *s=preview_worker_failed(a->renderer)?L"Preview rendering failed\nInspect Diagnostics":a->hide?L"Preview hidden\nWindows output continues independently":a->view->active?L"Waiting for a valid source frame…":L"Output preview\nChoose a device and source, then Start capture";
    ui_set(a->placeholder,s);
}
static void layout_audit(AppUI *a){
    /* Opt-in developer evidence from our own controls after Windows has applied
       layout. Native combo heights and fractional DPI are measured, not guessed.
       Scroll content may extend vertically; ordinary siblings may not overlap. */
    wchar_t path[AWM_PATH];if(!GetEnvironmentVariableW(L"AWM_UI_LAYOUT_TRACE",path,AWM_PATH)||IsIconic(a->window))return;
    FILE *out=_wfopen(path,L"a");if(!out)return;unsigned clipped=0,overlaps=0;
    for(int i=0;i<256;i++){
        HWND child=a->controls[i];if(!child||!IsWindowVisible(child))continue;HWND parent=GetParent(child);RECT r,bounds;GetWindowRect(child,&r);MapWindowPoints(NULL,parent,(POINT *)&r,2);GetClientRect(parent,&bounds);
        if(r.left<0||r.right>bounds.right||(parent!=a->property&&(r.top<0||r.bottom>bounds.bottom))){fprintf(out,"CLIPPED id=%d rect=%ld,%ld,%ld,%ld parent=%ld,%ld\n",i,r.left,r.top,r.right,r.bottom,bounds.right,bounds.bottom);clipped++;}
        for(int j=i+1;j<256;j++){
            HWND other=a->controls[j];if(!other||!IsWindowVisible(other)||GetParent(other)!=parent)continue;RECT second,intersection;GetWindowRect(other,&second);MapWindowPoints(NULL,parent,(POINT *)&second,2);
            if(IntersectRect(&intersection,&r,&second)){fprintf(out,"OVERLAP ids=%d,%d pixels=%ld,%ld\n",i,j,intersection.right-intersection.left,intersection.bottom-intersection.top);overlaps++;}
        }
    }
    fprintf(out,"LAYOUT page=%d settings=%d dpi=%u clipped=%u overlaps=%u\n",a->page,a->settings_page,a->dpi,clipped,overlaps);fclose(out);
}
void ui_layout(AppUI *a){
    if(!a->controls[UI_TABS])return;if(IsIconic(a->window)){ui_preview_state(a);return;}RECT r;GetClientRect(a->window,&r);int w=MulDiv(r.right,96,a->dpi),h=MulDiv(r.bottom,96,a->dpi);
    /* The tab face and the parent navigation rule share the exact 42-DIP edge. */
    TabCtrl_SetItemSize(a->controls[UI_TABS],MulDiv(112,a->dpi,96),MulDiv(32,a->dpi,96));ui_move(a,a->controls[UI_TABS],12,6,452,36);for(int i=0;i<4;i++){ui_move(a,a->pages[i],12,56,w-24,h-110);ui_show_mode(a->pages[i],a->page==i?SW_SHOW:SW_HIDE);}
    capture_page_layout(a,w-24,h-110);devices_page_layout(a,w-24,h-110);settings_page_layout(a,w-24,h-110);diagnostics_page_layout(a,w-24,h-110);
    ui_move(a,a->controls[UI_STATUS],480,4,w-492,18);ui_move(a,a->controls[UI_NOTICE],480,23,w-492,18);ui_move(a,a->controls[UI_START],w-142,h-43,130,33);ui_preview_state(a);layout_audit(a);
    /* Resize changes divider/box geometry even in areas without child controls.
       Discard old background pixels only on layout, never on status polling. */
    RedrawWindow(a->window,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
}
static void refresh(AppUI *a){
    HWND focused=GetFocus();if(focused&&IsChild(a->window,focused))a->last_focus=focused;
    a->view_revision=native_manager_revision(a->manager);
    native_manager_snapshot(a->manager,a->view);
    /* Dropdown notifications own acceptance. Do not poll closed comboboxes
       for changes; restore the model value on cancel or refresh instead. */
    unsigned theme=a->settings_pending?a->pending_theme:a->view->settings.ui_theme;if(theme!=a->theme_mode)ui_theme_update(a,theme,false);
    bool locked=a->view->active||a->view->phase==N_RECONNECTING;
    char key[24576]={0};for(int i=0;i<a->view->devices.count;i++){const Device *d=&a->view->devices.items[i];size_t n=strlen(key);snprintf(key+n,sizeof(key)-n,"%.255s/%.31s/%d/%d;",d->serial,d->transport_id,d->state,d->sdk);}
    bool changed=strcmp(key,a->device_key)!=0;bool rebound_authorized=false;
    if(changed){awm_copy(a->device_key,sizeof(a->device_key),key);a->loading=true;SendMessageW(a->controls[UI_DEVICE],CB_RESETCONTENT,0,0);ui_item(a->controls[UI_DEVICE],L"Select an authorized device");int selected=0;
        for(int i=0;i<a->view->devices.count;i++){Device *d=&a->view->devices.items[i];char s[900];snprintf(s,sizeof(s),"%s · %s · %s · transport %s",d->model[0]?d->model:"Android device",transport_name(d->transport),device_state_name(d->state),d->transport_id[0]?d->transport_id:"?");wchar_t text[900];utf8_to_wide(s,text,900);ui_item(a->controls[UI_DEVICE],text);if(!strcmp(d->serial,a->selected.serial)&&!strcmp(d->transport_id,a->selected.transport_id)){selected=i+1;a->selected=*d;}}
        if(!selected&&a->selected.serial[0]&&a->selected.transport!=TRANSPORT_UNKNOWN){
            /* If the exact same serial/transport returned with a new ADB transport ID
               (e.g. after USB replug), follow it without jumping across transports. */
            for(int i=0;i<a->view->devices.count;i++){Device *d=&a->view->devices.items[i];if(d->state==DEV_AUTHORIZED&&!strcmp(d->serial,a->selected.serial)&&d->transport==a->selected.transport){selected=i+1;a->selected=*d;rebound_authorized=!locked;break;}}
        }
        ui_select(a->controls[UI_DEVICE],(int)(selected));a->loading=false;}
    if(rebound_authorized&&!a->view->busy){a->caps_key[0]=0;a->probe_revision=a->view->probe_revision;a->probing=true;ui_send(a,N_PROBE);}
    /* Smart pre-selection without auto-starting capture: if no device is
       selected and the user didn't clear selection, pick the sole authorized
       device (or the unique authorized device with a saved profile). */
    if(!a->selected.serial[0]&&!a->user_deselected&&!locked&&!a->view->busy&&a->view->outputs_checked){
        int auth_idx=-1,auth_count=0,saved_idx=-1,saved_count=0;
        for(int i=0;i<a->view->devices.count;i++){
            const Device *d=&a->view->devices.items[i];if(d->state!=DEV_AUTHORIZED)continue;
            auth_idx=i;auth_count++;
            for(int k=0;k<a->view->settings.capture_count;k++)if(!strcmp(a->view->settings.captures[k].serial,d->serial)){saved_idx=i;saved_count++;break;}
        }
        int pick=auth_count==1?auth_idx:(saved_count==1?saved_idx:-1);
        if(pick>=0)ui_select_device(a,&a->view->devices.items[pick]);
    }
    if(a->probing&&!a->view->busy&&a->view->probe_revision!=a->probe_revision)a->probing=false;
    const wchar_t *action=a->view->phase==N_STOPPING?L"Stopping…":a->view->phase==N_RECONNECTING?L"Cancel reconnect":a->view->phase==N_STARTING?L"Cancel start":a->view->active?L"Stop capture":(a->view->busy||a->probing)?L"Cancel operation":L"Start capture";
    ui_set(a->controls[UI_START],a->closing?L"Closing…":action);ui_enable(a->controls[UI_START],!a->closing&&a->view->phase!=N_STOPPING&&(locked||a->view->busy||a->probing||a->selected.state==DEV_AUTHORIZED));
    ui_text(a->controls[UI_NOTICE],a->view->notice);status_update(a);
    capture_page_update(a);devices_page_update(a,changed);settings_page_update(a);if(a->page==3)diagnostics_page_update(a);
    if(a->receiver_started){char out[4096];while(process_read(&a->receiver,out,sizeof(out))){}if(!process_alive(&a->receiver)){swprintf(a->receiver_state,256,L"Local test %s; visually confirm the phone image",a->receiver.exit_code?L"failed — inspect the receiver/log":L"received camera samples");a->receiver_started=false;process_close(&a->receiver);}}
    ui_set(a->controls[UI_RECEIVER],a->receiver_state);ui_preview_state(a);
}
static void fonts(AppUI *a){
    if(a->font)DeleteObject(a->font);if(a->mono)DeleteObject(a->mono);if(a->heading)DeleteObject(a->heading);a->heading=CreateFontW(-MulDiv(10,a->dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");a->font=CreateFontW(-MulDiv(10,a->dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");a->mono=CreateFontW(-MulDiv(9,a->dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Consolas");
    for(int i=0;i<256;i++)if(a->controls[i])SendMessageW(a->controls[i],WM_SETFONT,(WPARAM)(i==UI_LOG?a->mono:ui_heading_id(i)?a->heading:a->font),TRUE);if(a->placeholder)SendMessageW(a->placeholder,WM_SETFONT,(WPARAM)a->font,TRUE);
}
static LRESULT CALLBACK pane(HWND h,UINT m,WPARAM w,LPARAM l){
    LRESULT themed;if(ui_theme_message(ui,h,m,w,l,&themed))return themed;
    if(m==WM_COMMAND||m==WM_NOTIFY)return SendMessageW(ui->window,m,w,l);
    if((m==WM_VSCROLL&&h==ui->property)||(m==WM_MOUSEWHEEL&&(h==ui->property||h==ui->property_viewport||h==ui->property_content))){
        SCROLLINFO info={0};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(ui->controls[UI_PROPERTY_SCROLL],SB_CTL,&info);int n=info.nPos;
        if(m==WM_MOUSEWHEEL){ui->wheel_remainder+=GET_WHEEL_DELTA_WPARAM(w)*36;n-=ui->wheel_remainder/WHEEL_DELTA;ui->wheel_remainder%=WHEEL_DELTA;}
        else{int code=LOWORD(w);if(code==SB_LINEUP)n-=20;else if(code==SB_LINEDOWN)n+=20;else if(code==SB_PAGEUP)n-=(int)info.nPage;else if(code==SB_PAGEDOWN)n+=(int)info.nPage;else if(code==SB_THUMBTRACK||code==SB_THUMBPOSITION)n=HIWORD(w);else if(code==SB_TOP)n=0;else if(code==SB_BOTTOM)n=info.nMax;}
        int maximum=info.nMax-(int)info.nPage+1;if(maximum<0)maximum=0;if(n<0)n=0;if(n>maximum)n=maximum;
        if(n!=ui->property_scroll){ui->property_scroll=n;capture_property_scroll(ui);}
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK placeholder(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);RECT r;GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));wchar_t s[256];GetWindowTextW(h,s,256);SelectObject(dc,ui->font);SetTextColor(dc,RGB(220,220,220));SetBkMode(dc,TRANSPARENT);r.top=r.bottom/2-24;DrawTextW(dc,s,-1,&r,DT_CENTER|DT_WORDBREAK);EndPaint(h,&p);return 0;}return DefWindowProcW(h,m,w,l);
}
static void canvas_zoom_anchored(AppUI *a,HWND h,float next_zoom,float su,float sv,int mx,int my){
    RECT r;GetClientRect(h,&r);if(r.right<=0||r.bottom<=0)return;
    uint32_t sw,sh;canvas_source_size(a,&sw,&sh);
    unsigned framing=a->view&&a->view->active?a->active_framing:(unsigned)a->profile.framing;
    AwmComposition probe;
    if(!awm_composition_zoom(sw,sh,(uint32_t)r.right,(uint32_t)r.bottom,framing,next_zoom,0.5f,0.5f,&probe)){ui_set_zoom(a,next_zoom,0.5f,0.5f);return;}
    float nfx=probe.width?((float)mx-(float)probe.x)/(float)probe.width:0.5f;
    float nfy=probe.height?((float)my-(float)probe.y)/(float)probe.height:0.5f;
    if(nfx<0.0f)nfx=0.0f;if(nfx>1.0f)nfx=1.0f;if(nfy<0.0f)nfy=0.0f;if(nfy>1.0f)nfy=1.0f;
    float nu=su-nfx*probe.uw,nv=sv-nfy*probe.vh;
    if(nu<0.0f)nu=0.0f;if(nu>1.0f-probe.uw)nu=1.0f-probe.uw;
    if(nv<0.0f)nv=0.0f;if(nv>1.0f-probe.vh)nv=1.0f-probe.vh;
    ui_set_zoom(a,next_zoom,nu+probe.uw*0.5f,nv+probe.vh*0.5f);
}
static LRESULT CALLBACK canvas(HWND h,UINT m,WPARAM w,LPARAM l){
    AppUI *a=ui;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}
    if(m==WM_MOUSEWHEEL&&a&&!a->closing){
        POINT pt={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&pt);
        RECT r;GetClientRect(h,&r);if(r.right<=0||r.bottom<=0)return 0;
        uint32_t sw,sh;canvas_source_size(a,&sw,&sh);
        unsigned framing=a->view&&a->view->active?a->active_framing:(unsigned)a->profile.framing;
        AwmComposition cur;if(!awm_composition_zoom(sw,sh,(uint32_t)r.right,(uint32_t)r.bottom,framing,a->active_zoom,a->active_pan_x,a->active_pan_y,&cur))return 0;
        float fx=cur.width?((float)pt.x-(float)cur.x)/(float)cur.width:0.5f,fy=cur.height?((float)pt.y-(float)cur.y)/(float)cur.height:0.5f;
        if(fx<0.0f)fx=0.0f;if(fx>1.0f)fx=1.0f;if(fy<0.0f)fy=0.0f;if(fy>1.0f)fy=1.0f;
        float su=cur.u+fx*cur.uw,sv=cur.v+fy*cur.vh;
        float notches=(float)GET_WHEEL_DELTA_WPARAM(w)/(float)WHEEL_DELTA;
        float next_zoom=a->active_zoom*powf(1.15f,notches);
        if(notches<0.0f&&next_zoom<1.03f)next_zoom=1.0f;
        if(next_zoom<1.0f)next_zoom=1.0f;if(next_zoom>8.0f)next_zoom=8.0f;
        canvas_zoom_anchored(a,h,next_zoom,su,sv,pt.x,pt.y);
        return 0;
    }
    if(m==WM_LBUTTONDBLCLK&&a&&!a->closing){ui_set_zoom(a,1.0f,0.5f,0.5f);return 0;}
    if(m==WM_LBUTTONDOWN&&a&&!a->closing){
        RECT r;GetClientRect(h,&r);if(r.right<=0||r.bottom<=0)return 0;
        int mx=GET_X_LPARAM(l),my=GET_Y_LPARAM(l);
        uint32_t sw,sh;canvas_source_size(a,&sw,&sh);
        unsigned framing=a->view&&a->view->active?a->active_framing:(unsigned)a->profile.framing;
        AwmComposition cur;if(!awm_composition_zoom(sw,sh,(uint32_t)r.right,(uint32_t)r.bottom,framing,a->active_zoom,a->active_pan_x,a->active_pan_y,&cur))return 0;
        a->dragging_preview=true;a->drag_start.x=mx;a->drag_start.y=my;
        a->drag_start_zoom=a->active_zoom;a->drag_start_pan_x=a->active_pan_x;a->drag_start_pan_y=a->active_pan_y;
        /* When already zoomed or cropped, drag pans 1:1. When full unzoomed frame is
           visible, dragging up/down activates and smoothly adjusts zoom at the click point. */
        a->drag_zoom_mode=(cur.uw>=0.999f&&cur.vh>=0.999f);
        float fx=cur.width?((float)mx-(float)cur.x)/(float)cur.width:0.5f,fy=cur.height?((float)my-(float)cur.y)/(float)cur.height:0.5f;
        if(fx<0.0f)fx=0.0f;if(fx>1.0f)fx=1.0f;if(fy<0.0f)fy=0.0f;if(fy>1.0f)fy=1.0f;
        a->drag_anchor_u=cur.u+fx*cur.uw;a->drag_anchor_v=cur.v+fy*cur.vh;
        SetCapture(h);return 0;
    }
    if(m==WM_MOUSEMOVE&&a&&a->dragging_preview&&GetCapture()==h){
        RECT r;GetClientRect(h,&r);if(r.right<=0||r.bottom<=0)return 0;
        int mx=GET_X_LPARAM(l),my=GET_Y_LPARAM(l);
        int dx=mx-a->drag_start.x,dy=my-a->drag_start.y;
        if(a->drag_zoom_mode){
            if(abs(dy)<3&&a->active_zoom<=1.001f)return 0;
            float next_zoom=a->drag_start_zoom*expf((float)(-dy)/140.0f);
            if(next_zoom<1.02f)next_zoom=1.0f;if(next_zoom>8.0f)next_zoom=8.0f;
            canvas_zoom_anchored(a,h,next_zoom,a->drag_anchor_u,a->drag_anchor_v,a->drag_start.x,a->drag_start.y);
        }else{
            uint32_t sw,sh;canvas_source_size(a,&sw,&sh);
            unsigned framing=a->view&&a->view->active?a->active_framing:(unsigned)a->profile.framing;
            AwmComposition start_comp;
            if(awm_composition_zoom(sw,sh,(uint32_t)r.right,(uint32_t)r.bottom,framing,a->drag_start_zoom,a->drag_start_pan_x,a->drag_start_pan_y,&start_comp)){
                float du=start_comp.width?-((float)dx/(float)start_comp.width)*start_comp.uw:0.0f;
                float dv=start_comp.height?-((float)dy/(float)start_comp.height)*start_comp.vh:0.0f;
                ui_set_zoom(a,a->drag_start_zoom,a->drag_start_pan_x+du,a->drag_start_pan_y+dv);
            }
        }
        return 0;
    }
    if((m==WM_LBUTTONUP||m==WM_CAPTURECHANGED||m==WM_CANCELMODE)&&a&&a->dragging_preview){
        a->dragging_preview=false;a->drag_zoom_mode=false;
        if(m!=WM_CAPTURECHANGED&&GetCapture()==h)ReleaseCapture();
        return 0;
    }
    if(m==WM_SETCURSOR&&a){
        if(a->dragging_preview)SetCursor(LoadCursorW(NULL,a->drag_zoom_mode?IDC_SIZENS:IDC_SIZEALL));
        else if(a->active_zoom>1.001f||(a->view&&a->view->active?a->active_framing:(unsigned)a->profile.framing)==FRAME_FILL)SetCursor(LoadCursorW(NULL,IDC_SIZEALL));
        else SetCursor(LoadCursorW(NULL,IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK main_proc(HWND h,UINT msg,WPARAM w,LPARAM l){
    AppUI *a=ui;
    if(msg==WM_CREATE){a->window=h;a->dpi=GetDpiForWindow(h);awm_app_apply_icons(h,a->instance,a->dpi,&window_icons);fonts(a);ui_theme_update(a,0,true);ui_control(a,h,WC_TABCONTROLW,L"Pages",WS_TABSTOP|TCS_FIXEDWIDTH,UI_TABS);const wchar_t *names[]={L"Capture",L"Devices",L"Settings",L"Diagnostics"};for(int i=0;i<4;i++){TCITEMW t={0};t.mask=TCIF_TEXT;t.pszText=(LPWSTR)names[i];TabCtrl_InsertItem(a->controls[UI_TABS],i,&t);a->pages[i]=CreateWindowExW(WS_EX_CONTROLPARENT,L"AWMCompactPane",names[i],WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,h,NULL,a->instance,NULL);}
        ui_button(a,h,L"Start capture",UI_START);ui_control(a,h,L"STATIC",L"Inspecting dependencies…",SS_LEFT|SS_ENDELLIPSIS,UI_NOTICE);ui_control(a,h,L"STATIC",L"Stopped",SS_LEFT|SS_ENDELLIPSIS,UI_STATUS);
        capture_page_create(a);devices_page_create(a);settings_page_create(a);diagnostics_page_create(a);a->manager=native_manager_create(h);a->renderer=preview_worker_create(a->canvas,h);swprintf(a->receiver_state,256,L"Receiving app: Not checked");ui_theme_update(a,0,true);status_update(a);ui_layout(a);SetTimer(h,1,100,NULL);return 0;
    }
    LRESULT themed;if(ui_theme_message(a,h,msg,w,l,&themed))return themed;
    if(msg==WM_SETTINGCHANGE||msg==WM_THEMECHANGED||msg==WM_SYSCOLORCHANGE){ui_theme_update(a,a->theme_mode,true);return 0;}
    if(msg==WM_ACTIVATE&&LOWORD(w)==WA_INACTIVE){HWND f=GetFocus();if(f&&IsChild(h,f))a->last_focus=f;}
    if(msg==WM_SETFOCUS){if(a->last_focus&&IsWindowVisible(a->last_focus)&&IsWindowEnabled(a->last_focus))SetFocus(a->last_focus);else SetFocus(a->controls[UI_TABS]);return 0;}
    if(msg==WM_SIZE){ui_layout(a);return 0;}
    if(msg==WM_DPICHANGED){a->dpi=HIWORD(w);awm_app_apply_icons(h,a->instance,a->dpi,&window_icons);RECT *r=(RECT *)l;SetWindowPos(h,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);fonts(a);ui_layout(a);return 0;}
    if(msg==WM_GETMINMAXINFO){MINMAXINFO *v=(void *)l;UINT dpi=a->dpi?a->dpi:96;RECT r={0,0,MulDiv(960,dpi,96),MulDiv(640,dpi,96)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);v->ptMinTrackSize.x=r.right-r.left;v->ptMinTrackSize.y=r.bottom-r.top;return 0;}
    if(msg==NATIVE_UPDATE||msg==PREVIEW_UPDATE){if(a->manager&&!a->closing)refresh(a);return 0;}
    if(msg==WM_TIMER){if(a->closing){if(native_manager_closed(a->manager)&&preview_worker_closed(a->renderer)){native_manager_destroy(a->manager);a->manager=NULL;preview_worker_destroy(a->renderer);a->renderer=NULL;DestroyWindow(h);}}else if(a->manager){if(native_manager_revision(a->manager)!=a->view_revision||a->receiver_started)refresh(a);if(a->page==1&&!a->view->active&&!a->view->busy&&a->view->features.adb_mdns&&GetTickCount64()-a->last_mdns_poll>=5000){a->last_mdns_poll=GetTickCount64();ui_send(a,N_MDNS_QUIET);}}return 0;}
    if(msg==WM_NOTIFY&&!a->loading){NMHDR *n=(void *)l;if(n->idFrom==UI_TABS&&n->code==TCN_SELCHANGE){a->page=TabCtrl_GetCurSel(a->controls[UI_TABS]);if(a->page==1&&!a->view->active&&!a->view->busy&&a->view->features.adb_mdns){a->last_mdns_poll=GetTickCount64();ui_send(a,N_MDNS_QUIET);}ui_layout(a);refresh(a);return 0;}if(n->idFrom==UI_SETTINGS_TABS&&n->code==TCN_SELCHANGE){a->settings_page=TabCtrl_GetCurSel(a->controls[UI_SETTINGS_TABS]);ui_layout(a);return 0;}if(n->idFrom==UI_DEVICE_LIST&&n->code==LVN_ITEMCHANGED&&(((NMLISTVIEW *)n)->uChanged&LVIF_STATE)&&(((NMLISTVIEW *)n)->uNewState&LVIS_SELECTED)){int i=ListView_GetNextItem(a->controls[UI_DEVICE_LIST],-1,LVNI_SELECTED);if(i>=0&&i<a->view->devices.count){a->inspected=a->view->devices.items[i];devices_page_update(a,false);}return 0;}}
    if(msg==WM_COMMAND&&!a->loading&&!a->closing){int id=LOWORD(w),code=HIWORD(w);
        if(id==UI_START){if(a->view->active||a->view->busy||a->probing||a->view->phase==N_RECONNECTING){a->probing=false;ui_send(a,N_STOP);}else{a->active_framing=a->profile.framing;ui_send(a,N_START);}return 0;}
        if(capture_page_command(a,id,code)||devices_page_command(a,id,code)||settings_page_command(a,id,code)||diagnostics_page_command(a,id))return 0;
    }
    if(msg==WM_CLOSE){if(!a->closing){a->closing=true;native_manager_begin_close(a->manager);preview_worker_close(a->renderer);if(a->receiver_started){process_close(&a->receiver);a->receiver_started=false;}ui_set(a->controls[UI_START],L"Closing…");ui_enable(a->controls[UI_START],FALSE);}return 0;}
    if(msg==WM_DESTROY){KillTimer(h,1);PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
int app_ui_run(HINSTANCE instance,int show){
    platform_init();SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&cc);
    ui=calloc(1,sizeof(*ui));if(!ui)return 1;ui->view=calloc(1,sizeof(*ui->view));if(!ui->view){free(ui);return 1;}ui->instance=instance;ui->dpi=96;ui->active_zoom=1.0f;ui->active_pan_x=0.5f;ui->active_pan_y=0.5f;native_capture_default(&ui->profile,CAPTURE_CAMERA,0);
    WNDCLASSW c={0};c.hInstance=instance;c.hCursor=LoadCursorW(NULL,IDC_ARROW);c.hbrBackground=NULL;c.style=CS_HREDRAW|CS_VREDRAW;c.lpfnWndProc=pane;c.lpszClassName=L"AWMCompactPane";RegisterClassW(&c);c.lpfnWndProc=placeholder;c.lpszClassName=L"AWMCompactPlaceholder";RegisterClassW(&c);c.style=CS_DBLCLKS;c.lpfnWndProc=canvas;c.lpszClassName=L"AWMCompactCanvas";RegisterClassW(&c);c.style=CS_HREDRAW|CS_VREDRAW;c.lpfnWndProc=main_proc;c.lpszClassName=L"AWMCompactManager";c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON));RegisterClassW(&c);
    UINT dpi=GetDpiForSystem();RECT r={0,0,MulDiv(1040,dpi,96),MulDiv(700,dpi,96)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);
    HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,c.lpszClassName,L"WinDroid Capture",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,NULL,NULL,instance,NULL);if(!h){free(ui->view);free(ui);platform_shutdown();return 1;}ui_show_mode(h,show);UpdateWindow(h);
    MSG m;while(GetMessageW(&m,NULL,0,0)>0){
        /* Route hover scroll directly to the live preview canvas without requiring click-focus. */
        if(m.message==WM_MOUSEWHEEL&&ui->canvas&&IsWindowVisible(ui->canvas)&&!ui->closing){
            POINT pt={GET_X_LPARAM(m.lParam),GET_Y_LPARAM(m.lParam)};
            if(WindowFromPoint(pt)==ui->canvas){SendMessageW(ui->canvas,WM_MOUSEWHEEL,m.wParam,m.lParam);continue;}
        }
        if(!IsDialogMessageW(h,&m)){TranslateMessage(&m);DispatchMessageW(&m);}
    }
    awm_app_release_icons(&window_icons);if(ui->font)DeleteObject(ui->font);if(ui->mono)DeleteObject(ui->mono);if(ui->heading)DeleteObject(ui->heading);if(ui->background)DeleteObject(ui->background);if(ui->white)DeleteObject(ui->white);if(ui->nav_brush)DeleteObject(ui->nav_brush);free(ui->view);free(ui);platform_shutdown();return 0;
}
