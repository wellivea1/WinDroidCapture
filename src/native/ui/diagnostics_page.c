#include "app_ui.h"
#include "../device_profile.h"
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void row(HWND list,int index,const wchar_t *name,const wchar_t *value){if(ListView_GetItemCount(list)<=index){LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=(LPWSTR)name;ListView_InsertItem(list,&item);}wchar_t old[1600];ListView_GetItemText(list,index,1,old,1600);if(wcscmp(old,value))ListView_SetItemText(list,index,1,(LPWSTR)value);}
void diagnostics_page_create(AppUI *a){
    HWND p=a->pages[3],list=ui_control(a,p,WC_LISTVIEWW,L"Observed capture stages",LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP,UI_STAGES);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=L"Stage";c.cx=200;ListView_InsertColumn(list,0,&c);c.pszText=L"Observed state";c.cx=600;ListView_InsertColumn(list,1,&c);
    ui_edit(a,p,UI_LOG,ES_MULTILINE|ES_READONLY|WS_VSCROLL);SendMessageW(a->controls[UI_LOG],WM_SETFONT,(WPARAM)a->mono,TRUE);ui_button(a,p,L"Copy capture command",UI_COPY_COMMAND);ui_button(a,p,L"Export redacted diagnostics",UI_EXPORT);
}
void diagnostics_page_layout(AppUI *a,int w,int h){ui_move(a,a->controls[UI_STAGES],0,0,w,258);ListView_SetColumnWidth(a->controls[UI_STAGES],0,MulDiv(180,a->dpi,96));ListView_SetColumnWidth(a->controls[UI_STAGES],1,MulDiv(w-198,a->dpi,96));ui_move(a,a->controls[UI_LOG],0,270,w,h-312);ui_move(a,a->controls[UI_COPY_COMMAND],0,h-32,192,30);ui_move(a,a->controls[UI_EXPORT],204,h-32,222,30);}
void diagnostics_page_update(AppUI *a){
    HWND list=a->controls[UI_STAGES];wchar_t s[1600];swprintf(s,1600,L"%hs · transport %hs",a->selected.serial[0]?device_state_name(a->selected.state):"not selected",a->selected.transport_id);row(list,0,L"ADB selection",s);
    swprintf(s,1600,L"%hs · %s",native_capture_name(a->profile.kind),a->view->active?L"session active":L"stopped");row(list,1,L"Video source",s);
    if(a->profile.kind==CAPTURE_SCREEN)swprintf(s,1600,L"Display %d · %s · FPS limit %d · %hs / %d Mbps",a->profile.screen.display_id,a->profile.screen.max_size?L"bounded long edge":L"Native",a->profile.screen.fps_limit,a->profile.encoding.codec,a->profile.encoding.bitrate_mbps);else swprintf(s,1600,L"Camera %hs · requested %d × %d / %d · %hs",a->profile.camera.id,a->profile.camera.width,a->profile.camera.height,a->profile.camera.fps,a->profile.encoding.codec);row(list,2,L"Requested configuration",s);
    swprintf(s,1600,L"%u × %u · %llu published source frames · video epoch %u",a->view->width,a->view->height,(unsigned long long)a->view->frames,a->view->video_epoch);row(list,3,L"Observed stream",s);
    swprintf(s,1600,L"%llu ms since content update · %hs",(unsigned long long)a->view->content_age_ms,a->view->camera_status);row(list,4,L"Content / health",s);
    swprintf(s,1600,L"%s · client request %u × %u / %s · requests %llu · repeated %llu",a->view->camera_registered?L"Enumerated":L"Unavailable",a->view->request_width,a->view->request_height,a->view->request_format==2?L"NV12":a->view->request_format==1?L"RGB32":L"unknown",(unsigned long long)a->view->camera_requests,(unsigned long long)a->view->repeated_samples);row(list,5,L"Windows camera",s);
    swprintf(s,1600,L"%s · %hs · PCM frames %llu",a->view->microphone_installed?L"Endpoint available":L"Not installed",a->view->mic_status,(unsigned long long)a->view->audio_samples);row(list,6,L"Phone microphone",s);
    PreviewStats stats;preview_worker_stats(a->renderer,&stats);swprintf(s,1600,L"Present submissions %llu · skipped source updates %llu · copy contention %llu · last render %.2f ms / gap %.2f ms (not phone latency)",(unsigned long long)stats.presented,(unsigned long long)stats.skipped,(unsigned long long)stats.copy_misses,stats.render_ms,stats.last_gap_ms);row(list,7,L"Integrated preview",s);row(list,8,L"Pacing setting (next Start)",a->view->settings.video_buffer?L"Smooth priority · one requested frame interval · camera and screen":L"Minimum latency · no intentional buffer · camera and screen");row(list,9,L"Receiving application",a->receiver_state);ui_text(a->controls[UI_LOG],a->view->diagnostics);
    swprintf(s,1600,L"%hs · Camera2 API extension %u · %d reported entries%s",native_device_profile_name(&a->selected),a->view->capabilities.camera_api,a->view->capabilities.camera_count,a->view->capabilities.camera_truncated?L" (truncated)":L"");row(list,10,L"Device camera profile",s);
}
bool diagnostics_page_command(AppUI *a,int id){
    if(id==UI_COPY_COMMAND){ui_copy(a,a->view->command);return true;}
    if(id==UI_EXPORT){wchar_t file[AWM_PATH]=L"AWM-diagnostics.txt";OPENFILENAMEW f={0};f.lStructSize=sizeof(f);f.hwndOwner=a->window;f.lpstrFilter=L"Text\0*.txt\0\0";f.lpstrFile=file;f.nMaxFile=AWM_PATH;f.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;f.lpstrDefExt=L"txt";if(GetSaveFileNameW(&f)){
        char *text=calloc(AWM_TEXT,1);if(text){awm_copy(text,AWM_TEXT,a->view->diagnostics);redact_text(text,&a->view->devices);HANDLE out=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD wrote;if(out!=INVALID_HANDLE_VALUE){WriteFile(out,text,(DWORD)strlen(text),&wrote,NULL);CloseHandle(out);ui_set(a->controls[UI_NOTICE],L"Redacted diagnostics exported; no media or pairing codes included.");}free(text);}}
        return true;
    }return false;
}
