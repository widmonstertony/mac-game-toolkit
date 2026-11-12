#include <windows.h>
#include <stdio.h>
int wmain(int argc,wchar_t **argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);if(argc!=2)return 2;HMODULE h=LoadLibraryW(argv[1]);printf("module loaded=%d error=%lu\n",h!=NULL,h?0:GetLastError());fflush(stdout);if(h)Sleep(1000);return h?0:1;}
