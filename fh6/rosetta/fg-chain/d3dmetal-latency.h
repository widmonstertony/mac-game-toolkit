#pragma once
#include <dxgi1_4.h>

// D3DMetal cannot supply an AMD Anti-Lag 2 driver context. The original FSR
// presenter accepts a successful short read and may dereference uninitialized
// stack bytes. Keep that one unsupported driver key absent at the DXGI boundary.
static constexpr GUID AntiLag2Data{0x5083ae5b, 0x8070, 0x4fca, {0x8e, 0xe5, 0x35, 0x82, 0xdd, 0x36, 0x7d, 0x13}};
using PrivateDataRead = HRESULT (STDMETHODCALLTYPE *)(IDXGISwapChain1 *, REFGUID, UINT *, void *);
using CreateForHwnd = HRESULT (STDMETHODCALLTYPE *)(IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
static SRWLOCK latencyHookLock = SRWLOCK_INIT;
static PrivateDataRead originalPrivateData;
static CreateForHwnd originalCreateForHwnd;

static HRESULT STDMETHODCALLTYPE d3dmetalPrivateData(IDXGISwapChain1 *chain, REFGUID key, UINT *size, void *data)
{
    if (IsEqualGUID(key, AntiLag2Data)) {
        if (size) *size = 0;
        return DXGI_ERROR_NOT_FOUND;
    }
    return originalPrivateData(chain, key, size, data);
}

template<typename Function> static bool attachLatencyHook(Function *slot, Function replacement, Function &original)
{
    AcquireSRWLockExclusive(&latencyHookLock);
    bool hooked = *slot == replacement;
    if (!hooked && (!original || original == *slot)) {
        DWORD protection;
        if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
            original = *slot;
            InterlockedExchangePointer((void *volatile *)slot, (void *)replacement);
            DWORD ignored;
            VirtualProtect(slot, sizeof(*slot), protection, &ignored);
            hooked = true;
        }
    }
    ReleaseSRWLockExclusive(&latencyHookLock);
    return hooked;
}

static HRESULT STDMETHODCALLTYPE d3dmetalCreateForHwnd(IDXGIFactory2 *factory, IUnknown *queue, HWND window,
    const DXGI_SWAP_CHAIN_DESC1 *description, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
    IDXGIOutput *output, IDXGISwapChain1 **chain)
{
    const HRESULT result = originalCreateForHwnd(factory, queue, window, description, fullscreen, output, chain);
    if (SUCCEEDED(result) && chain && *chain) {
        // Slot 5 is IDXGIObject::GetPrivateData across IDXGISwapChain versions.
        auto vtable = *reinterpret_cast<void ***>(*chain);
        const bool hooked = attachLatencyHook(reinterpret_cast<PrivateDataRead *>(&vtable[5]),
                                              d3dmetalPrivateData, originalPrivateData);
        if (report) report(hooked ? "{\"event\":\"d3dmetal_antilag_unavailable\"}"
                                  : "{\"event\":\"d3dmetal_antilag_guard_failed\"}");
    }
    return result;
}

static void protectD3DMetalLatency(ffxCreateContextDescHeader *desc)
{
    wchar_t enabled[2]{};
    if (!desc || desc->type != FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12 ||
        GetEnvironmentVariableW(L"FH6_D3DMETAL_NO_ANTILAG2", enabled, 2) != 1 || enabled[0] != L'1') return;
    auto supplied = ((ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 *)desc)->dxgiFactory;
    IDXGIFactory2 *factory = nullptr;
    if (!supplied || FAILED(supplied->QueryInterface(IID_IDXGIFactory2, (void **)&factory))) return;
    // Slot 15 is IDXGIFactory2::CreateSwapChainForHwnd.
    auto vtable = *reinterpret_cast<void ***>(factory);
    const bool hooked = attachLatencyHook(reinterpret_cast<CreateForHwnd *>(&vtable[15]), d3dmetalCreateForHwnd,
                                         originalCreateForHwnd);
    if (!hooked && report) report("{\"event\":\"d3dmetal_antilag_factory_guard_failed\"}");
    factory->Release();
}
