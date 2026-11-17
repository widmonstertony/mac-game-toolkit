#include <windows.h>
#include <stdio.h>
int main(void){HANDLE t;DWORD n=0;BOOL b=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&t);printf("open=%d err=%lu\n",b,GetLastError());SetLastError(0);b=GetTokenInformation(t,TokenUser,NULL,0,&n);printf("query=%d err=%lu bytes=%lu\n",b,GetLastError(),n);return 0;}
