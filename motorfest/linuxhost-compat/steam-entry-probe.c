/* SPDX-License-Identifier: AGPL-3.0-only
 * Read-only connection test: never calls RestartAppIfNecessary, login, download,
 * achievement, ownership override, or any game entry point. */
#include <windows.h>
#include <stdio.h>
static int report_success(void)
{
    char path[32768], nonce[65], payload[160];
    DWORD n=GetEnvironmentVariableA("MOTORFEST_PROBE_RESULT",path,sizeof(path));
    if(!n || n>=sizeof(path))return 9;
    n=GetEnvironmentVariableA("MOTORFEST_PROBE_NONCE",nonce,sizeof(nonce));
    if(n!=32)return 9;
    for(DWORD i=0;i<n;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return 9;
    int size=snprintf(payload,sizeof(payload),"STEAM_API_INIT_AND_SHUTDOWN_OK %s\n",nonce);
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return 10;
    DWORD written=0;
    BOOL ok=WriteFile(file,payload,(DWORD)size,&written,NULL);
    if(ok)ok=FlushFileBuffers(file);
    CloseHandle(file);
    return ok && written==(DWORD)size ? 0 : 11;
}
int main(int argc,char **argv)
{
    if(argc!=2 && argc!=3)return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc==3) /* Only the runner's isolated prefix; no login/ownership changes. */
    {
        const char *key="Software\\Valve\\Steam\\ActiveProcess";
        DWORD pid=GetCurrentProcessId();
        if(RegSetKeyValueA(HKEY_CURRENT_USER,key,"pid",REG_DWORD,&pid,sizeof(pid)) ||
           RegSetKeyValueA(HKEY_CURRENT_USER,key,sizeof(void*)==4?"SteamClientDll":"SteamClientDll64",REG_SZ,argv[2],(DWORD)strlen(argv[2])+1))return 6;
        HMODULE client=LoadLibraryA(argv[2]);
        if(!client){printf("STEAM_CLIENT_LOAD_FAILED error=%lu\n",GetLastError());return 7;}
        void *create=GetProcAddress(client,"CreateInterface");
        if(!create){printf("STEAM_CLIENT_FORWARD_FAILED error=%lu\n",GetLastError());return 8;}
        puts("STEAM_CLIENT_FORWARD_OK");
    }
    HMODULE api=LoadLibraryA(argv[1]);
    if(!api){printf("STEAM_API_LOAD_FAILED error=%lu\n",GetLastError());return 3;}
    unsigned char (*init)(void)=(void*)GetProcAddress(api,"SteamAPI_Init");
    if(!init)init=(void*)GetProcAddress(api,"SteamAPI_InitSafe");
    void (*shutdown)(void)=(void*)GetProcAddress(api,"SteamAPI_Shutdown");
    if(!init || !shutdown){puts("STEAM_API_EXPORTS_MISSING");return 4;}
    if(!init()){puts("STEAM_API_INIT_FAILED");return 5;}
    puts("STEAM_API_INIT_OK");shutdown();return report_success();
}
