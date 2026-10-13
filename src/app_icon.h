#ifndef AWM_APP_ICON_H
#define AWM_APP_ICON_H
#include <windows.h>
#include "resource.h"

typedef struct AwmAppIcons {HICON large_icon,small_icon;} AwmAppIcons;

static inline HICON awm_app_icon(HINSTANCE module,UINT dpi,int size){
    int x=size==ICON_SMALL?SM_CXSMICON:SM_CXICON;
    int y=size==ICON_SMALL?SM_CYSMICON:SM_CYICON;
    return (HICON)LoadImageW(module,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,
        GetSystemMetricsForDpi(x,dpi),GetSystemMetricsForDpi(y,dpi),0);
}
static inline void awm_app_apply_icons(HWND window,HINSTANCE module,UINT dpi,AwmAppIcons *owned){
    /* LR_SHARED caches by resource name without respecting requested size.
       Keep independently owned large/small icons and release replaced ones.
       Update on creation/DPI changes, outside status/preview refresh. */
    HICON icon_large=awm_app_icon(module,dpi,ICON_BIG),icon_small=awm_app_icon(module,dpi,ICON_SMALL);
    if(icon_large){SendMessageW(window,WM_SETICON,ICON_BIG,(LPARAM)icon_large);if(owned->large_icon)DestroyIcon(owned->large_icon);owned->large_icon=icon_large;}
    if(icon_small){SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)icon_small);if(owned->small_icon)DestroyIcon(owned->small_icon);owned->small_icon=icon_small;}
}
static inline void awm_app_release_icons(AwmAppIcons *owned){
    if(owned->large_icon)DestroyIcon(owned->large_icon);if(owned->small_icon)DestroyIcon(owned->small_icon);
    owned->large_icon=owned->small_icon=NULL;
}
#endif
