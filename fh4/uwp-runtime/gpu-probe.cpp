// SPDX-License-Identifier: LGPL-2.1-or-later
// Exercise a real D3D12 swap-chain buffer using the supplied CoreWindow.
// This does not show or drive the window, or claim visible presentation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdint>

template<class T> struct Ptr {
    T *p=nullptr;
    ~Ptr() { if(p) p->Release(); }
    T *operator->() const { return p; }
    void **out() { return reinterpret_cast<void**>(&p); }
};
#define HR(expr) do { HRESULT hr_=(expr); if(FAILED(hr_)) { \
    std::printf("GPU_FAIL line=%d operation=%s hr=%08lx\n",__LINE__,#expr,(unsigned long)hr_); return hr_; } } while(0)

extern "C" __declspec(dllimport) HRESULT WINAPI FH4CreateCoreWindowSwapChain(
    IDXGIFactory2*,IUnknown*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**);
extern "C" __declspec(dllimport) HRESULT WINAPI FH4WrapDxgiFactory(IUnknown*,REFIID,void**);

extern "C" __declspec(dllexport) HRESULT WINAPI RunGpuProbe(IUnknown *window,BOOL bridge)
{
    Ptr<ID3D12Device> device;
    Ptr<ID3D12CommandQueue> queue;
    Ptr<IDXGIFactory2> factory;
    Ptr<IDXGISwapChain1> swap;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,__uuidof(ID3D12Device),device.out()));
    D3D12_COMMAND_QUEUE_DESC q{};
    HR(device->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.out()));
    HR(CreateDXGIFactory1(__uuidof(IDXGIFactory2),factory.out()));
    {
        Ptr<IDXGIFactory> legacy;
        Ptr<IDXGIFactory2> version2,from_legacy;
        HR(CreateDXGIFactory(__uuidof(IDXGIFactory),legacy.out()));
        HR(legacy->QueryInterface(__uuidof(IDXGIFactory2),from_legacy.out()));
        HR(CreateDXGIFactory2(0,__uuidof(IDXGIFactory2),version2.out()));
        factory.p->Release();factory.p=version2.p;version2.p=nullptr;
        std::puts("GPU_FACTORY_EXPORTS_PASS versions=0,1,2");
    }
    if(bridge) {
        IDXGIFactory2 *wrapped=nullptr;
        HR(FH4WrapDxgiFactory(factory.p,__uuidof(IDXGIFactory2),(void**)&wrapped));
        factory.p->Release();factory.p=wrapped;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=64;desc.Height=64;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    HR(factory->CreateSwapChainForCoreWindow(queue.p,window,&desc,nullptr,&swap.p));
    std::puts("GPU_COREWINDOW_SWAPCHAIN_CREATED");
    {
        Ptr<IUnknown> expected,actual;
        HR(window->QueryInterface(__uuidof(IUnknown),expected.out()));
        HR(swap->GetCoreWindow(__uuidof(IUnknown),actual.out()));
        if(expected.p!=actual.p)return E_UNEXPECTED;
        DXGI_SWAP_CHAIN_DESC1 returned{};HR(swap->GetDesc1(&returned));
        if(returned.Width!=desc.Width||returned.Height!=desc.Height||returned.Format!=desc.Format||
           returned.BufferCount!=desc.BufferCount)return E_UNEXPECTED;
        Ptr<IDXGISwapChain3> third;HR(swap->QueryInterface(__uuidof(IDXGISwapChain3),third.out()));
        Ptr<IUnknown> first_identity,third_identity;
        HR(swap->QueryInterface(__uuidof(IUnknown),first_identity.out()));
        HR(third->QueryInterface(__uuidof(IUnknown),third_identity.out()));
        if(first_identity.p!=third_identity.p)return E_UNEXPECTED;
        Ptr<IDXGIFactory2> parent;
        HR(swap->GetParent(__uuidof(IDXGIFactory2),parent.out()));
        if(parent.p!=factory.p)return E_UNEXPECTED;
        std::puts("GPU_COREWINDOW_CONTRACT_PASS identity=retained desc=preserved swapchain3=consistent");
    }
    Ptr<ID3D12Resource> buffer, readback;
    Ptr<ID3D12CommandAllocator> allocator;
    Ptr<ID3D12GraphicsCommandList> list;
    Ptr<ID3D12DescriptorHeap> rtv;
    Ptr<ID3D12Fence> fence;
    HR(swap->GetBuffer(0,__uuidof(ID3D12Resource),buffer.out()));
    HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),allocator.out()));
    HR(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,
                                __uuidof(ID3D12GraphicsCommandList),list.out()));
    D3D12_DESCRIPTOR_HEAP_DESC dh{};dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;dh.NumDescriptors=1;
    HR(device->CreateDescriptorHeap(&dh,__uuidof(ID3D12DescriptorHeap),rtv.out()));
    auto handle=rtv->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(buffer.p,nullptr,handle);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width=64*64*4;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;
    bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),readback.out()));
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={buffer.p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                  D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET};
    list->ResourceBarrier(1,&b);
    const float color[]={0.25f,0.5f,0.75f,1.0f};
    list->ClearRenderTargetView(handle,color,0,nullptr);
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=buffer.p;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=readback.p;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint={desc.Format,64,64,1,256};
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
    list->ResourceBarrier(1,&b);HR(list->Close());
    ID3D12CommandList *commands[]={list.p};queue->ExecuteCommandLists(1,commands);
    HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.out()));
    HR(queue->Signal(fence.p,1));
    ULONGLONG start=GetTickCount64();
    while(fence->GetCompletedValue()<1 && GetTickCount64()-start<15000) Sleep(1);
    if(fence->GetCompletedValue()==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
    if(fence->GetCompletedValue()<1) return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    unsigned char *pixels=nullptr;D3D12_RANGE range{0,64*64*4},none{0,0};
    HR(readback->Map(0,&range,reinterpret_cast<void**>(&pixels)));
    unsigned bad=0;const unsigned expected[]={64,128,191,255};
    for(unsigned i=0;i<64*64*4;i++)
        if(pixels[i]+1<expected[i%4] || pixels[i]>expected[i%4]+1) ++bad;
    readback->Unmap(0,&none);
    if(bad) { std::printf("GPU_READBACK_FAIL bytes=%u\n",bad);return E_FAIL; }
    std::puts("GPU_COREWINDOW_READBACK_PASS pixels=4096");
    const HRESULT present=swap->Present(0,0);
    std::printf("GPU_PRESENT_ATTEMPT hr=%08lx visiblePresentationVerified=false\n",(unsigned long)present);
    if(FAILED(present))return present;
    // Release every client reference before resizing the real swap chain.
    list.p->Release();list.p=nullptr;rtv.p->Release();rtv.p=nullptr;
    buffer.p->Release();buffer.p=nullptr;
    HR(swap->ResizeBuffers(2,128,96,desc.Format,0));
    DXGI_SWAP_CHAIN_DESC1 resized{};HR(swap->GetDesc1(&resized));
    if(resized.Width!=128||resized.Height!=96)return E_UNEXPECTED;
    std::puts("GPU_COREWINDOW_RESIZE_PASS width=128 height=96");
    return S_OK;
}
