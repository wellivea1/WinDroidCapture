#include "app_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void column(HWND list,int i,const wchar_t *name,int width){LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;c.pszText=(LPWSTR)name;c.cx=width;c.iSubItem=i;ListView_InsertColumn(list,i,&c);}
void devices_page_create(AppUI *a){
    HWND p=a->pages[1],list=ui_control(a,p,WC_LISTVIEWW,L"Android devices",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,UI_DEVICE_LIST);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);column(list,0,L"Device",280);column(list,1,L"Connection",110);column(list,2,L"ADB state",130);column(list,3,L"Android",90);column(list,4,L"Active",100);
    ui_button(a,p,L"Refresh",UI_REFRESH);ui_button(a,p,L"Use selected device",UI_INSPECT);ui_button(a,p,L"USB setup",UI_USB);ui_control(a,p,L"STATIC",L"Select a device row to inspect its exact identity.",0,UI_DEVICE_DETAIL);
    ui_control(a,p,L"STATIC",L"Selected device",0,UI_DEVICE_TITLE);ui_control(a,p,L"STATIC",L"Wireless debugging",0,UI_WIRELESS_TITLE);
    ui_field(a,p,UI_PAIR_ADDRESS,L"Pairing address:port");ui_edit(a,p,UI_PAIR_ADDRESS,0);ui_field(a,p,UI_PAIR_CODE,L"Pairing code");ui_edit(a,p,UI_PAIR_CODE,ES_PASSWORD);ui_field(a,p,UI_CONNECT_ADDRESS,L"Connection address:port");ui_edit(a,p,UI_CONNECT_ADDRESS,0);
    ui_button(a,p,L"Pair",UI_PAIR);ui_button(a,p,L"Connect",UI_CONNECT);ui_button(a,p,L"Disconnect address",UI_DISCONNECT);ui_button(a,p,L"Discover wireless",UI_MDNS);
    ui_control(a,p,L"STATIC",L"Wireless debugging: pairing and connection ports can differ. Use the numeric ports shown on the phone. Codes are never saved. Discovery results appear in Diagnostics.",0,UI_WIRELESS_INFO);
}
void devices_page_layout(AppUI *a,int w,int h){
    ui_move(a,a->controls[UI_DEVICE_LIST],0,0,w,h-264);ui_move(a,a->controls[UI_REFRESH],0,h-254,92,28);ui_move(a,a->controls[UI_INSPECT],104,h-254,164,28);ui_move(a,a->controls[UI_USB],280,h-254,110,28);
    ui_move(a,a->controls[UI_DEVICE_TITLE],0,h-216,w,20);ui_move(a,a->controls[UI_DEVICE_DETAIL],0,h-194,w,32);ui_move(a,a->controls[UI_WIRELESS_TITLE],0,h-155,w,20);int pair=w/3;ui_field_layout(a,UI_PAIR_ADDRESS,0,h-131,pair-12);ui_field_layout(a,UI_PAIR_CODE,pair,h-131,150);ui_field_layout(a,UI_CONNECT_ADDRESS,pair+170,h-131,w-pair-170);
    ui_move(a,a->controls[UI_PAIR],0,h-78,92,28);ui_move(a,a->controls[UI_CONNECT],104,h-78,92,28);ui_move(a,a->controls[UI_DISCONNECT],208,h-78,164,28);ui_move(a,a->controls[UI_MDNS],384,h-78,160,28);ui_move(a,a->controls[UI_WIRELESS_INFO],0,h-41,w,40);
    RECT bounds;GetClientRect(a->controls[UI_DEVICE_LIST],&bounds);int fixed=0;int widths[]={0,116,134,86,110};for(int i=1;i<5;i++){widths[i]=MulDiv(widths[i],a->dpi,96);fixed+=widths[i];}widths[0]=bounds.right-fixed;for(int i=0;i<5;i++)ListView_SetColumnWidth(a->controls[UI_DEVICE_LIST],i,widths[i]);
}
void devices_page_update(AppUI *a,bool changed){
    HWND list=a->controls[UI_DEVICE_LIST];if(changed){a->loading=true;ListView_DeleteAllItems(list);for(int i=0;i<a->view->devices.count;i++){
        Device *d=&a->view->devices.items[i];wchar_t name[512],connection[128],state[128],android[64];utf8_to_wide(d->model[0]?d->model:"Android device",name,512);utf8_to_wide(transport_name(d->transport),connection,128);utf8_to_wide(device_state_name(d->state),state,128);utf8_to_wide(d->android[0]?d->android:"Unknown",android,64);
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=name;item.lParam=i;ListView_InsertItem(list,&item);ListView_SetItemText(list,i,1,connection);ListView_SetItemText(list,i,2,state);ListView_SetItemText(list,i,3,android);
        if(!strcmp(d->serial,a->inspected.serial)&&!strcmp(d->transport_id,a->inspected.transport_id)){a->inspected=*d;ListView_SetItemState(list,i,LVIS_SELECTED,LVIS_SELECTED);}
    }a->loading=false;}
    for(int i=0;i<a->view->devices.count;i++){Device *d=&a->view->devices.items[i];bool active=a->view->active&&!strcmp(d->serial,a->view->active_serial)&&!strcmp(d->transport_id,a->view->active_transport);wchar_t existing[32];ListView_GetItemText(list,i,4,existing,32);if(wcscmp(existing,active?L"Active":L""))ListView_SetItemText(list,i,4,active?L"Active":L"");}
    if(a->inspected.serial[0]){char s[1500];snprintf(s,sizeof(s),"Identity: %s · transport %s\r\nADB: %s · Android %s · Camera: %s · Screen: %s",a->inspected.serial,a->inspected.transport_id,device_state_name(a->inspected.state),a->inspected.android[0]?a->inspected.android:"unknown",a->inspected.sdk>=31&&a->inspected.transport!=TRANSPORT_EMULATOR?"probe/start required":"unsupported or SDK unknown",a->inspected.sdk>=21?"probe/start required":"SDK unknown / unsupported");ui_text(a->controls[UI_DEVICE_DETAIL],s);}
    ui_enable(a->controls[UI_PAIR],a->view->features.adb_pair&&!a->view->busy);ui_enable(a->controls[UI_MDNS],a->view->features.adb_mdns&&!a->view->busy);ui_enable(a->controls[UI_INSPECT],!a->view->active&&!a->view->busy&&a->inspected.serial[0]);
}
bool devices_page_command(AppUI *a,int id){
    if(id==UI_REFRESH){ui_send(a,N_REFRESH);return true;}
    if(id==UI_INSPECT){if(a->inspected.serial[0]){ui_select_device(a,&a->inspected);a->page=0;TabCtrl_SetCurSel(a->controls[UI_TABS],0);ui_layout(a);}return true;}
    if(id==UI_USB){MessageBoxW(a->window,L"On the phone: enable Developer options, then USB debugging. Connect a data-capable USB cable, unlock the phone and accept its authorization prompt.\n\nUnauthorized, offline and unknown states are not usable capture sessions. The application never bypasses authorization or restarts the shared ADB server automatically.",L"USB setup",MB_OK|MB_ICONINFORMATION);return true;}
    if(id==UI_PAIR||id==UI_CONNECT||id==UI_DISCONNECT||id==UI_MDNS){NativeCommand *c=calloc(1,sizeof(*c));if(!c)return true;c->action=id==UI_PAIR?N_PAIR:id==UI_CONNECT?N_CONNECT:id==UI_DISCONNECT?N_DISCONNECT:N_MDNS;
        GetWindowTextW(a->controls[id==UI_PAIR?UI_PAIR_ADDRESS:UI_CONNECT_ADDRESS],c->address,320);GetWindowTextW(a->controls[UI_PAIR_CODE],c->code,16);native_manager_send(a->manager,c);SecureZeroMemory(c,sizeof(*c));free(c);ui_set(a->controls[UI_PAIR_CODE],L"");return true;}
    return false;
}
