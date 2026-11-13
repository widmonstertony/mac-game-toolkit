// Owned exit forwarding fixture, no game process or game file access.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <cstring>
__attribute__((noinline,noreturn)) static void crtExit(){std::exit(37);}
__attribute__((noinline,noreturn)) static void winExit(){ExitProcess(37);}
int main(int argc,char **argv){HMODULE h=LoadLibraryW(L"fh6-fg-chain.asi");if(!h)return 2;auto initialize=(void(*)())GetProcAddress(h,"InitializeASI");if(!initialize)return 3;initialize();if(argc>1&&!strcmp(argv[1],"win"))winExit();crtExit();}
