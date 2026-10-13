#ifndef AWM_APP_UI_H
#define AWM_APP_UI_H
#include "../manager.h"
#include "../preview_worker.h"
#include <commctrl.h>
enum {
    UI_TABS=10,UI_START,UI_NOTICE,UI_STATUS,
    UI_DEVICE=20,UI_SOURCE,UI_SELECTOR,UI_PRESET,UI_ORIENTATION,UI_FRAMING,UI_AUDIO,UI_ADVANCED,UI_HIDE,UI_COPY_CAMERA,UI_COPY_MIC,UI_TEST,UI_CAPTURE_INFO,UI_CAMERA_NAME,UI_MIC_NAME,UI_RECEIVER,UI_TRANSPORT,UI_PRIVACY,UI_WIDTH,UI_HEIGHT,UI_FPS,UI_BITRATE,UI_CODEC,UI_ENCODER,UI_ROTATION,UI_MIRROR,UI_ZOOM,UI_SAVED,
    UI_CAPTURE_REFRESH=48,
    UI_PREVIEW_TITLE,UI_SOURCE_TITLE,UI_OUTPUT_TITLE,UI_WIRELESS_TITLE,UI_DEVICE_TITLE,UI_PROPERTY_SCROLL,
    UI_DEVICE_LIST=60,UI_DEVICE_DETAIL,UI_REFRESH,UI_INSPECT,UI_USB,UI_PAIR_ADDRESS,UI_PAIR_CODE,UI_CONNECT_ADDRESS,UI_PAIR,UI_CONNECT,UI_DISCONNECT,UI_MDNS,UI_WIRELESS_INFO,
    UI_SETTINGS_TABS=80,UI_OUTPUT_FORMAT,UI_PREVIEW_SCALE,UI_PIPELINE_SCALE,UI_ADB,UI_SCRCPY,UI_BROWSE_ADB,UI_BROWSE_SCRCPY,UI_APPLY_PATHS,UI_INSTALL_CAMERA,UI_REGISTER_CAMERA,UI_INSTALL_MIC,UI_COMPONENTS,UI_VERSIONS,UI_CAPTURE_PATH,UI_RECONNECT,UI_ADB_LINK,UI_SCRCPY_LINK,UI_DRIVER_LINK,
    UI_BEHAVIOR_INFO=99,
    UI_THEME=100,UI_OUTPUT_SETTINGS_TITLE,UI_COMPONENT_TITLE,UI_VIDEO_PACING,
    UI_STAGES=110,UI_LOG,UI_COPY_COMMAND,UI_EXPORT,
    UI_CAMERA_SETUP_TITLE=115,UI_MIC_SETUP_TITLE,UI_MIC_SETUP_TEXT,UI_COMPONENT_HELP,UI_BEHAVIOR_TITLE,UI_DRIVER_TITLE,UI_DRIVER_HELP
};
typedef enum { UI_TONE_NEUTRAL,UI_TONE_READY,UI_TONE_PENDING,UI_TONE_WARNING,UI_TONE_FAILURE } UIStatusTone;
typedef struct UIStatusItem {wchar_t label[16],value[48];UIStatusTone tone;} UIStatusItem;
typedef struct AppUI {
    HINSTANCE instance;HWND window,pages[4],settings_pages[3],controls[256],property,property_viewport,property_content,canvas,placeholder;
    HFONT font,mono,heading;HBRUSH background,white,nav_brush;UINT dpi;int page,settings_page,property_scroll;
    COLORREF bg,surface,nav,text,muted,border,accent,warning,success,info,error;bool dark,high_contrast,theme_ready;unsigned theme_mode;HWND last_focus;HDWP layout_batch;int wheel_remainder;
    UIStatusItem status[4];
    RECT settings_sections[3];
    NativeManager *manager;NativeView *view;NativeCaptureProfile profile;Device selected,inspected;PreviewWorker *renderer;int property_fields_width,property_content_height;
    bool loading,advanced,hide,closing,settings_loaded,probing,profile_dirty,settings_pending;char device_key[24576],caps_key[1024];
    unsigned pending_preview,pending_pipeline,pending_width,pending_fps,pending_theme,pending_pacing,probe_revision;
    Process receiver;bool receiver_started;wchar_t receiver_state[256];unsigned active_framing;
} AppUI;
int app_ui_run(HINSTANCE instance,int show);
HWND ui_control(AppUI *a,HWND parent,const wchar_t *class,const wchar_t *text,DWORD style,int id);
HWND ui_button(AppUI *a,HWND parent,const wchar_t *text,int id);
HWND ui_combo(AppUI *a,HWND parent,int id);
HWND ui_edit(AppUI *a,HWND parent,int id,DWORD style);
void ui_field(AppUI *a,HWND parent,int id,const wchar_t *name);
void ui_move(AppUI *a,HWND h,int x,int y,int w,int height);
void ui_text(HWND h,const char *text);
void ui_set(HWND h,const wchar_t *text);
void ui_select(HWND h,int index);
void ui_check(HWND h,bool checked);
void ui_enable(HWND h,bool enabled);
void ui_show_mode(HWND h,int mode);
void ui_item(HWND h,const wchar_t *text);
void ui_field_layout(AppUI *a,int id,int x,int y,int w);
void ui_copy(AppUI *a,const wchar_t *text);
void ui_send(AppUI *a,NativeAction action);
void ui_save_profile(AppUI *a);
void ui_select_device(AppUI *a,const Device *d);
void ui_profile_controls(AppUI *a);
void ui_layout(AppUI *a);
void ui_preview_state(AppUI *a);
void ui_theme_attach(AppUI *a,HWND control,const wchar_t *class,DWORD style);
void ui_theme_update(AppUI *a,unsigned mode,bool force);
bool ui_theme_message(AppUI *a,HWND h,UINT m,WPARAM w,LPARAM l,LRESULT *result);
bool ui_heading_id(int id);
void capture_page_create(AppUI *a);
void capture_page_layout(AppUI *a,int w,int h);
void capture_property_layout(AppUI *a,int height);
void capture_property_scroll(AppUI *a);
void capture_page_update(AppUI *a);
bool capture_page_command(AppUI *a,int id,int code);
void devices_page_create(AppUI *a);
void devices_page_layout(AppUI *a,int w,int h);
void devices_page_update(AppUI *a,bool devices_changed);
bool devices_page_command(AppUI *a,int id);
void settings_page_create(AppUI *a);
void settings_page_layout(AppUI *a,int w,int h);
void settings_page_update(AppUI *a);
bool settings_page_command(AppUI *a,int id,int code);
void diagnostics_page_create(AppUI *a);
void diagnostics_page_layout(AppUI *a,int w,int h);
void diagnostics_page_update(AppUI *a);
bool diagnostics_page_command(AppUI *a,int id);
#endif
