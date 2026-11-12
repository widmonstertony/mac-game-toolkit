#include <windows.h>
#include <stdio.h>
int main(void){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);const wchar_t *names[]={L"ucrtbase.dll",L"vcruntime140.dll",L"vcruntime140_1.dll",L"msvcp140.dll",L"concrt140.dll"};int fail=0;for(unsigned i=0;i<5;i++){HMODULE m=LoadLibraryW(names[i]);printf("%ls loaded=%d error=%lu\n",names[i],m!=NULL,m?0:GetLastError());fflush(stdout);if(!m)fail++;}return fail;}
