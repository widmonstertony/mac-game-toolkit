#include <windows.h>
#include <stdio.h>
typedef void*(__cdecl*Query)(unsigned);typedef int(__cdecl*Init)(void);
int wmain(int argc,wchar_t**argv){HMODULE m=argc>1?LoadLibraryW(argv[1]):LoadLibraryW(L"nvapi64.dll");printf("module=%d error=%lu\n",m!=NULL,m?0:GetLastError());if(!m)return 1;Query q=(Query)GetProcAddress(m,"nvapi_QueryInterface");printf("query=%p\n",(void*)q);if(!q)return 2;unsigned ids[]={0x0150e828,0xd22bdd7e,0x21382138};for(unsigned i=0;i<3;i++){void*f=q(ids[i]);printf("query id=%08x ptr=%p\n",ids[i],f);if(i==0&&f)printf("initialize status=%d\n",((Init)f)());}return 0;}
