/* SPDX-License-Identifier: AGPL-3.0-only
 * PE entry adapter for the pinned, unmodified old Wine ELF builtin DLL.
 * The original library implements the API. This only binds its Windows IAT,
 * runs its real DllMain and forwards both exports without changing arguments
 * or results. Requires explicit paths; no fallback or success substitutes.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define SYSV __attribute__((sysv_abi))
void *legacy_Init,*legacy_GetVer;
void (*legacy_thread_init)(void);
static BOOL (WINAPI *legacy_main)(HINSTANCE,DWORD,void*);
static HMODULE host;
static int bind_imports(IMAGE_NT_HEADERS64 *nt)
{
    unsigned char *base=(void*)nt;
    /* Pinned old Wine format: descriptor/name/thunk RVAs are relative to this
     * NT header, not the fake on-disk PE file. Limit all metadata to the header's
     * small data image; never write to its original code or API exports. */
    if(!nt || nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return 0;
    size_t size=nt->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress && !dir.Size)return 1;
    if(size<sizeof(*nt)||size>1024*1024||dir.VirtualAddress>=size||dir.Size>size-dir.VirtualAddress)return 0;
    IMAGE_IMPORT_DESCRIPTOR *d=(void*)(base+dir.VirtualAddress);
    for(size_t n=0;n<dir.Size/sizeof(*d);n++,d++)
    {
        if(!d->Name)return 1;
        if(d->Name>=size || !memchr(base+d->Name,0,size-d->Name))return 0;
        HMODULE dll=LoadLibraryA((char*)base+d->Name);if(!dll)return 0;
        DWORD orig=d->OriginalFirstThunk?d->OriginalFirstThunk:d->FirstThunk;
        for(size_t i=0;;i++)
        {
            size_t from=(size_t)orig+i*8,to=(size_t)d->FirstThunk+i*8;
            if(from>size-8||to>size-8)return 0;
            ULONGLONG value=*(ULONGLONG*)(base+from);if(!value)break;
            FARPROC fn;
            if(IMAGE_SNAP_BY_ORDINAL64(value))fn=GetProcAddress(dll,(const char*)(uintptr_t)IMAGE_ORDINAL64(value));
            else
            {
                if(value>size-3||!memchr(base+value+2,0,size-(size_t)value-2))return 0;
                fn=GetProcAddress(dll,(char*)base+value+2);
            }
            if(!fn)return 0;
            *(ULONGLONG*)(base+to)=(uintptr_t)fn;
        }
    }
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE self,DWORD reason,void *reserved)
{
    if(reason==DLL_PROCESS_ATTACH)
    {
        char hp[2048],path[2048];
        DWORD n=GetEnvironmentVariableA("LINUXHOST_LEGACY_HOST",hp,sizeof(hp));
        if(!n||n>=sizeof(hp))return FALSE;
        n=GetEnvironmentVariableA("LINUXHOST_LEGACY_BRIDGE",path,sizeof(path));
        if(!n||n>=sizeof(path))return FALSE;
        host=LoadLibraryA(hp);if(!host)return FALSE;
        void *(*resolve)(const char*,void*)=(void*)GetProcAddress(host,"linuxhost_resolve");
        if(!resolve)return FALSE;
        void *(SYSV *open)(const char*,int)=resolve("dlopen",NULL);
        /* These three Wine builtin exports are Win64 ABI. The host's dlsym
         * debug trampoline assumes SysV, so resolve their unwrapped addresses. */
        void *(*symbol)(void*,const char*)=(void*)GetProcAddress(host,"elf_symbol");
        legacy_thread_init=(void*)GetProcAddress(host,"linuxhost_thread_init");
        if(!open||!symbol||!legacy_thread_init)return FALSE;
        void *m=open(path,2);if(!m)return FALSE;
        legacy_main=symbol(m,"DllMain");
        legacy_Init=symbol(m,"Init");legacy_GetVer=symbol(m,"GetVer");
        if(!legacy_main||!legacy_Init||!legacy_GetVer||!bind_imports(symbol(m,"__wine_spec_nt_header")))return FALSE;
        legacy_thread_init();return legacy_main(self,reason,reserved);
    }
    if(legacy_main){legacy_thread_init();return legacy_main(self,reason,reserved);}
    return TRUE;
}
/* Original entry points already use Win64 calling convention. Tail jumps retain
 * every register and stack argument, including callback/output structures. */
__asm__(".text\n.globl Init\nInit:\nmovq legacy_Init(%rip),%r11\njmp legacy_enter\n"
    ".globl GetVer\nGetVer:\nmovq legacy_GetVer(%rip),%r11\nlegacy_enter:\n"
    "subq $0xa8,%rsp\nmovq %rcx,0x20(%rsp)\nmovq %rdx,0x28(%rsp)\n"
    "movq %r8,0x30(%rsp)\nmovq %r9,0x38(%rsp)\nmovq %r11,0x40(%rsp)\n"
    "movdqu %xmm0,0x50(%rsp)\nmovdqu %xmm1,0x60(%rsp)\n"
    "movdqu %xmm2,0x70(%rsp)\nmovdqu %xmm3,0x80(%rsp)\n"
    "call *legacy_thread_init(%rip)\nmovq 0x20(%rsp),%rcx\nmovq 0x28(%rsp),%rdx\n"
    "movq 0x30(%rsp),%r8\nmovq 0x38(%rsp),%r9\nmovq 0x40(%rsp),%r11\n"
    "movdqu 0x50(%rsp),%xmm0\nmovdqu 0x60(%rsp),%xmm1\n"
    "movdqu 0x70(%rsp),%xmm2\nmovdqu 0x80(%rsp),%xmm3\n"
    "addq $0xa8,%rsp\njmp *%r11\n");
