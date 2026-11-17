// SPDX-License-Identifier: LGPL-2.1-or-later
// Process-local UWP graphics adapter. Uses the actual CoreWindow HWND and
// forwards GPU work to the existing renderer. No game or shared DLL is patched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <cstdlib>
#include <new>

struct CoreWindowInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND *)=0;
    virtual HRESULT STDMETHODCALLTYPE put_MessageHandled(unsigned char)=0;
};
static const IID core_interop_iid={0x45d64a29,0xa63e,0x4cb6,{0xb4,0x98,0x57,0x81,0xd2,0x98,0xcb,0x4f}};

class CoreSwapChain final : public IDXGISwapChain4 {
    LONG refs=1;
    IDXGISwapChain1 *s;
    IDXGISwapChain2 *s2=nullptr;
    IDXGISwapChain3 *s3=nullptr;
    IDXGISwapChain4 *s4=nullptr;
    IUnknown *window;
    IDXGIFactory2 *parent;
public:
    CoreSwapChain(IDXGISwapChain1 *native,IUnknown *core,IDXGIFactory2 *factory):s(native),window(core),parent(factory) {
        window->AddRef();
        parent->AddRef();
        s->QueryInterface(__uuidof(IDXGISwapChain2),(void**)&s2);
        s->QueryInterface(__uuidof(IDXGISwapChain3),(void**)&s3);
        s->QueryInterface(__uuidof(IDXGISwapChain4),(void**)&s4);
    }
    ~CoreSwapChain() {
        if(s4)s4->Release();if(s3)s3->Release();if(s2)s2->Release();
        s->Release();window->Release();parent->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIDeviceSubObject)||
           id==__uuidof(IDXGISwapChain)||id==__uuidof(IDXGISwapChain1)||
           (id==__uuidof(IDXGISwapChain2)&&s2)||(id==__uuidof(IDXGISwapChain3)&&s3)||
           (id==__uuidof(IDXGISwapChain4)&&s4)) {
            *out=static_cast<IDXGISwapChain4*>(this);AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n=InterlockedDecrement(&refs);
        if(!n){this->~CoreSwapChain();std::free(this);}return n;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g,UINT n,const void *p) override {return s->SetPrivateData(g,n,p);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g,const IUnknown *p) override {return s->SetPrivateDataInterface(g,p);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g,UINT *n,void *p) override {return s->GetPrivateData(g,n,p);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID g,void **p) override {return parent->QueryInterface(g,p);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID g,void **p) override {return s->GetDevice(g,p);}
    HRESULT STDMETHODCALLTYPE Present(UINT i,UINT f) override {return s->Present(i,f);}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT i,REFIID g,void **p) override {return s->GetBuffer(i,g,p);}
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL full,IDXGIOutput *o) override {
        return full?DXGI_ERROR_INVALID_CALL:s->SetFullscreenState(FALSE,o);
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *f,IDXGIOutput **o) override {return s->GetFullscreenState(f,o);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *d) override {
        HRESULT hr=s->GetDesc(d);if(SUCCEEDED(hr)&&d)d->OutputWindow=nullptr;return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags) override {
        return s->ResizeBuffers(n,w,h,f,flags);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *) override {return DXGI_ERROR_INVALID_CALL;}
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **p) override {return s->GetContainingOutput(p);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *p) override {return s->GetFrameStatistics(p);}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *p) override {return s->GetLastPresentCount(p);}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *p) override {return s->GetDesc1(p);}
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *p) override {return s->GetFullscreenDesc(p);}
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND *p) override {if(p)*p=nullptr;return DXGI_ERROR_INVALID_CALL;}
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID id,void **out) override {return window->QueryInterface(id,out);}
    HRESULT STDMETHODCALLTYPE Present1(UINT i,UINT f,const DXGI_PRESENT_PARAMETERS *p) override {return s->Present1(i,f,p);}
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override {return s->IsTemporaryMonoSupported();}
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **p) override {return s->GetRestrictToOutput(p);}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *p) override {return s->SetBackgroundColor(p);}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *p) override {return s->GetBackgroundColor(p);}
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION p) override {return s->SetRotation(p);}
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *p) override {return s->GetRotation(p);}
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w,UINT h) override {return s2?s2->SetSourceSize(w,h):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *w,UINT *h) override {return s2?s2->GetSourceSize(w,h):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT p) override {return s2?s2->SetMaximumFrameLatency(p):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *p) override {return s2?s2->GetMaximumFrameLatency(p):E_NOINTERFACE;}
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override {return s2?s2->GetFrameLatencyWaitableObject():nullptr;}
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *p) override {return s2?s2->SetMatrixTransform(p):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *p) override {return s2?s2->GetMatrixTransform(p):E_NOINTERFACE;}
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override {return s3?s3->GetCurrentBackBufferIndex():0;}
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE c,UINT *p) override {return s3?s3->CheckColorSpaceSupport(c,p):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE c) override {return s3?s3->SetColorSpace1(c):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags,const UINT *m,IUnknown *const *q) override {
        return s3?s3->ResizeBuffers1(n,w,h,f,flags,m,q):E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE t,UINT n,void *p) override {return s4?s4->SetHDRMetaData(t,n,p):E_NOINTERFACE;}
};

extern "C" __declspec(dllexport) HRESULT WINAPI FH4CreateCoreWindowSwapChain(
    IDXGIFactory2 *factory,IUnknown *device,IUnknown *window,const DXGI_SWAP_CHAIN_DESC1 *desc,
    IDXGIOutput *restrict_to,IDXGISwapChain1 **out)
{
    if(!out)return E_POINTER;*out=nullptr;
    if(!factory||!device||!window||!desc)return E_INVALIDARG;
    // Foreground-layer/composition semantics cannot be represented by this HWND route.
    if(desc->Flags&DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER)return DXGI_ERROR_UNSUPPORTED;
    CoreWindowInterop *interop=nullptr;HWND hwnd=nullptr;
    HRESULT hr=window->QueryInterface(core_interop_iid,(void**)&interop);
    if(FAILED(hr))return hr;
    hr=interop->get_WindowHandle(&hwnd);interop->Release();
    if(FAILED(hr))return hr;
    if(!hwnd||!IsWindow(hwnd))return DXGI_ERROR_INVALID_CALL;
    IDXGISwapChain1 *native=nullptr;
    hr=factory->CreateSwapChainForHwnd(device,hwnd,desc,nullptr,restrict_to,&native);
    if(FAILED(hr))return hr;
    void *memory=std::malloc(sizeof(CoreSwapChain));
    if(!memory){native->Release();return E_OUTOFMEMORY;}
    *out=new(memory) CoreSwapChain(native,window,factory);
    return S_OK;
}

class CoreFactory final : public IDXGIFactory7 {
    LONG refs=1;
    IDXGIFactory2 *f;
    IDXGIFactory3 *f3=nullptr;
    IDXGIFactory4 *f4=nullptr;
    IDXGIFactory5 *f5=nullptr;
    IDXGIFactory6 *f6=nullptr;
    IDXGIFactory7 *f7=nullptr;
public:
    explicit CoreFactory(IDXGIFactory2 *native):f(native) {
        f->QueryInterface(__uuidof(IDXGIFactory3),(void**)&f3);
        f->QueryInterface(__uuidof(IDXGIFactory4),(void**)&f4);
        f->QueryInterface(__uuidof(IDXGIFactory5),(void**)&f5);
        f->QueryInterface(__uuidof(IDXGIFactory6),(void**)&f6);
        f->QueryInterface(__uuidof(IDXGIFactory7),(void**)&f7);
    }
    ~CoreFactory(){if(f7)f7->Release();if(f6)f6->Release();if(f5)f5->Release();if(f4)f4->Release();if(f3)f3->Release();f->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIFactory)||
           id==__uuidof(IDXGIFactory1)||id==__uuidof(IDXGIFactory2)||
           (id==__uuidof(IDXGIFactory3)&&f3)||(id==__uuidof(IDXGIFactory4)&&f4)||
           (id==__uuidof(IDXGIFactory5)&&f5)||(id==__uuidof(IDXGIFactory6)&&f6)||
           (id==__uuidof(IDXGIFactory7)&&f7)) {
            *out=static_cast<IDXGIFactory7*>(this);AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n=InterlockedDecrement(&refs);if(!n){this->~CoreFactory();std::free(this);}return n;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g,UINT n,const void *p) override {return f->SetPrivateData(g,n,p);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g,const IUnknown *p) override {return f->SetPrivateDataInterface(g,p);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g,UINT *n,void *p) override {return f->GetPrivateData(g,n,p);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID g,void **p) override {return f->GetParent(g,p);}
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT i,IDXGIAdapter **p) override {return f->EnumAdapters(i,p);}
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND w,UINT flags) override {return f->MakeWindowAssociation(w,flags);}
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND *w) override {return f->GetWindowAssociation(w);}
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown *d,DXGI_SWAP_CHAIN_DESC *desc,IDXGISwapChain **out) override {return f->CreateSwapChain(d,desc,out);}
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE m,IDXGIAdapter **out) override {return f->CreateSoftwareAdapter(m,out);}
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT i,IDXGIAdapter1 **out) override {return f->EnumAdapters1(i,out);}
    BOOL STDMETHODCALLTYPE IsCurrent() override {return f->IsCurrent();}
    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override {return f->IsWindowedStereoEnabled();}
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown *d,HWND w,const DXGI_SWAP_CHAIN_DESC1 *desc,
            const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *full,IDXGIOutput *o,IDXGISwapChain1 **out) override {
        return f->CreateSwapChainForHwnd(d,w,desc,full,o,out);
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown *d,IUnknown *w,const DXGI_SWAP_CHAIN_DESC1 *desc,
            IDXGIOutput *o,IDXGISwapChain1 **out) override {
        return FH4CreateCoreWindowSwapChain(this,d,w,desc,o,out);
    }
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE h,LUID *p) override {return f->GetSharedResourceAdapterLuid(h,p);}
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND w,UINT m,DWORD *c) override {return f->RegisterStereoStatusWindow(w,m,c);}
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE h,DWORD *c) override {return f->RegisterStereoStatusEvent(h,c);}
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD c) override {f->UnregisterStereoStatus(c);}
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND w,UINT m,DWORD *c) override {return f->RegisterOcclusionStatusWindow(w,m,c);}
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE h,DWORD *c) override {return f->RegisterOcclusionStatusEvent(h,c);}
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD c) override {f->UnregisterOcclusionStatus(c);}
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown *d,const DXGI_SWAP_CHAIN_DESC1 *s,IDXGIOutput *o,IDXGISwapChain1 **p) override {return f->CreateSwapChainForComposition(d,s,o,p);}
    UINT STDMETHODCALLTYPE GetCreationFlags() override {return f3?f3->GetCreationFlags():0;}
    HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID l,REFIID id,void **p) override {return f4?f4->EnumAdapterByLuid(l,id,p):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID id,void **p) override {return f4?f4->EnumWarpAdapter(id,p):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE t,void *p,UINT n) override {return f5?f5->CheckFeatureSupport(t,p,n):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT n,DXGI_GPU_PREFERENCE g,REFIID id,void **p) override {return f6?f6->EnumAdapterByGpuPreference(n,g,id,p):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent(HANDLE h,DWORD *c) override {return f7?f7->RegisterAdaptersChangedEvent(h,c):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent(DWORD c) override {return f7?f7->UnregisterAdaptersChangedEvent(c):E_NOINTERFACE;}
};

extern "C" __declspec(dllexport) HRESULT WINAPI FH4WrapDxgiFactory(IUnknown *native,REFIID id,void **out)
{
    if(!out)return E_POINTER;*out=nullptr;
    if(!native)return E_INVALIDARG;
    IDXGIFactory2 *f=nullptr;HRESULT hr=native->QueryInterface(__uuidof(IDXGIFactory2),(void**)&f);
    if(FAILED(hr))return hr;
    void *memory=std::malloc(sizeof(CoreFactory));
    if(!memory){f->Release();return E_OUTOFMEMORY;}
    CoreFactory *wrapper=new(memory) CoreFactory(f);
    hr=wrapper->QueryInterface(id,out);wrapper->Release();return hr;
}
