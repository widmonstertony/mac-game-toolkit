/* SPDX-License-Identifier: AGPL-3.0-only
 * No host sources linked into this executable: load the actual candidate DLL. */
#include <windows.h>
#include <stdio.h>
#define SYSV __attribute__((sysv_abi))
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE dll=LoadLibraryA(argv[1]);
    if(!dll){printf("DLL_LOAD_FAILED error=%lu\n",GetLastError());return 3;}
    void *(*resolve)(const char*,void*)=(void*)GetProcAddress(dll,"linuxhost_resolve");
    if(!resolve)return 4;
    void *(SYSV *open_fn)(const char*,int)=resolve("dlopen",NULL);
    void *(SYSV *symbol_fn)(void*,const char*)=resolve("dlsym",NULL);
    char *(SYSV *error_fn)(void)=resolve("dlerror",NULL);
    void *module=open_fn(argv[2],2);
    if(!module){printf("DLL_DLOPEN_REJECT %s\n",error_fn());return 10;}
    long long (SYSV *test)(void)=symbol_fn(module,"cxx_test");
    if(!test)return 5;
    long long result=test();
    printf("DLL_CXX_%s result=%lld\n",result ? "FAIL" : "PASS",result);
    /* Keep the process-lifetime runtime loaded until process teardown. */
    return result ? 6 : 0;
}
