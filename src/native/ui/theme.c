#include "app_ui.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windowsx.h>
#include <wchar.h>
/* Paint documented native controls; their input, focus and accessibility remain native. */
static int px(AppUI *a,int dip){return MulDiv(dip,a->dpi,96);}
static void fill(HDC dc,const RECT *r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,r,b);DeleteObject(b);}
/* Each custom control owns its entire face, including WM_PRINTCLIENT used by
   native/composited repaint paths. Never let an alternate path paint classic
   borders or arrows over the themed face. Blitting respects the destination's
   update/visibility clip; all primitive geometry is drawn on a complete bitmap. */
typedef struct ControlPaint { HDC output,dc; HBITMAP bitmap,previous; PAINTSTRUCT ps; RECT bounds; bool begun; int saved; } ControlPaint;
static bool control_begin(ControlPaint *p,HWND h,UINT message,WPARAM target){
    memset(p,0,sizeof(*p));GetClientRect(h,&p->bounds);
    p->begun=message==WM_PAINT&&!target;p->output=p->begun?BeginPaint(h,&p->ps):(HDC)target;
    if(!p->output)return false;
    /* Optional local paint trace for investigating clipping; contains geometry,
       no device information, credentials, or captured media. */
    static unsigned traced;wchar_t trace_path[AWM_PATH];
    if(traced<120&&GetEnvironmentVariableW(L"AWM_UI_PAINT_TRACE",trace_path,AWM_PATH)&&GetDlgCtrlID(h)>=UI_COPY_CAMERA){
        RECT clip,window,parent;POINT viewport;GetClipBox(p->output,&clip);GetWindowRect(h,&window);GetClientRect(GetParent(h),&parent);GetViewportOrgEx(p->output,&viewport);
        FILE *log=_wfopen(trace_path,L"a");if(log){fprintf(log,"id=%d message=%04x bounds=%ld,%ld clip=%ld,%ld,%ld,%ld viewport=%ld,%ld parent=%ld,%ld window=%ld,%ld\n",GetDlgCtrlID(h),message,p->bounds.right,p->bounds.bottom,clip.left,clip.top,clip.right,clip.bottom,viewport.x,viewport.y,parent.right,parent.bottom,window.right-window.left,window.bottom-window.top);fclose(log);traced++;}
    }
    p->dc=CreateCompatibleDC(p->output);p->bitmap=CreateCompatibleBitmap(p->output,p->bounds.right>0?p->bounds.right:1,p->bounds.bottom>0?p->bounds.bottom:1);
    if(p->dc&&p->bitmap)p->previous=SelectObject(p->dc,p->bitmap);
    else{if(p->dc)DeleteDC(p->dc);if(p->bitmap)DeleteObject(p->bitmap);p->dc=p->output;p->bitmap=NULL;p->saved=SaveDC(p->output);}
    return true;
}
static void control_end(ControlPaint *p,HWND h){
    if(p->bitmap){BitBlt(p->output,0,0,p->bounds.right,p->bounds.bottom,p->dc,0,0,SRCCOPY);SelectObject(p->dc,p->previous);DeleteObject(p->bitmap);DeleteDC(p->dc);}
    else if(p->saved)RestoreDC(p->output,p->saved);
    if(p->begun)EndPaint(h,&p->ps);
}
static void frame(AppUI *a,HDC dc,RECT r,COLORREF color){
    int edge=px(a,1);if(edge<1)edge=1;
    RECT top={r.left,r.top,r.right,r.top+edge},bottom={r.left,r.bottom-edge,r.right,r.bottom};
    RECT left={r.left,r.top,r.left+edge,r.bottom},right={r.right-edge,r.top,r.right,r.bottom};
    fill(dc,&top,color);fill(dc,&bottom,color);fill(dc,&left,color);fill(dc,&right,color);
}
static void chevron(AppUI *a,HDC dc,RECT bounds,bool down,COLORREF color){
    int half=px(a,4),thickness=px(a,2),margin=px(a,2);if(thickness<1)thickness=1;
    int available=(bounds.right-bounds.left-2*margin-1)/2;if(half>available)half=available;
    available=bounds.bottom-bounds.top-2*margin-thickness;if(half>available)half=available;if(half<1)return;
    int cx=(bounds.left+bounds.right-1)/2,top=(bounds.top+bounds.bottom-half-thickness)/2;
    HBRUSH brush=CreateSolidBrush(color);
    for(int offset=-half;offset<=half;offset++){
        int y=top+(down?half-abs(offset):abs(offset));RECT stroke={cx+offset,y,cx+offset+1,y+thickness};FillRect(dc,&stroke,brush);
    }
    DeleteObject(brush);
}
static void rule(HDC dc,int x,int y,int end,COLORREF color,bool vertical){
    HPEN p=CreatePen(PS_SOLID,1,color),old=SelectObject(dc,p);MoveToEx(dc,x,y,NULL);LineTo(dc,vertical?x:end,vertical?end:y);SelectObject(dc,old);DeleteObject(p);
}
bool ui_heading_id(int id){return id==UI_PREVIEW_TITLE||id==UI_SOURCE_TITLE||id==UI_OUTPUT_TITLE||id==UI_WIRELESS_TITLE||id==UI_DEVICE_TITLE||id==UI_OUTPUT_SETTINGS_TITLE||id==UI_COMPONENT_TITLE||id==UI_CAMERA_SETUP_TITLE||id==UI_MIC_SETUP_TITLE||id==UI_BEHAVIOR_TITLE||id==UI_DRIVER_TITLE;}
static void label(AppUI *a,HDC dc,RECT r,const wchar_t *text,COLORREF color,UINT align){
    HFONT old=SelectObject(dc,a->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|align);SelectObject(dc,old);
}
static bool show_focus(HWND h){return GetFocus()==h&&!(SendMessageW(h,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);}
static COLORREF status_color(AppUI *a,UIStatusTone tone){
    switch(tone){case UI_TONE_READY:return a->success;case UI_TONE_PENDING:return a->info;case UI_TONE_WARNING:return a->warning;case UI_TONE_FAILURE:return a->error;default:return a->muted;}
}
static void paint_status(AppUI *a,HDC dc,RECT bounds){
    FillRect(dc,&bounds,a->nav_brush);int gap=px(a,10),lead=px(a,10),diameter=px(a,6),widths[4],prefix[4],total=0;
    int available=bounds.right-3*gap;if(available<1)return;
    HFONT old=SelectObject(dc,a->font);
    for(int i=0;i<4;i++){
        SIZE heading,value;GetTextExtentPoint32W(dc,a->status[i].label,(int)wcslen(a->status[i].label),&heading);GetTextExtentPoint32W(dc,a->status[i].value,(int)wcslen(a->status[i].value),&value);
        prefix[i]=heading.cx+px(a,4);widths[i]=lead+prefix[i]+value.cx;total+=widths[i];
    }
    int x=0,remaining=available;
    for(int i=0;i<4;i++){
        int width=total>available?MulDiv(widths[i],available,total):widths[i];if(i==3&&total>available)width=remaining;remaining-=width;
        COLORREF color=status_color(a,a->status[i].tone);HBRUSH brush=CreateSolidBrush(color),previous=SelectObject(dc,brush);HPEN pen=SelectObject(dc,GetStockObject(NULL_PEN));
        int y=(bounds.bottom-diameter)/2;Ellipse(dc,x,y,x+diameter,y+diameter);SelectObject(dc,pen);SelectObject(dc,previous);DeleteObject(brush);
        RECT text={x+lead,0,x+width,bounds.bottom},heading=text;heading.right=heading.left+prefix[i];
        label(a,dc,heading,a->status[i].label,a->muted,DT_LEFT|DT_NOPREFIX);text.left+=prefix[i];label(a,dc,text,a->status[i].value,color,DT_LEFT|DT_NOPREFIX);x+=width+gap;
    }
    SelectObject(dc,old);
}
static LRESULT CALLBACK status_strip(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    AppUI *a=(void *)ref;(void)id;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){ControlPaint paint;if(control_begin(&paint,h,m,w)){paint_status(a,paint.dc,paint.bounds);control_end(&paint,h);}return 0;}
    LRESULT result=DefSubclassProc(h,m,w,l);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,status_strip,8);return result;
}
static LRESULT CALLBACK navigation(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    AppUI *a=(void *)ref;(void)id;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){
        ControlPaint paint;if(!control_begin(&paint,h,m,w))return 0;HDC dc=paint.dc;RECT client=paint.bounds;FillRect(dc,&client,a->nav_brush);
        int current=TabCtrl_GetCurSel(h);for(int i=0;i<TabCtrl_GetItemCount(h);i++){
            RECT r;TabCtrl_GetItemRect(h,i,&r);r.top=0;r.bottom=client.bottom-1;
            /* Native item rectangles leave an outer inset. Our continuous
               tab strip starts at the control's edge, including its rule. */
            if(i==0)r.left=0;
            wchar_t name[80];TCITEMW t={0};t.mask=TCIF_TEXT;t.pszText=name;t.cchTextMax=80;TabCtrl_GetItem(h,i,&t);
            if(i==current)fill(dc,&r,a->accent);
            label(a,dc,r,name,i==current?GetSysColor(COLOR_HIGHLIGHTTEXT):a->text,DT_CENTER);
            if(i==0)rule(dc,r.left,0,r.bottom,a->border,true);
            rule(dc,r.right-1,0,r.bottom,a->border,true);
            if(i==current&&show_focus(h)){InflateRect(&r,-px(a,4),-px(a,4));SetTextColor(dc,GetSysColor(COLOR_HIGHLIGHTTEXT));DrawFocusRect(dc,&r);}
        }
        rule(dc,0,client.bottom-1,client.right,a->border,false);control_end(&paint,h);return 0;
    }
    LRESULT result=DefSubclassProc(h,m,w,l);
    if(m==TCM_SETCURSEL||m==TCM_SETCURFOCUS||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_KEYDOWN||m==WM_LBUTTONUP)InvalidateRect(h,NULL,FALSE);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,navigation,1);return result;
}
static LRESULT CALLBACK combo_face(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    AppUI *a=(void *)ref;(void)id;
    if(m==WM_MOUSEWHEEL&&GetParent(h)==a->property_content&&!SendMessageW(h,CB_GETDROPPEDSTATE,0,0))return SendMessageW(a->property,m,w,l);
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){
        ControlPaint paint;if(!control_begin(&paint,h,m,w))return 0;HDC dc=paint.dc;RECT r=paint.bounds;FillRect(dc,&r,a->white);
        frame(a,dc,r,show_focus(h)?a->accent:a->border);
        COMBOBOXINFO info={0};info.cbSize=sizeof(info);RECT arrow=r;arrow.left=arrow.right-px(a,24);
        if(GetComboBoxInfo(h,&info)&&info.rcButton.right>info.rcButton.left)arrow=info.rcButton;
        wchar_t name[1024]=L"";GetWindowTextW(h,name,1024);RECT text=r;text.left+=px(a,7);text.right=arrow.left-px(a,4);
        label(a,dc,text,name,IsWindowEnabled(h)?a->text:a->muted,DT_LEFT|DT_NOPREFIX);
        chevron(a,dc,arrow,true,IsWindowEnabled(h)?a->text:a->muted);
        if(show_focus(h)){InflateRect(&text,-px(a,1),-px(a,4));DrawFocusRect(dc,&text);}control_end(&paint,h);return 0;
    }
    LRESULT result=DefSubclassProc(h,m,w,l);
    if(m==CB_SETCURSEL||m==WM_ENABLE||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_KEYUP||m==WM_LBUTTONUP)InvalidateRect(h,NULL,FALSE);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,combo_face,2);return result;
}
static LRESULT CALLBACK checkbox(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    AppUI *a=(void *)ref;(void)id;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){
        ControlPaint paint;if(!control_begin(&paint,h,m,w))return 0;HDC dc=paint.dc;RECT r=paint.bounds;FillRect(dc,&r,a->background);
        int size=px(a,14);RECT check={0,(r.bottom-size)/2,size,(r.bottom+size)/2};fill(dc,&check,a->surface);
        HBRUSH b=CreateSolidBrush(show_focus(h)?a->accent:a->border);FrameRect(dc,&check,b);DeleteObject(b);
        COLORREF color=IsWindowEnabled(h)?a->text:a->muted;
        if(SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED){HPEN p=CreatePen(PS_SOLID,px(a,2),color),old=SelectObject(dc,p);MoveToEx(dc,px(a,3),check.top+px(a,7),NULL);LineTo(dc,px(a,6),check.top+px(a,10));LineTo(dc,px(a,11),check.top+px(a,3));SelectObject(dc,old);DeleteObject(p);}
        wchar_t name[512];GetWindowTextW(h,name,512);r.left=px(a,22);label(a,dc,r,name,color,DT_LEFT);
        if(show_focus(h)){InflateRect(&r,-px(a,1),-px(a,3));DrawFocusRect(dc,&r);}control_end(&paint,h);return 0;
    }
    LRESULT result=DefSubclassProc(h,m,w,l);if(m==BM_SETCHECK||m==WM_ENABLE||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_KEYUP||m==WM_LBUTTONUP)InvalidateRect(h,NULL,FALSE);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,checkbox,3);return result;
}
static void paint_button(AppUI *a,HWND h,HDC dc,RECT r,bool pressed,bool focused){
    bool disabled=!IsWindowEnabled(h),primary=GetDlgCtrlID(h)==UI_START;
    COLORREF face=primary&&!disabled?a->accent:a->surface;if(pressed)face=a->nav;fill(dc,&r,face);
    frame(a,dc,r,primary&&!disabled?a->accent:a->border);wchar_t text[512];GetWindowTextW(h,text,512);
    RECT caption=r;caption.left+=px(a,7);caption.right-=px(a,7);caption.top+=px(a,2);caption.bottom-=px(a,2);
    COLORREF color=disabled?a->muted:primary&&!pressed?GetSysColor(COLOR_HIGHLIGHTTEXT):a->text;
    if(GetDlgCtrlID(h)==UI_ADVANCED){RECT arrow=r;arrow.left=arrow.right-px(a,24);caption.right=arrow.left;chevron(a,dc,arrow,!a->advanced,color);}
    label(a,dc,caption,text,color,DT_CENTER);
    if(focused){InflateRect(&r,-px(a,4),-px(a,4));DrawFocusRect(dc,&r);}
}
static LRESULT CALLBACK button(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    AppUI *a=(void *)ref;(void)id;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){ControlPaint paint;if(!control_begin(&paint,h,m,w))return 0;paint_button(a,h,paint.dc,paint.bounds,(SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED)!=0,show_focus(h));control_end(&paint,h);return 0;}
    LRESULT result=DefSubclassProc(h,m,w,l);
    if(m==BM_SETSTATE||m==WM_ENABLE||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_KEYUP||m==WM_LBUTTONUP||m==WM_SETTEXT)InvalidateRect(h,NULL,FALSE);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,button,7);return result;
}
static LRESULT CALLBACK header(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    AppUI *a=(void *)ref;(void)id;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){ControlPaint paint;if(!control_begin(&paint,h,m,w))return 0;HDC dc=paint.dc;RECT client=paint.bounds;FillRect(dc,&client,a->nav_brush);
        for(int i=0;i<Header_GetItemCount(h);i++){RECT r;Header_GetItemRect(h,i,&r);wchar_t text[128];HDITEMW t={0};t.mask=HDI_TEXT;t.pszText=text;t.cchTextMax=128;Header_GetItem(h,i,&t);rule(dc,r.right-1,0,r.bottom,a->border,true);r.left+=px(a,7);label(a,dc,r,text,a->text,DT_LEFT);}
        rule(dc,0,client.bottom-1,client.right,a->border,false);control_end(&paint,h);return 0;
    }
    LRESULT result=DefSubclassProc(h,m,w,l);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,header,4);return result;
}
typedef struct ScrollInteraction {AppUI *ui;bool dragging,hot,focused,focus_visible;int grab_offset,repeat_code;UINT repeat_delay,blink_time;HBRUSH focus_pattern;} ScrollInteraction;
typedef struct ScrollGeometry {RECT bounds;SCROLLINFO info;int arrow,top,bottom,travel,maximum;} ScrollGeometry;
static bool scroll_geometry(AppUI *a,HWND h,ScrollGeometry *g){
    memset(g,0,sizeof(*g));GetClientRect(h,&g->bounds);g->info.cbSize=sizeof(g->info);g->info.fMask=SIF_ALL;
    if(!GetScrollInfo(h,SB_CTL,&g->info))return false;
    g->arrow=g->bounds.right;if(g->arrow*2>g->bounds.bottom)g->arrow=g->bounds.bottom/2;
    int range=g->info.nMax-g->info.nMin+1,track=g->bounds.bottom-2*g->arrow;
    int size=range>0?MulDiv((int)g->info.nPage,track,range):track;int minimum=px(a,20);if(size<minimum)size=minimum;if(size>track)size=track;
    g->maximum=g->info.nMax-(int)g->info.nPage+1;if(g->maximum<g->info.nMin)g->maximum=g->info.nMin;
    g->travel=track-size;g->top=g->arrow+(g->maximum>g->info.nMin?MulDiv(g->info.nPos-g->info.nMin,g->travel,g->maximum-g->info.nMin):0);g->bottom=g->top+size;return true;
}
static void scroll_notify(HWND h,int code,int position){SendMessageW(GetParent(h),WM_VSCROLL,MAKEWPARAM(code,position),(LPARAM)h);}
static void scroll_focus_animation(ScrollInteraction *state,HWND h){
    bool focused=GetFocus()==h&&IsWindowEnabled(h);UINT blink=GetCaretBlinkTime();
    if(!focused){KillTimer(h,2);state->focused=state->focus_visible=false;return;}
    if(!state->focused||blink!=state->blink_time){
        KillTimer(h,2);state->focused=state->focus_visible=true;state->blink_time=blink;
        if(blink&&blink!=INFINITE)SetTimer(h,2,blink,NULL);
    }
}
static void paint_scroll_focus(ScrollInteraction *state,HDC dc,const RECT *thumb){
    if(!state->focused||!state->focus_visible||!state->focus_pattern)return;
    /* The same gray checker/inversion as the former Windows gray caret, with
       the user's blink cadence. Paint it in the thumb's buffer, not as an OS
       caret over a composited pane: that overlay tears during scrolling. */
    int saved=SaveDC(dc);SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,RGB(0,0,0));
    SelectObject(dc,state->focus_pattern);SetBrushOrgEx(dc,thumb->left,thumb->top,NULL);
    PatBlt(dc,thumb->left,thumb->top,thumb->right-thumb->left,thumb->bottom-thumb->top,PATINVERT);RestoreDC(dc,saved);
}
static void paint_scrollbar(ScrollInteraction *state,HWND h,HDC dc){
    AppUI *a=state->ui;
    RECT r;GetClientRect(h,&r);FillRect(dc,&r,a->background);
    ScrollGeometry g;if(!scroll_geometry(a,h,&g))return;int arrow=g.arrow,top=g.top,bottom=g.bottom;
    COLORREF contrast=a->high_contrast?a->text:a->dark?RGB(137,147,163):RGB(114,124,138);
    RECT thumb={px(a,3),top,r.right-px(a,3),bottom};if(bottom>top){
        /* Inverting the blue hover fill produces an unwanted red/orange phase.
           Keep the original gray pattern phase; use blue between those blinks. */
        bool patterned=state->focused&&state->focus_visible&&state->focus_pattern;
        fill(dc,&thumb,(state->dragging||state->hot)&&!patterned?a->accent:contrast);paint_scroll_focus(state,dc,&thumb);
    }
    RECT up={0,0,r.right,arrow},down={0,r.bottom-arrow,r.right,r.bottom};chevron(a,dc,up,false,a->text);chevron(a,dc,down,true,a->text);
}
static LRESULT CALLBACK scrollbar(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    ScrollInteraction *state=(void *)ref;AppUI *a=state->ui;(void)id;if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){ControlPaint paint;if(!control_begin(&paint,h,m,w))return 0;paint_scrollbar(state,h,paint.dc);control_end(&paint,h);return 0;}
    /* The classic SCROLLBAR procedure paints directly while handling its modal
       drag loop. Own input as well as paint so those pixels can never leak into
       the themed thumb. Keep the native HWND, range and accessibility provider. */
    if(m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK){
        if(!IsWindowEnabled(h))return 0;ScrollGeometry g;if(!scroll_geometry(a,h,&g))return 0;int y=GET_Y_LPARAM(l);SetFocus(h);SetCapture(h);
        if(y>=g.top&&y<g.bottom){state->dragging=true;state->grab_offset=y-g.top;state->repeat_code=-1;}
        else{state->repeat_code=y<g.arrow?SB_LINEUP:y>=g.bounds.bottom-g.arrow?SB_LINEDOWN:y<g.top?SB_PAGEUP:SB_PAGEDOWN;scroll_notify(h,state->repeat_code,0);state->repeat_delay=350;SetTimer(h,1,state->repeat_delay,NULL);}
        InvalidateRect(h,NULL,FALSE);return 0;
    }
    if(m==WM_MOUSEMOVE){
        ScrollGeometry g;if(!scroll_geometry(a,h,&g))return 0;int y=GET_Y_LPARAM(l);
        if(state->dragging&&GetCapture()==h&&g.travel>0){int offset=y-state->grab_offset-g.arrow;if(offset<0)offset=0;if(offset>g.travel)offset=g.travel;int position=g.info.nMin+MulDiv(offset,g.maximum-g.info.nMin,g.travel);scroll_notify(h,SB_THUMBTRACK,position);}
        bool hot=y>=g.top&&y<g.bottom;if(hot!=state->hot){state->hot=hot;InvalidateRect(h,NULL,FALSE);}
        TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;
    }
    if(m==WM_MOUSELEAVE){state->hot=false;InvalidateRect(h,NULL,FALSE);return 0;}
    if(m==WM_TIMER&&w==1){if(GetCapture()!=h){KillTimer(h,1);return 0;}if(state->repeat_code>=0)scroll_notify(h,state->repeat_code,0);if(state->repeat_delay!=55){state->repeat_delay=55;SetTimer(h,1,55,NULL);}return 0;}
    if(m==WM_TIMER&&w==2){state->focus_visible=!state->focus_visible;InvalidateRect(h,NULL,FALSE);return 0;}
    if(m==WM_LBUTTONUP||m==WM_CANCELMODE||m==WM_CAPTURECHANGED){bool owned=GetCapture()==h;bool drag=state->dragging;state->dragging=false;state->repeat_code=-1;KillTimer(h,1);if(m!=WM_CAPTURECHANGED&&owned)ReleaseCapture();if(drag){SCROLLINFO info={0};info.cbSize=sizeof(info);info.fMask=SIF_POS;GetScrollInfo(h,SB_CTL,&info);scroll_notify(h,SB_THUMBPOSITION,info.nPos);}scroll_notify(h,SB_ENDSCROLL,0);InvalidateRect(h,NULL,FALSE);return 0;}
    if(m==WM_GETDLGCODE)return DLGC_WANTARROWS;
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_ENABLE||m==WM_KEYUP){scroll_focus_animation(state,h);InvalidateRect(h,NULL,FALSE);return 0;}
    if(m==WM_KEYDOWN){int code=w==VK_UP?SB_LINEUP:w==VK_DOWN?SB_LINEDOWN:w==VK_PRIOR?SB_PAGEUP:w==VK_NEXT?SB_PAGEDOWN:w==VK_HOME?SB_TOP:w==VK_END?SB_BOTTOM:-1;if(code>=0)scroll_notify(h,code,0);return 0;}
    if(m==WM_MOUSEWHEEL){SendMessageW(GetParent(h),m,w,l);return 0;}
    LRESULT result=DefSubclassProc(h,m,w,l);
    /* Range changes can occur inside DeferWindowPos. Queue paint until all
       siblings are in place; never synchronously present that partial layout. */
    if(m==SBM_SETSCROLLINFO||m==WM_SIZE){scroll_focus_animation(state,h);InvalidateRect(h,NULL,FALSE);}
    if(m==WM_NCDESTROY){KillTimer(h,1);KillTimer(h,2);if(state->focus_pattern)DeleteObject(state->focus_pattern);RemoveWindowSubclass(h,scrollbar,5);free(state);}return result;
}
static LRESULT CALLBACK edit_frame(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){
    AppUI *a=(void *)ref;(void)id;if(m==WM_MOUSEWHEEL&&GetParent(h)==a->property_content)return SendMessageW(a->property,m,w,l);LRESULT result=DefSubclassProc(h,m,w,l);
    if(m==WM_NCPAINT){HDC dc=GetWindowDC(h);if(dc){RECT r,client;GetWindowRect(h,&r);GetClientRect(h,&client);POINT origin={0,0};ClientToScreen(h,&origin);int edge=origin.x-r.left;r.right-=r.left;r.bottom-=r.top;r.left=r.top=0;
        HBRUSH b=CreateSolidBrush(show_focus(h)?a->accent:a->border);for(int i=0;i<edge;i++){FrameRect(dc,&r,b);InflateRect(&r,-1,-1);}DeleteObject(b);ReleaseDC(h,dc);}}
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS)RedrawWindow(h,NULL,NULL,RDW_INVALIDATE|RDW_FRAME);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,edit_frame,6);return result;
}
void ui_theme_attach(AppUI *a,HWND h,const wchar_t *class,DWORD style){
    if(!wcscmp(class,L"STATIC")&&GetDlgCtrlID(h)==UI_STATUS){SetWindowSubclass(h,status_strip,8,(DWORD_PTR)a);return;}
    if(!h)return;
    if(!wcscmp(class,WC_TABCONTROLW)){SetWindowTheme(h,L"",L"");SetWindowSubclass(h,navigation,1,(DWORD_PTR)a);}
    else if(!wcscmp(class,L"COMBOBOX")){SetWindowTheme(h,L"",L"");SetWindowSubclass(h,combo_face,2,(DWORD_PTR)a);}
    else if(!wcscmp(class,L"BUTTON")&&(style&BS_TYPEMASK)==BS_AUTOCHECKBOX)SetWindowSubclass(h,checkbox,3,(DWORD_PTR)a);
    else if(!wcscmp(class,L"BUTTON"))SetWindowSubclass(h,button,7,(DWORD_PTR)a);
    else if(!wcscmp(class,L"EDIT")){SetWindowTheme(h,L"",L"");SetWindowSubclass(h,edit_frame,6,(DWORD_PTR)a);}
    else if(!wcscmp(class,L"SCROLLBAR")){ScrollInteraction *state=calloc(1,sizeof(*state));if(state){state->ui=a;state->repeat_code=-1;WORD checker[]={0x00aa,0x0055,0x00aa,0x0055,0x00aa,0x0055,0x00aa,0x0055};HBITMAP pattern=CreateBitmap(8,8,1,1,checker);if(pattern){state->focus_pattern=CreatePatternBrush(pattern);DeleteObject(pattern);}SetWindowTheme(h,L"",L"");if(!SetWindowSubclass(h,scrollbar,5,(DWORD_PTR)state)){if(state->focus_pattern)DeleteObject(state->focus_pattern);free(state);}}}
    else if(!wcscmp(class,WC_LISTVIEWW)){SetWindowTheme(h,L"",L"");SetWindowSubclass(ListView_GetHeader(h),header,4,(DWORD_PTR)a);}
}
void ui_theme_update(AppUI *a,unsigned mode,bool force){
    HIGHCONTRASTW hc={0};hc.cbSize=sizeof(hc);SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);bool high=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0;
    DWORD light=1,size=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,NULL,&light,&size);
    bool dark=!high&&(mode==2||(mode==0&&!light));
    if(!force&&a->theme_ready&&mode==a->theme_mode&&dark==a->dark&&high==a->high_contrast)return;
    a->theme_mode=mode;a->dark=dark;a->high_contrast=high;a->theme_ready=true;
    if(high){a->bg=a->nav=GetSysColor(COLOR_BTNFACE);a->surface=GetSysColor(COLOR_WINDOW);a->text=GetSysColor(COLOR_WINDOWTEXT);a->muted=GetSysColor(COLOR_GRAYTEXT);a->border=GetSysColor(COLOR_WINDOWTEXT);a->accent=GetSysColor(COLOR_HIGHLIGHT);a->warning=a->success=a->info=a->error=a->text;}
    else if(dark){a->bg=RGB(25,27,30);a->surface=RGB(37,40,44);a->nav=RGB(16,18,21);a->text=RGB(232,234,237);a->muted=RGB(158,166,176);a->border=RGB(70,76,84);a->accent=RGB(0,95,184);a->warning=RGB(244,189,89);a->success=RGB(107,223,141);a->info=RGB(115,183,255);a->error=RGB(255,125,125);}
    else{a->bg=RGB(243,243,243);a->surface=RGB(255,255,255);a->nav=RGB(229,232,236);a->text=RGB(32,32,32);a->muted=RGB(95,101,109);a->border=RGB(191,196,203);a->accent=RGB(0,95,184);a->warning=RGB(138,87,0);a->success=RGB(19,115,51);a->info=RGB(0,90,158);a->error=RGB(179,38,30);}
    if(a->background)DeleteObject(a->background);if(a->white)DeleteObject(a->white);if(a->nav_brush)DeleteObject(a->nav_brush);
    a->background=CreateSolidBrush(a->bg);a->white=CreateSolidBrush(a->surface);a->nav_brush=CreateSolidBrush(a->nav);
    BOOL caption=dark;DwmSetWindowAttribute(a->window,DWMWA_USE_IMMERSIVE_DARK_MODE,&caption,sizeof(caption));
    int lists[]={UI_DEVICE_LIST,UI_STAGES};for(unsigned i=0;i<ARRAYSIZE(lists);i++){HWND list=a->controls[lists[i]];if(list){ListView_SetBkColor(list,a->surface);ListView_SetTextBkColor(list,a->surface);ListView_SetTextColor(list,a->text);}}
    RedrawWindow(a->window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}
static void paint_surface(AppUI *a,HWND h,HDC dc){
    RECT r;GetClientRect(h,&r);FillRect(dc,&r,a->background);int w=MulDiv(r.right,96,a->dpi),height=MulDiv(r.bottom,96,a->dpi);
    if(h==a->window){RECT nav={0,0,r.right,px(a,42)};FillRect(dc,&nav,a->nav_brush);rule(dc,0,px(a,42)-1,r.right,a->border,false);rule(dc,0,px(a,height-52),r.right,a->border,false);}
    else if(h==a->pages[0]){RECT source={px(a,w-304),px(a,68),r.right,px(a,height-8)};frame(a,dc,source,a->border);rule(dc,0,px(a,height-134),px(a,w-328),a->border,false);}
    else if(h==a->pages[1]){rule(dc,0,px(a,height-159),r.right,a->border,false);}
    else if(h==a->settings_pages[0]){
        rule(dc,px(a,(w<980?420:460)+14),px(a,6),px(a,height-32),a->border,true);frame(a,dc,a->settings_sections[0],a->border);
        rule(dc,a->settings_sections[0].left+px(a,12),px(a,180),a->settings_sections[0].right-px(a,12),a->border,false);
    }else if(h==a->settings_pages[2]){frame(a,dc,a->settings_sections[1],a->border);frame(a,dc,a->settings_sections[2],a->border);}
}
bool ui_theme_message(AppUI *a,HWND h,UINT m,WPARAM w,LPARAM l,LRESULT *result){
    if(m==WM_ERASEBKGND){*result=1;return true;}
    if(m==WM_PAINT||m==WM_PRINTCLIENT){
        ControlPaint paint;if(control_begin(&paint,h,m,w)){paint_surface(a,h,paint.dc);control_end(&paint,h);}*result=0;return true;
    }
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORLISTBOX||m==WM_CTLCOLORBTN||m==WM_CTLCOLORSCROLLBAR){
        HDC dc=(HDC)w;HWND control=(HWND)l;wchar_t class[40];GetClassNameW(control,class,40);int id=GetDlgCtrlID(control);bool field=!wcscmp(class,L"EDIT")||m==WM_CTLCOLORLISTBOX;
        COLORREF color=id==UI_MIC_NAME&&!a->view->microphone_installed?a->warning:(id==UI_TRANSPORT||id==UI_PRIVACY||id==UI_CAPTURE_INFO||id==UI_SAVED? a->muted:a->text);
        bool navigation_status=h==a->window&&(id==UI_STATUS||id==UI_NOTICE);
        SetTextColor(dc,color);SetBkColor(dc,field?a->surface:navigation_status?a->nav:a->bg);*result=(LRESULT)(field?a->white:navigation_status?a->nav_brush:a->background);return true;
    }
    if(m==WM_MEASUREITEM){MEASUREITEMSTRUCT *v=(void *)l;if(v->CtlType==ODT_COMBOBOX){v->itemHeight=(UINT)px(a,22);*result=TRUE;return true;}}
    if(m==WM_DRAWITEM){DRAWITEMSTRUCT *v=(void *)l;RECT r=v->rcItem;bool disabled=(v->itemState&ODS_DISABLED)!=0;
        if(v->CtlType==ODT_BUTTON){
            ControlPaint paint;if(control_begin(&paint,v->hwndItem,WM_PRINTCLIENT,(WPARAM)v->hDC)){paint_button(a,v->hwndItem,paint.dc,paint.bounds,(v->itemState&ODS_SELECTED)!=0,(v->itemState&ODS_FOCUS)&&!(v->itemState&ODS_NOFOCUSRECT));control_end(&paint,v->hwndItem);}*result=TRUE;return true;
        }
        if(v->CtlType==ODT_COMBOBOX){
            /* A selection-field owner draw must fill its supplied DC now.
               Returning an empty item and queuing WM_PAINT exposes native
               background pixels during a composited viewport scroll. */
            if(v->itemState&ODS_COMBOBOXEDIT){
                fill(v->hDC,&r,a->surface);wchar_t text[1024]=L"";GetWindowTextW(v->hwndItem,text,1024);r.left+=px(a,7);r.right-=px(a,5);label(a,v->hDC,r,text,disabled?a->muted:a->text,DT_LEFT|DT_NOPREFIX);*result=TRUE;return true;
            }
            bool selected=(v->itemState&ODS_SELECTED)!=0&&!(v->itemState&ODS_COMBOBOXEDIT);fill(v->hDC,&r,selected?a->accent:a->surface);wchar_t text[1024]=L"";
            if(v->itemID!=(UINT)-1&&SendMessageW(v->hwndItem,CB_GETLBTEXTLEN,v->itemID,0)<1024)SendMessageW(v->hwndItem,CB_GETLBTEXT,v->itemID,(LPARAM)text);
            r.left+=px(a,7);r.right-=px(a,5);label(a,v->hDC,r,text,disabled?a->muted:selected?GetSysColor(COLOR_HIGHLIGHTTEXT):a->text,DT_LEFT|DT_NOPREFIX);*result=TRUE;return true;
        }
    }
    return false;
}
