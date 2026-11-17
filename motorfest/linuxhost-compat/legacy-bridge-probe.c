#include <windows.h>
#include <stdio.h>
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);if(argc!=2)return 2;puts("BRIDGE_LOAD_BEGIN");HMODULE m=LoadLibraryA(argv[1]);if(!m){printf("BRIDGE_LOAD_FAILED error=%lu\n",GetLastError());return 3;}puts("BRIDGE_LOAD_RETURNED");unsigned (*version)(void)=(void*)GetProcAddress(m,"GetVer");if(!version||!GetProcAddress(m,"Init"))return 4;unsigned v=version();printf("BRIDGE_VERSION %u\n",v);return v==0xffffffff ? 5:0;}
