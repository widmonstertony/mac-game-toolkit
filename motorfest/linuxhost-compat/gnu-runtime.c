/* SPDX-License-Identifier: AGPL-3.0-only
 * Opt-in loading of the exact GNU DSOs validated by the isolated C++ probe.
 * Used by the actual host dlopen path, not just the inspection driver. */
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elfload.h"
#include "glibc.h"
#include "process.h"
#include "syscall.h"

static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
static int ready;
static char failure[256];
static struct elf_module *verified_modules[2];
static const char *const names[]={"libgcc_s.so.1","libstdc++.so.6.0.21"};
static const char *const hashes[]={
    "753a1cb28fe20a9a0fb75fe6ee2b9b2a9a36041d4ec41817da9bc9a614c4ab00",
    "83846996211e93eaa5f945f631aaa2f7dd4ae98bbd967fac78964668118887ed"};

void *linuxhost_gnu_symbol(const char *name)
{
    for(unsigned i=0;i<2;i++)
    {
        struct elf_module *m=verified_modules[i];
        void *p=m ? elf_symbol(m,name) : NULL;
        if(p)return p;
    }
    return NULL;
}

/* Hash and load the same bytes, preventing a validation/load replacement race. */
static unsigned char *read_verified(const char *path,const char *expected,size_t *size)
{
    WCHAR *wide=linuxhost_unix_path(path);
    HANDLE file=wide ? CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL) : INVALID_HANDLE_VALUE;
    free(wide);
    if(file==INVALID_HANDLE_VALUE)return NULL;
    LARGE_INTEGER length; unsigned char *data=NULL;
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    unsigned char digest[32];char hex[65];int success=0;
    if(!GetFileSizeEx(file,&length) || length.QuadPart<=0 || length.QuadPart>32*1024*1024)goto done;
    *size=(size_t)length.QuadPart; data=malloc(*size);
    if(!data)goto done;
    size_t pos=0;
    while(pos<*size)
    {
        DWORD count;
        if(!ReadFile(file,data+pos,(DWORD)(*size-pos),&count,NULL) || !count)goto done;
        pos+=count;
    }
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0 ||
       BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)<0 ||
       BCryptHashData(hash,data,(ULONG)*size,0)<0 ||
       BCryptFinishHash(hash,digest,sizeof(digest),0)<0)goto done;
    for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    success=!strcmp(hex,expected);
done:
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    if(!success){free(data);return NULL;}
    return data;
}

static BOOL CALLBACK initialize(PINIT_ONCE control,void *parameter,void **context)
{
    char directory[2048],path[2304];
    DWORD length=GetEnvironmentVariableA("LINUXHOST_GNU_RUNTIME",directory,sizeof(directory));
    if(!length || length>=sizeof(directory))
    { snprintf(failure,sizeof(failure),"GNU runtime path missing or too long");return TRUE; }
    for(unsigned i=0;i<2;i++)
    {
        snprintf(path,sizeof(path),"%s/%s",directory,names[i]);
        size_t size=0;unsigned char *data=read_verified(path,hashes[i],&size);
        if(!data){snprintf(failure,sizeof(failure),"GNU runtime verification failed: %s",names[i]);return TRUE;}
        struct elf_module *module=elf_load_buffer(path,data,size,linuxhost_resolve,NULL,failure,sizeof(failure));
        free(data);
        if(!module)return TRUE;
        const struct elf_stats *s=elf_get_stats(module);
        if(s->unresolved || s->unsupported)
        { snprintf(failure,sizeof(failure),"GNU runtime imports incomplete: %s",names[i]);return TRUE; }
        verified_modules[i]=module;
    }
    for(unsigned i=0;i<2;i++)
        if(!linuxhost_register_syscalls(verified_modules[i]))
        { snprintf(failure,sizeof(failure),"GNU runtime syscall routing unavailable");return TRUE; }
    for(unsigned i=0;i<2;i++)elf_run_constructors(verified_modules[i]);
    ready=1;return TRUE;
}

int linuxhost_gnu_prepare(char *error,size_t size)
{
    /* No environment override: preserve the ordinary non-C++ host path. */
    if(!GetEnvironmentVariableA("LINUXHOST_GNU_RUNTIME",NULL,0))return 1;
    if(!InitOnceExecuteOnce(&once,initialize,NULL,NULL) || !ready)
    { snprintf(error,size,"%s",failure[0] ? failure : "GNU runtime initialization failed");return 0; }
    return 1;
}
