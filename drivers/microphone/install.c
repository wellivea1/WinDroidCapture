/* Signed-package installer. Never alters code integrity or default devices. */
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <devguid.h>
#include <shlwapi.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>
static int existing_roots(void){
    HDEVINFO list=SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA,NULL,NULL,0);if(list==INVALID_HANDLE_VALUE)return -1;
    int count=0;SP_DEVINFO_DATA dev={0};dev.cbSize=sizeof(dev);DWORD index=0;
    for(;SetupDiEnumDeviceInfo(list,index,&dev);index++){
        wchar_t ids[4096]={0};DWORD type=0,size=0;
        if(SetupDiGetDeviceRegistryPropertyW(list,&dev,SPDRP_HARDWAREID,&type,(BYTE *)ids,sizeof(ids)-2*sizeof(wchar_t),&size)&&type==REG_MULTI_SZ){
            for(const wchar_t *id=ids;*id;id+=wcslen(id)+1)if(!_wcsicmp(id,L"Root\\AWMPhoneMicrophone")){count++;break;}
        }else if(GetLastError()==ERROR_INSUFFICIENT_BUFFER){count=-1;break;}
    }
    if(count>=0&&GetLastError()!=ERROR_NO_MORE_ITEMS)count=-1;
    SetupDiDestroyDeviceInfoList(list);return count;
}
int wmain(int argc,wchar_t **argv){
    if(argc!=2){puts("Usage: awm-microphone-install <signed-package-folder>");return 2;}
    wchar_t inf[32768],cat[32768];if(!GetFullPathNameW(argv[1],32700,inf,NULL))return 2;wcscpy_s(cat,32768,inf);PathAppendW(inf,L"awm_mic.inf");PathAppendW(cat,L"awm_mic.cat");
    WINTRUST_FILE_INFO file={0};file.cbStruct=sizeof(file);file.pcwszFilePath=cat;WINTRUST_DATA data={0};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG trust=WinVerifyTrust(NULL,&policy,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(NULL,&policy,&data);
    if(trust!=ERROR_SUCCESS){printf("REFUSED: driver catalog is not trusted (0x%08lX). Supply a Microsoft-signed package. No device was created.\n",(unsigned long)trust);return 3;}
    int existing=existing_roots();if(existing<0||existing>1){puts("REFUSED: unable to identify a single existing own root device. No duplicate device was created.");return 5;}
    BOOL reboot=FALSE;if(!DiInstallDriverW(NULL,inf,0,&reboot)){printf("Windows refused driver staging: %lu\n",GetLastError());return 4;}
    if(existing==1){BOOL updated=UpdateDriverForPlugAndPlayDevicesW(NULL,L"Root\\AWMPhoneMicrophone",inf,0,&reboot);printf("Existing own device update result %d; restart requested %d. No duplicate device created.\n",updated,reboot);return updated?0:6;}
    HDEVINFO list=SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_MEDIA,NULL);SP_DEVINFO_DATA dev={0};dev.cbSize=sizeof(dev);
    if(list==INVALID_HANDLE_VALUE||!SetupDiCreateDeviceInfoW(list,L"AWMPhoneMicrophone",&GUID_DEVCLASS_MEDIA,L"Android Webcam Microphone",NULL,DICD_GENERATE_ID,&dev))return 5;
    static const wchar_t hardware[]=L"Root\\AWMPhoneMicrophone\0";BOOL ok=SetupDiSetDeviceRegistryPropertyW(list,&dev,SPDRP_HARDWAREID,(BYTE *)hardware,sizeof(hardware))&&SetupDiCallClassInstaller(DIF_REGISTERDEVICE,list,&dev);
    if(ok)ok=UpdateDriverForPlugAndPlayDevicesW(NULL,L"Root\\AWMPhoneMicrophone",inf,0,&reboot);
    if(!ok){DWORD error=GetLastError();SetupDiCallClassInstaller(DIF_REMOVE,list,&dev);printf("Installation failed: %lu. New root device removed.\n",error);}
    SetupDiDestroyDeviceInfoList(list);printf("Install result %d; restart requested %d. Check endpoint and driver health separately.\n",ok,reboot);return ok?0:6;
}
