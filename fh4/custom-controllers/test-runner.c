/* LGPL-2.1-or-later. Runs only the self-authored registry test export. */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    HMODULE m=LoadLibraryW(L"registry-test.dll");
    DWORD (WINAPI *run)(void)=m?(void*)GetProcAddress(m,"FH4CustomControllerSelfTest"):NULL;
    DWORD result;
    if(!run){printf("TEST_LOAD_FAILED %lu\n",GetLastError());return 1;}
    result=run();printf("CUSTOM_CONTROLLER_REGISTRY %s line=%lu\n",result?"FAIL":"PASS",result);
    return result?1:0;
}
