// SPDX-License-Identifier: LGPL-2.1-or-later
// Local import entry for the owned FH4 Store experiment. The original renderer
// is loaded unchanged from an explicit absolute path; no shared module is edited.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <d3dcommon.h>
#include <cstdio>

extern "C" __declspec(dllimport) HRESULT WINAPI FH4WrapDxgiFactory(IUnknown*,REFIID,void**);
static HMODULE self, renderer;
static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
static HRESULT load_status=E_UNEXPECTED;

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*)
{
    if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);}
    return TRUE;
}

static BOOL CALLBACK load_renderer(PINIT_ONCE,void*,void**)
{
    WCHAR path[32768];
    DWORD n=GetEnvironmentVariableW(L"FH4_UWP_REAL_DXGI",path,ARRAYSIZE(path));
    if(!n||n>=ARRAYSIZE(path)||n<4||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))
    {load_status=E_INVALIDARG;return TRUE;}
    renderer=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!renderer){load_status=HRESULT_FROM_WIN32(GetLastError());return TRUE;}
    if(renderer==self)
    {FreeLibrary(renderer);renderer=nullptr;load_status=HRESULT_FROM_WIN32(ERROR_INVALID_DLL);return TRUE;}
    load_status=S_OK;
    return TRUE;
}

static FARPROC entry(const char *name)
{
    InitOnceExecuteOnce(&once,load_renderer,nullptr,nullptr);
    return SUCCEEDED(load_status)?GetProcAddress(renderer,name):nullptr;
}

extern "C" HRESULT WINAPI FH4_InitializeRenderer()
{
    InitOnceExecuteOnce(&once,load_renderer,nullptr,nullptr);
    std::fprintf(stderr,"FH4_DXGI_RENDERER_INIT hr=%08lx\n",(unsigned long)load_status);
    return load_status;
}

static HRESULT create(const char *name,UINT flags,REFIID iid,void **out,bool version2)
{
    if(!out)return E_POINTER;*out=nullptr;
    auto proc=entry(name);
    if(!proc)return FAILED(load_status)?load_status:HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    IUnknown *native=nullptr;
    HRESULT hr=version2 ? reinterpret_cast<HRESULT(WINAPI*)(UINT,REFIID,void**)>(proc)(flags,iid,(void**)&native)
                       : reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(proc)(iid,(void**)&native);
    if(FAILED(hr))return hr;
    if(!native)return E_UNEXPECTED;
    hr=FH4WrapDxgiFactory(native,iid,out);native->Release();
    std::fprintf(stderr,"FH4_DXGI_ENTRY %s hr=%08lx\n",name,(unsigned long)hr);
    return hr;
}

extern "C" HRESULT WINAPI FH4_CreateDXGIFactory(REFIID iid,void **out)
{return create("CreateDXGIFactory",0,iid,out,false);}
extern "C" HRESULT WINAPI FH4_CreateDXGIFactory1(REFIID iid,void **out)
{return create("CreateDXGIFactory1",0,iid,out,false);}
extern "C" HRESULT WINAPI FH4_CreateDXGIFactory2(UINT flags,REFIID iid,void **out)
{return create("CreateDXGIFactory2",flags,iid,out,true);}
extern "C" HRESULT WINAPI FH4_DXGIGetDebugInterface1(UINT flags,REFIID iid,void **out)
{
    if(!out)return E_POINTER;*out=nullptr;
    auto p=entry("DXGIGetDebugInterface1");
    return p?reinterpret_cast<HRESULT(WINAPI*)(UINT,REFIID,void**)>(p)(flags,iid,out):DXGI_ERROR_UNSUPPORTED;
}
extern "C" HRESULT WINAPI FH4_DXGIDeclareAdapterRemovalSupport()
{
    auto p=entry("DXGIDeclareAdapterRemovalSupport");
    return p?reinterpret_cast<HRESULT(WINAPI*)()>(p)():DXGI_ERROR_UNSUPPORTED;
}
extern "C" HRESULT WINAPI FH4_DXGID3D10CreateDevice(HMODULE module,void *factory,void *adapter,
    UINT flags,const D3D_FEATURE_LEVEL *levels,UINT count,void **device)
{
    auto p=entry("DXGID3D10CreateDevice");
    return p?reinterpret_cast<HRESULT(WINAPI*)(HMODULE,void*,void*,UINT,const D3D_FEATURE_LEVEL*,UINT,void**)>(p)
        (module,factory,adapter,flags,levels,count,device):DXGI_ERROR_UNSUPPORTED;
}
extern "C" HRESULT WINAPI FH4_DXGID3D10RegisterLayers(void *layers,UINT count)
{
    auto p=entry("DXGID3D10RegisterLayers");
    return p?reinterpret_cast<HRESULT(WINAPI*)(void*,UINT)>(p)(layers,count):DXGI_ERROR_UNSUPPORTED;
}
