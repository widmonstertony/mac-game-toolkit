/* SPDX-License-Identifier: AGPL-3.0-only */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "elfload.h"
#include "glibc.h"
int main(int argc,char **argv)
{
    char error[256];
    if (argc<2) return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    struct elf_module *module=elf_load(argv[1],linuxhost_resolve,NULL,error,sizeof(error));
    if (!module) { printf("LOAD_FAIL %s\n",error); return 3; }
    const struct elf_stats *stats=elf_get_stats(module);
    printf("ELF_IMPORTS resolved=%u unresolved=%u unsupported=%u\n",stats->imports,stats->unresolved,stats->unsupported);
    if (stats->unresolved || stats->unsupported) return 4;
    if (argc==2)
    {
        long long (SYSV *fn)(void)=elf_symbol(module,"compat_test");
        if (!fn) return 5;
        long long result=fn();
        printf("STRING_ABI_%s result=%lld\n",result ? "FAIL" : "PASS",result);
        return result ? 6 : 0;
    }
    long long (SYSV *fn)(long long)=elf_symbol(module,"compat_overflow");
    if (!fn) return 5;
    fn(atoi(argv[2]));
    puts("FORTIFY_UNEXPECTED_RETURN"); return 7;
}
