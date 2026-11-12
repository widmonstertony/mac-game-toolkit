#include <windows.h>
/* lsteamclient allocates SteamClient objects inside steamclient64's .data
 * for API compatibility. Keep this arena free of CRT state: the CRT's
 * thread-attach writes would otherwise overwrite the interface pointer. */
__attribute__((used,section(".data"))) volatile BYTE steam_interface_arena[65536] = {1};
BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID p){(void)h;(void)r;(void)p;return TRUE;}
