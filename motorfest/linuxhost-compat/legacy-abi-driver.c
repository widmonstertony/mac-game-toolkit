/* SPDX-License-Identifier: AGPL-3.0-only */
#include <windows.h>
#include <stdio.h>
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    HMODULE dll=LoadLibraryA(argv[1]);if(!dll){printf("LOAD_FAILED %lu\n",GetLastError());return 3;}
    unsigned (*version)(void)=(void*)GetProcAddress(dll,"GetVer");
    double (*call)(double,double,double,double,double,double)=(void*)GetProcAddress(dll,"Init");
    if(!version||!call||version()!=0x12345678)return 4;
    __asm__ volatile("xorq %%rax,%%rax\nwrfsbase %%rax":::"rax");
    if(call(1,2,4,8,16,32)!=321.0)return 5;
    puts("LEGACY_WIN64_ABI_PASS");return 0;
}
