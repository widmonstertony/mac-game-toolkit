/* SPDX-License-Identifier: AGPL-3.0-only
 * Read-only connection test: never calls RestartAppIfNecessary, login, download,
 * achievement, ownership override, or any game entry point. */
#include <windows.h>
#include <stdio.h>
int main(int argc,char **argv)
{
    if(argc!=2 && argc!=3)return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc==3) /* Only the runner's isolated prefix; no login/ownership changes. */
    {
        const char *key="Software\\Valve\\Steam\\ActiveProcess";
        DWORD pid=GetCurrentProcessId();
        if(RegSetKeyValueA(HKEY_CURRENT_USER,key,"pid",REG_DWORD,&pid,sizeof(pid)) ||
           RegSetKeyValueA(HKEY_CURRENT_USER,key,"SteamClientDll64",REG_SZ,argv[2],(DWORD)strlen(argv[2])+1))return 6;
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
    puts("STEAM_API_INIT_OK");shutdown();return 0;
}
