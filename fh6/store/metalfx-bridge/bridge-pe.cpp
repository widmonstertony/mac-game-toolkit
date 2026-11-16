#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
static HMODULE module;
extern "C" __declspec(dllexport) LONG WINAPI MetalFxBridgeCall(unsigned operation,void *args) {
    using Query=LONG(WINAPI*)(HANDLE,const void*,int,void*,SIZE_T,SIZE_T*);
    using Call=LONG(WINAPI*)(uint64_t,unsigned,void*);
    auto ntdll=GetModuleHandleW(L"ntdll.dll");
    auto query=(Query)GetProcAddress(ntdll,"NtQueryVirtualMemory");
    auto call=(Call)GetProcAddress(ntdll,"__wine_unix_call");
    if(!query||!call||operation>4)return (LONG)0xc000007a;
    uint64_t handle=0;LONG result=query(GetCurrentProcess(),module,1000,&handle,sizeof(handle),nullptr);
    return result?result:call(handle,operation,args);
}
extern "C" BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH)module=instance;return TRUE;}
