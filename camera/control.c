#include "../src/platform.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <sddl.h>
#include "camera_ids.h"
static bool elevated(void){BOOL yes=FALSE;SID_IDENTIFIER_AUTHORITY a=SECURITY_NT_AUTHORITY;PSID sid=NULL;if(AllocateAndInitializeSid(&a,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){CheckTokenMembership(NULL,sid,&yes);FreeSid(sid);}return yes!=FALSE;}
int wmain(int argc,wchar_t **argv){
    if(argc!=2){fwprintf(stderr,L"Usage: awm-camera-control install-source | register-user | remove-user\n");return 2;}
    HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(hr))return 3;hr=MFStartup(MF_VERSION,MFSTARTUP_FULL);if(FAILED(hr))return 3;
    if(!wcscmp(argv[1],L"install-source")){
        if(!elevated()){fprintf(stderr,"Install requires elevation; normal operation does not.\n");hr=E_ACCESSDENIED;}
        else{
            wchar_t logs[AWM_PATH];GetEnvironmentVariableW(L"ProgramData",logs,AWM_PATH);PathAppendW(logs,L"AndroidWebcamManager");PSECURITY_DESCRIPTOR sd=NULL;
            if(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GA;;;LS)(A;OICI;GRGW;;;IU)",SDDL_REVISION_1,&sd,NULL)){SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};CreateDirectoryW(logs,&sa);LocalFree(sd);}
            wchar_t from[AWM_PATH],to[AWM_PATH];PWSTR pf=NULL;
            GetModuleFileNameW(NULL,from,AWM_PATH);PathRemoveFileSpecW(from);PathAppendW(from,L"awm-camera-source.dll");
            hr=SHGetKnownFolderPath(&FOLDERID_ProgramFiles,0,NULL,&pf);
            if(SUCCEEDED(hr)){swprintf(to,AWM_PATH,L"%s\\AndroidWebcamManager",pf);CoTaskMemFree(pf);if(!CreateDirectoryW(to,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)hr=HRESULT_FROM_WIN32(GetLastError());
                wchar_t filename[128];swprintf(filename,128,L"awm-camera-source-%llu.dll",(unsigned long long)GetTickCount64());PathAppendW(to,filename);if(SUCCEEDED(hr)&&!CopyFileW(from,to,TRUE))hr=HRESULT_FROM_WIN32(GetLastError());
                if(SUCCEEDED(hr)){HMODULE dll=LoadLibraryExW(to,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll)hr=HRESULT_FROM_WIN32(GetLastError());else{HRESULT (WINAPI *reg)(void)=(void *)GetProcAddress(dll,"DllRegisterServer");hr=reg?reg():E_NOINTERFACE;FreeLibrary(dll);}}
            }
        }
    }else if(!wcscmp(argv[1],L"register-user")||!wcscmp(argv[1],L"remove-user")){
        IMFVirtualCamera *camera=NULL;hr=MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,MFVirtualCameraLifetime_System,MFVirtualCameraAccess_CurrentUser,AWM_CAMERA_FRIENDLY,AWM_CAMERA_CLSID,NULL,0,&camera);
        printf("MFCreateVirtualCamera: 0x%08lX\n",(unsigned long)hr);
        if(SUCCEEDED(hr))hr=!wcscmp(argv[1],L"remove-user")?IMFVirtualCamera_Remove(camera):IMFVirtualCamera_Start(camera,NULL);
        if(camera){IMFVirtualCamera_Shutdown(camera);IMFVirtualCamera_Release(camera);}
    }else hr=E_INVALIDARG;
    printf("Camera operation: HRESULT 0x%08lX\n",(unsigned long)hr);MFShutdown();CoUninitialize();return FAILED(hr)?1:0;
}
