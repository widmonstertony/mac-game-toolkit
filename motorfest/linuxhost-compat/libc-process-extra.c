/* SPDX-License-Identifier: AGPL-3.0-only
 * Explicit host-shell route. This is macOS /bin/sh, NOT a Linux /proc namespace.
 * Disabled unless LINUXHOST_NATIVE_HELPER names the separately built supervisor.
 */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libc.h"
#define SYSV __attribute__((sysv_abi))
typedef long (WINAPI *spawn_t)(char *const *,int);
static int fail(int error){*linuxhost_errno()=error;return -1;}
static SYSV int lx_system_extra(const char *command)
{
    char helper[32768],hex[33];
    WCHAR winpath[MAX_PATH],temp[MAX_PATH],cwd[32768];
    char *nativecwd=NULL,*unixpath=NULL;
    char *(__cdecl *unix_name)(const WCHAR*)=(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"),"wine_get_unix_file_name");
    spawn_t spawn=(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"),"__wine_unix_spawnvp");
    DWORD n=GetEnvironmentVariableA("LINUXHOST_NATIVE_HELPER",helper,sizeof(helper));
    if(!spawn || !unix_name || !n || n>=sizeof(helper) || helper[0]!='/')return command ? fail(38):0;
    int query=command==NULL;
    if(query)command="exit 0";
    n=GetCurrentDirectoryW(sizeof(cwd)/sizeof(*cwd),cwd);
    if(!n || n>=sizeof(cwd)/sizeof(*cwd))return query ? 0:fail(5);
    if(!GetTempPathW(MAX_PATH,temp))return query ? 0:fail(5);
    unsigned char nonce[16];
    if(BCryptGenRandom(NULL,nonce,sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG))return query ? 0:fail(5);
    for(int i=0;i<16;i++)sprintf(hex+2*i,"%02x",nonce[i]);
    if(swprintf(winpath,MAX_PATH,L"%lslinuxhost-child-%hs.bin",temp,hex)<0)return query ? 0:fail(36);
    HANDLE file=CreateFileW(winpath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
    if(file==INVALID_HANDLE_VALUE)return query ? 0:fail(5);
    nativecwd=unix_name(cwd);unixpath=unix_name(winpath);
    uint32_t reply[4]={0};DWORD bytes=0;
    int result=-1,error=5;
    if(nativecwd && unixpath && WriteFile(file,reply,sizeof(reply),&bytes,NULL)&&bytes==sizeof(reply))
    {
        char *args[]={helper,unixpath,nativecwd,(char*)command,NULL};
        long rc=spawn(args,1);
        if(!rc && SetFilePointer(file,0,NULL,FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
            ReadFile(file,reply,sizeof(reply),&bytes,NULL)&&bytes==sizeof(reply)&&
            reply[0]==0x4c485350 && reply[1]==1)
        {result=(int)reply[2];error=(int)reply[3];}
    }
    if(nativecwd)HeapFree(GetProcessHeap(),0,nativecwd);
    if(unixpath)HeapFree(GetProcessHeap(),0,unixpath);
    CloseHandle(file);
    if(query)return !error && !result;
    if(error)return fail(error);
    return result;
}
void *linuxhost_process_symbol(const char *name)
{
    char enabled[32768];
    DWORD n=GetEnvironmentVariableA("LINUXHOST_NATIVE_HELPER",enabled,sizeof(enabled));
    if(n && n<sizeof(enabled) && enabled[0]=='/' && !strcmp(name,"system"))return lx_system_extra;
    return NULL;
}
