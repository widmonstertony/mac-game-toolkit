#include <windows.h>
#include <stdio.h>
int main(void) { HMODULE m=LoadLibraryW(L"winemetalgl.dll"); DWORD a=GetLastError(); HMODULE g=LoadLibraryW(L"opengl32.dll"); DWORD b=GetLastError(); FARPROC p=g?GetProcAddress(g,"glDisable"):0; printf("GL32_PROBE module=%p error=%lu opengl=%p error=%lu export=%p result=%s\n",m,a,g,b,p,m&&g&&p?"PASS":"FAIL"); return m&&g&&p?0:1; }
