/* SPDX-License-Identifier: AGPL-3.0-only
 * Uses the host's ordinary dlopen/dlsym entry, not a custom dependency resolver. */
#include <stdio.h>
#include <string.h>
#include "elfload.h"
#include "glibc.h"

int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    void *(SYSV *open_fn)(const char*,int)=linuxhost_resolve("dlopen",NULL);
    void *(SYSV *symbol_fn)(void*,const char*)=linuxhost_resolve("dlsym",NULL);
    char *(SYSV *error_fn)(void)=linuxhost_resolve("dlerror",NULL);
    void *module=open_fn(argv[1],2); /* RTLD_NOW */
    if(!module)
    {
        printf("DLOPEN_REJECT %s\n",error_fn());
        if(open_fn(argv[1],2)){puts("DLOPEN_BAD_CACHE_ACCEPTED");return 11;}
        printf("DLOPEN_REPEAT_REJECT %s\n",error_fn());
        return 10;
    }
    long long (SYSV *test)(void)=symbol_fn(module,"cxx_test");
    if(!test)return 3;
    long long result=test();
    printf("DLOPEN_CXX_%s result=%lld\n",result ? "FAIL" : "PASS",result);
    return result ? 4 : 0;
}
