/* SPDX-License-Identifier: AGPL-3.0-only
 * Load an unmodified ELF through the actual provider DLL. Constructors run;
 * no vendor entry point is called, patched or replaced. Not a game session. */
#include <windows.h>
#include <stdio.h>
#define SYSV __attribute__((sysv_abi))
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE host=LoadLibraryA(argv[1]);
    if(!host){printf("HOST_LOAD_FAILED %lu\n",GetLastError());return 3;}
    void *(*resolve)(const char*,void*)=(void*)GetProcAddress(host,"linuxhost_resolve");
    if(!resolve)return 4;
    void *(SYSV *open)(const char*,int)=resolve("dlopen",NULL);
    char *(SYSV *error)(void)=resolve("dlerror",NULL);
    if(!open || !error)return 5;
    puts("CLIENT_DLOPEN_BEGIN");
    if(!open(argv[2],2)){printf("CLIENT_DLOPEN_FAILED %s\n",error());return 6;}
    puts("CLIENT_DLOPEN_RETURNED");
    return 0;
}
