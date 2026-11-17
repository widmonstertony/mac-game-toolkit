/* SPDX-License-Identifier: AGPL-3.0-only
 * Default: read-only ELF linkage, no library or client constructors.
 * --own-probe: executes the separately built, owned C++ behavioral probe.
 * Only explicitly supplied GNU runtime libraries participate in lookup. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "elfload.h"
#include "glibc.h"
#include "syscall.h"

static unsigned library_resolutions;
static void *resolve_with_libraries(const char *symbol,void *context)
{
    static const char *const names[]={"libgcc_s.so.1","libstdc++.so.6"};
    for (unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
    {
        struct elf_module *module=elf_find(names[i]);
        void *address=module ? elf_symbol(module,symbol) : NULL;
        if (address)
        {
            printf("LIBRARY_RESOLVE %s %s\n",names[i],symbol);
            ++library_resolutions;
            return address;
        }
    }
    return linuxhost_resolve(symbol,NULL);
}

int main(int argc,char **argv)
{
    unsigned unresolved=0, unsupported=0;
    int run_probe=argc==5 && !strcmp(argv[4],"--own-probe");
    struct elf_module *loaded[3];
    if (argc!=4 && !run_probe) { puts("usage: dependency-probe libgcc_s.so.1 libstdc++.so.6 client.so"); return 2; }
    if(run_probe)
    {
        const char *base=strrchr(argv[3],'/');base=base ? base+1 : argv[3];
        if(strcmp(base,"cxx-probe.so")) { puts("Refusing to execute anything except own cxx-probe.so");return 2; }
    }
    setvbuf(stdout,NULL,_IONBF,0);
    for (int i=1;i<4;i++)
    {
        char error[256];
        struct elf_module *module=elf_load(argv[i],resolve_with_libraries,NULL,error,sizeof(error));
        if (!module) { printf("LOAD_FAILED %s\n",error); return 3; }
        const struct elf_stats *s=elf_get_stats(module);
        loaded[i-1]=module;
        unresolved+=s->unresolved; unsupported+=s->unsupported;
        printf("MODULE %s imports=%u unresolved=%u unsupported=%u\n",elf_get_name(module),s->imports,s->unresolved,s->unsupported);
        if (s->unresolved) printf("MISSING %s\n",s->unresolved_names);
    }
    printf("LINKAGE_TOTAL unresolved=%u unsupported=%u libraryResolved=%u codeExecuted=false\n",unresolved,unsupported,library_resolutions);
    if(run_probe && !unresolved && !unsupported)
    {
        for(unsigned i=0;i<3;i++)
        {
            if(!linuxhost_register_syscalls(loaded[i])) { puts("SYSCALL_REGISTRATION_FAILED");return 4; }
            elf_run_constructors(loaded[i]);
        }
        long long (SYSV *test)(void)=elf_symbol(loaded[2],"cxx_test");
        if(!test)return 5;
        long long result=test();
        printf("GNU_CXX_%s result=%lld clientCodeExecuted=false\n",result ? "FAIL" : "PASS",result);
        return result ? 6 : 0;
    }
    return unresolved || unsupported ? 1 : 0;
}
