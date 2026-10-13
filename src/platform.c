#include "platform.h"
#include <ws2tcpip.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <mfapi.h>
#include <mfidl.h>
#include <dshow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool platform_init(void){WSADATA w;return WSAStartup(MAKEWORD(2,2),&w)==0;}
void platform_shutdown(void){WSACleanup();}
void utf8_to_wide(const char *s,wchar_t *o,size_t cap){if(!cap)return;if(!MultiByteToWideChar(CP_UTF8,0,s,-1,o,(int)cap))o[0]=0;}
void wide_to_utf8(const wchar_t *s,char *o,size_t cap){if(!cap)return;if(!WideCharToMultiByte(CP_UTF8,0,s,-1,o,(int)cap,NULL,NULL))o[0]=0;}
void win_error(DWORD code,char *out,size_t cap){wchar_t w[512];if(!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,code,0,w,512,NULL))swprintf(w,512,L"Windows error %lu",code);wide_to_utf8(w,out,cap);}
bool path_is_file(const wchar_t *p){DWORD a=GetFileAttributesW(p);return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
void config_defaults(Configuration *c){memset(c,0,sizeof(*c));c->magic=0x314D5741;c->version=1;c->obs_port=4455;}
static bool search_exe(const wchar_t *name,wchar_t *out){DWORD n=SearchPathW(NULL,name,NULL,AWM_PATH,out,NULL);return n>0&&n<AWM_PATH&&path_is_file(out);}
void detect_dependencies(Configuration *c){
    wchar_t base[AWM_PATH],dir[AWM_PATH];GetModuleFileNameW(NULL,base,AWM_PATH);PathRemoveFileSpecW(base);
    if(!path_is_file(c->scrcpy)) {
        if(!search_exe(L"scrcpy.exe",c->scrcpy)) {
            /* Search only managed distributions beside this executable and its project parent. */
            for(int parent=0;parent<2&&!path_is_file(c->scrcpy);parent++){
                awm_wcopy(dir,AWM_PATH,base);PathAppendW(dir,L"tools\\scrcpy-win64-*");WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(dir,&fd);
                if(h!=INVALID_HANDLE_VALUE){do{if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){wchar_t candidate[AWM_PATH];swprintf(candidate,AWM_PATH,L"%s\\tools\\%s\\scrcpy.exe",base,fd.cFileName);if(path_is_file(candidate)){awm_wcopy(c->scrcpy,AWM_PATH,candidate);break;}}}while(FindNextFileW(h,&fd));FindClose(h);}PathRemoveFileSpecW(base);
            }
        }
    }
    if(!path_is_file(c->adb)) {
        awm_wcopy(dir,AWM_PATH,c->scrcpy);PathRemoveFileSpecW(dir);PathAppendW(dir,L"adb.exe");
        if(path_is_file(dir))awm_wcopy(c->adb,AWM_PATH,dir);else search_exe(L"adb.exe",c->adb);
    }
#ifndef AWM_NATIVE_ONLY
    if(!path_is_file(c->obs)) {
        if(!search_exe(L"obs64.exe",c->obs)) {
            GetEnvironmentVariableW(L"ProgramFiles",dir,AWM_PATH);PathAppendW(dir,L"obs-studio\\bin\\64bit\\obs64.exe");if(path_is_file(dir))awm_wcopy(c->obs,AWM_PATH,dir);
        }
    }
#endif
}
bool config_path(wchar_t *out,size_t cap){PWSTR local=NULL;HRESULT hr=SHGetKnownFolderPath(&FOLDERID_LocalAppData,0,NULL,&local);if(FAILED(hr))return false;
    /* Retain the original directory so the WinDroid Capture rename preserves
       existing encrypted settings and per-device profiles. */
    int n=swprintf(out,cap,L"%s\\AndroidWebcamManager",local);CoTaskMemFree(local);if(n<0)return false;
    if(!CreateDirectoryW(out,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    return wcslen(out)+14<cap&&PathAppendW(out,L"settings.dpapi");
}
bool config_save(const wchar_t *path,const Configuration *c,char *err,size_t cap){
    DATA_BLOB in={(DWORD)sizeof(*c),(BYTE *)c},encrypted={0};
    if(!CryptProtectData(&in,L"WinDroid Capture settings",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&encrypted)){win_error(GetLastError(),err,cap);return false;}
    wchar_t tmp[AWM_PATH];if(swprintf(tmp,AWM_PATH,L"%s.tmp",path)<0){LocalFree(encrypted.pbData);return false;}
    HANDLE f=CreateFileW(tmp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);DWORD written=0;
    bool ok=f!=INVALID_HANDLE_VALUE&&WriteFile(f,encrypted.pbData,encrypted.cbData,&written,NULL)&&written==encrypted.cbData;
    if(f!=INVALID_HANDLE_VALUE){FlushFileBuffers(f);CloseHandle(f);}LocalFree(encrypted.pbData);
    if(ok)ok=MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok){win_error(GetLastError(),err,cap);DeleteFileW(tmp);}return ok;
}
bool config_load(const wchar_t *path,Configuration *c,char *err,size_t cap){
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE){awm_copy(err,cap,"Settings not found; using defaults.");return false;}
    DWORD size=GetFileSize(f,NULL);bool ok=false;BYTE *buf=NULL;
    if(size>0&&size<1024*1024){buf=malloc(size);DWORD n;ok=buf&&ReadFile(f,buf,size,&n,NULL)&&n==size;}CloseHandle(f);
    if(ok){DATA_BLOB in={size,buf},decoded={0};ok=CryptUnprotectData(&in,NULL,NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&decoded)!=0;
        if(ok){ok=decoded.cbData==sizeof(*c);if(ok){Configuration *v=(Configuration *)decoded.pbData;ok=v->magic==0x314D5741&&v->version==1&&v->profile_count>=0&&v->profile_count<=AWM_PROFILES&&v->obs_port>0&&v->obs_port<=65535;
                if(ok){memcpy(c,v,sizeof(*c));c->adb[AWM_PATH-1]=c->scrcpy[AWM_PATH-1]=c->obs[AWM_PATH-1]=0;c->obs_password[255]=0;c->playback_id[1023]=c->recording_id[1023]=0;for(int i=0;i<c->profile_count;i++){SavedProfile *p=&c->profiles[i];p->serial[255]=0;p->profile.camera_id[79]=0;p->profile.camera_facing[31]=0;p->profile.codec[31]=0;p->profile.encoder[255]=0;p->profile.audio_source[63]=0;p->profile.camera_ar[31]=0;}}}
            SecureZeroMemory(decoded.pbData,decoded.cbData);LocalFree(decoded.pbData);}}
    free(buf);if(!ok)awm_copy(err,cap,"Settings are corrupt, incompatible, or protected for another Windows user; using defaults.");return ok;
}
bool config_profile_get(const Configuration *c,const char *id,Profile *p){for(int i=0;i<c->profile_count;i++)if(!strcmp(c->profiles[i].serial,id)){*p=c->profiles[i].profile;return true;}return false;}
void config_profile_set(Configuration *c,const char *id,const Profile *p){int i;for(i=0;i<c->profile_count;i++)if(!strcmp(c->profiles[i].serial,id))break;if(i==AWM_PROFILES)return;if(i==c->profile_count)c->profile_count++;awm_copy(c->profiles[i].serial,sizeof(c->profiles[i].serial),id);c->profiles[i].profile=*p;}
static int env_cmp(const void *a,const void *b){return _wcsicmp(*(const wchar_t * const *)a,*(const wchar_t * const *)b);}
static wchar_t *child_environment(const wchar_t *adb,const wchar_t *server){
    wchar_t *raw=GetEnvironmentStringsW();if(!raw)return NULL;size_t total=0;int count=0;
    for(wchar_t *p=raw;*p;p+=wcslen(p)+1){total+=wcslen(p)+1;count++;}
    wchar_t *buf=calloc(total+2*AWM_PATH+40,sizeof(wchar_t));wchar_t **entries=calloc((size_t)count+3,sizeof(*entries));
    if(!buf||!entries){free(buf);free(entries);FreeEnvironmentStringsW(raw);return NULL;}
    size_t n=0;int k=0;
    for(wchar_t *p=raw;*p;p+=wcslen(p)+1){if(!_wcsnicmp(p,L"ADB_TRACE=",10)|| (adb&& !_wcsnicmp(p,L"ADB=",4))||(server&&!_wcsnicmp(p,L"SCRCPY_SERVER_PATH=",19)))continue;size_t l=wcslen(p)+1;memcpy(buf+n,p,l*sizeof(wchar_t));entries[k++]=buf+n;n+=l;}
    if(adb){entries[k++]=buf+n;int l=swprintf(buf+n,AWM_PATH+5,L"ADB=%s",adb);if(l<0){free(buf);free(entries);FreeEnvironmentStringsW(raw);return NULL;}n+=(size_t)l+1;}
    if(server){entries[k++]=buf+n;int l=swprintf(buf+n,AWM_PATH+24,L"SCRCPY_SERVER_PATH=%s",server);if(l<0){free(buf);free(entries);FreeEnvironmentStringsW(raw);return NULL;}n+=(size_t)l+1;}
    qsort(entries,(size_t)k,sizeof(*entries),env_cmp);wchar_t *sorted=calloc(n+2,sizeof(wchar_t));
    if(sorted){size_t pos=0;for(int i=0;i<k;i++){size_t l=wcslen(entries[i])+1;memcpy(sorted+pos,entries[i],l*sizeof(wchar_t));pos+=l;}}
    free(entries);free(buf);FreeEnvironmentStringsW(raw);return sorted;
}
bool process_start(Process *p,const Arguments *a,const wchar_t *adb,const char *input,bool capture,char *err,size_t cap){
    memset(p,0,sizeof(*p));wchar_t cmd[32768];if(!a->count||!path_is_file(a->values[0])||!quote_windows_args(a,cmd,32768)){awm_copy(err,cap,"Executable missing or command too long.");return false;}
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE read=NULL,write=NULL,inread=NULL,inwrite=NULL;
    if(!CreatePipe(&read,&write,&sa,0)||!SetHandleInformation(read,HANDLE_FLAG_INHERIT,0)||!CreatePipe(&inread,&inwrite,&sa,0)||!SetHandleInformation(inwrite,HANDLE_FLAG_INHERIT,0)){win_error(GetLastError(),err,cap);goto failed;}
    STARTUPINFOEXW si={0};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdOutput=write;si.StartupInfo.hStdError=write;si.StartupInfo.hStdInput=inread;
    SIZE_T size=0;InitializeProcThreadAttributeList(NULL,1,0,&size);si.lpAttributeList=malloc(size);
    if(!si.lpAttributeList){awm_copy(err,cap,"Out of memory.");goto failed;}
    if(!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&size)){free(si.lpAttributeList);goto failed;}
    HANDLE inherited[]={write,inread};bool attr=UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),NULL,NULL)!=0;
    wchar_t server[AWM_PATH]=L"";const wchar_t *owned_server=NULL;
    if(!_wcsicmp(PathFindFileNameW(a->values[0]),L"scrcpy-awm.exe")){awm_wcopy(server,AWM_PATH,a->values[0]);PathRemoveFileSpecW(server);PathAppendW(server,L"scrcpy-server-awm");if(path_is_file(server))owned_server=server;}
    wchar_t *env=child_environment(adb,owned_server);PROCESS_INFORMATION pi={0};
    wchar_t working[AWM_PATH];awm_wcopy(working,AWM_PATH,a->values[0]);PathRemoveFileSpecW(working);
    DWORD flags=CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED;
    bool ok=attr&&env&&CreateProcessW(a->values[0],cmd,NULL,NULL,TRUE,flags,env,working,&si.StartupInfo,&pi);
    DWORD start_error=GetLastError();free(env);DeleteProcThreadAttributeList(si.lpAttributeList);free(si.lpAttributeList);
    if(!ok){win_error(start_error,err,cap);goto failed;}
    p->process=pi.hProcess;p->pid=pi.dwProcessId;p->pipe=read;read=NULL;
    if(capture){p->job=CreateJobObjectW(NULL,NULL);JOBOBJECT_EXTENDED_LIMIT_INFORMATION info={0};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
        if(!p->job||!SetInformationJobObject(p->job,JobObjectExtendedLimitInformation,&info,sizeof(info))||!AssignProcessToJobObject(p->job,p->process)){if(p->job)CloseHandle(p->job);p->job=NULL;}}
    ResumeThread(pi.hThread);CloseHandle(pi.hThread);CloseHandle(write);write=NULL;CloseHandle(inread);inread=NULL;
    if(input){DWORD n;WriteFile(inwrite,input,(DWORD)strlen(input),&n,NULL);}CloseHandle(inwrite);return true;
failed:
    if(read)CloseHandle(read);if(write)CloseHandle(write);if(inread)CloseHandle(inread);if(inwrite)CloseHandle(inwrite);return false;
}
size_t process_read(Process *p,char *out,size_t cap){if(!cap)return 0;out[0]=0;DWORD avail=0,n=0;if(!p->pipe||!PeekNamedPipe(p->pipe,NULL,0,NULL,&avail,NULL)||!avail)return 0;
    DWORD request=avail<(DWORD)(cap-1)?avail:(DWORD)(cap-1);if(!ReadFile(p->pipe,out,request,&n,NULL))return 0;out[n]=0;return n;
}
bool process_alive(Process *p){if(!p->process||p->exited)return false;if(WaitForSingleObject(p->process,0)==WAIT_TIMEOUT)return true;p->exited=true;GetExitCodeProcess(p->process,&p->exit_code);return false;}
static BOOL CALLBACK close_owned_window(HWND w,LPARAM param){DWORD pid;GetWindowThreadProcessId(w,&pid);if(pid==(DWORD)param)PostMessageW(w,WM_CLOSE,0,0);return TRUE;}
void process_stop(Process *p){if(p->process&&process_alive(p)){EnumWindows(close_owned_window,(LPARAM)p->pid);if(WaitForSingleObject(p->process,400)==WAIT_TIMEOUT)TerminateProcess(p->process,ERROR_CANCELLED);WaitForSingleObject(p->process,800);process_alive(p);}}
void process_close(Process *p){process_stop(p);if(p->pipe)CloseHandle(p->pipe);if(p->job)CloseHandle(p->job);if(p->process)CloseHandle(p->process);memset(p,0,sizeof(*p));}
bool process_run(const Arguments *a,const wchar_t *adb,const char *input,HANDLE cancel,DWORD timeout,char *out,size_t cap,DWORD *code,char *err,size_t ec){
    Process p;if(!process_start(&p,a,adb,input,false,err,ec))return false;size_t n=0;out[0]=0;ULONGLONG deadline=GetTickCount64()+timeout;bool ok=true;bool truncated=false;
    for(;;){char b[4096];size_t got;int reads=0;while(reads++<16&&(got=process_read(&p,b,sizeof(b)))>0){if(n+got<cap){memcpy(out+n,b,got);n+=got;out[n]=0;}else truncated=true;}
        if(!process_alive(&p)){
            /* A short-lived child may write and exit between the preceding read and wait. */
            while((got=process_read(&p,b,sizeof(b)))>0){if(n+got<cap){memcpy(out+n,b,got);n+=got;out[n]=0;}else truncated=true;}
            break;
        }
        if((cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)||GetTickCount64()>=deadline){awm_copy(err,ec,"Operation cancelled or timed out.");ok=false;break;}
        if(cancel)WaitForSingleObject(cancel,30);else Sleep(30);
    }
    if(code)*code=p.exit_code;if(truncated){awm_copy(err,ec,"Subprocess output exceeded the bounded buffer.");ok=false;}process_close(&p);return ok;
}
bool adb_server_compatible(int expected,char *err,size_t cap){
    wchar_t socket_override[256];if(GetEnvironmentVariableW(L"ADB_SERVER_SOCKET",socket_override,256)||GetEnvironmentVariableW(L"ANDROID_ADB_SERVER_PORT",socket_override,256)){awm_copy(err,cap,"Nonstandard ADB server environment detected. This version supports the local shared server on port 5037; clear that override explicitly.");return false;}
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET){awm_copy(err,cap,"Cannot inspect ADB server.");return false;}
    DWORD ms=750;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char *)&ms,sizeof(ms));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char *)&ms,sizeof(ms));
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(5037);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(s,(struct sockaddr *)&a,sizeof(a))==SOCKET_ERROR){int e=WSAGetLastError();closesocket(s);if(e==WSAECONNREFUSED)return true;awm_copy(err,cap,"Cannot inspect the shared ADB server safely.");return false;}
    const char request[]="000chost:version";int sent=send(s,request,sizeof(request)-1,0);char reply[64]={0};int n=0;
    while(sent==(int)sizeof(request)-1&&n<12){int r=recv(s,reply+n,12-n,0);if(r<=0)break;n+=r;}closesocket(s);
    if(n!=12||memcmp(reply,"OKAY0004",8)){awm_copy(err,cap,"Shared ADB server did not report a valid protocol version. No automatic restart was attempted.");return false;}
    char *end;long version=strtol(reply+8,&end,16);if(end!=reply+12||version!=expected){snprintf(err,cap,"ADB protocol conflict: selected client=%d, shared server=%ld. Discovery is blocked to avoid ADB's automatic server restart. Use the explicit repair action if you accept interrupting other ADB users.",expected,version);return false;}return true;
}
bool file_version(const wchar_t *path,char *out,size_t cap){DWORD dummy=0,size=GetFileVersionInfoSizeW(path,&dummy);if(!size)return false;BYTE *data=malloc(size);bool ok=false;if(data&&GetFileVersionInfoW(path,0,size,data)){VS_FIXEDFILEINFO *v=NULL;UINT n=0;if(VerQueryValueW(data,L"\\",(void **)&v,&n)&&n>=sizeof(*v)){snprintf(out,cap,"%u.%u.%u.%u",HIWORD(v->dwFileVersionMS),LOWORD(v->dwFileVersionMS),HIWORD(v->dwFileVersionLS),LOWORD(v->dwFileVersionLS));ok=true;}}free(data);return ok;}
bool webcam_device_names(char *out,size_t cap){
    out[0]=0;HRESULT init=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return false;
    bool ok=false;IMFAttributes *attr=NULL;IMFActivate **devices=NULL;UINT32 count=0;
    if(SUCCEEDED(MFStartup(MF_VERSION,MFSTARTUP_LITE))){if(SUCCEEDED(MFCreateAttributes(&attr,1))&&SUCCEEDED(IMFAttributes_SetGUID(attr,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))&&SUCCEEDED(MFEnumDeviceSources(attr,&devices,&count))){
            ok=true;for(UINT32 i=0;i<count;i++){wchar_t *name=NULL;UINT32 len;if(SUCCEEDED(IMFActivate_GetAllocatedString(devices[i],&MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&len))){char text[1024];wide_to_utf8(name,text,sizeof(text));size_t n=strlen(out);if(n+strlen(text)+3<cap)snprintf(out+n,cap-n,"%s\r\n",text);CoTaskMemFree(name);}IMFActivate_Release(devices[i]);}CoTaskMemFree(devices);
        }if(attr)IMFAttributes_Release(attr);MFShutdown();}
    /* OBS's Windows Virtual Camera is also exposed through DirectShow. */
    if(!out[0]){ICreateDevEnum *de=NULL;IEnumMoniker *list=NULL;
        if(SUCCEEDED(CoCreateInstance(&CLSID_SystemDeviceEnum,NULL,CLSCTX_INPROC_SERVER,&IID_ICreateDevEnum,(void **)&de))&&ICreateDevEnum_CreateClassEnumerator(de,&CLSID_VideoInputDeviceCategory,&list,0)==S_OK){IMoniker *moniker=NULL;
            while(IEnumMoniker_Next(list,1,&moniker,NULL)==S_OK){IPropertyBag *bag=NULL;if(SUCCEEDED(IMoniker_BindToStorage(moniker,NULL,NULL,&IID_IPropertyBag,(void **)&bag))){VARIANT name;VariantInit(&name);if(SUCCEEDED(IPropertyBag_Read(bag,L"FriendlyName",&name,NULL))&&name.vt==VT_BSTR){char text[1024];wide_to_utf8(name.bstrVal,text,sizeof(text));size_t n=strlen(out);if(n+strlen(text)+3<cap)snprintf(out+n,cap-n,"%s\r\n",text);ok=true;}VariantClear(&name);IPropertyBag_Release(bag);}IMoniker_Release(moniker);}IEnumMoniker_Release(list);}if(de)ICreateDevEnum_Release(de);}
    if(!out[0])awm_copy(out,cap,"No video devices were reported. Browser/receiving-app enumeration is still required.\r\n");
    if(SUCCEEDED(init))CoUninitialize();return ok;
}
