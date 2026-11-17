// SPDX-License-Identifier: AGPL-3.0-only
// Headless integration check: real legacy client version + GPU clear/readback.
// Does not call vendor Init, launch the game or create/control a user window.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
template<class T> struct Ref {
    T *p=nullptr;
    ~Ref(){if(p)p->Release();}
    T *operator->()const{return p;}
    void **out(){return reinterpret_cast<void**>(&p);}
};
#define CHECK(call) do{HRESULT h=(call);if(FAILED(h)){std::printf("GPU_FAIL line=%d hr=%08lx\n",__LINE__,(unsigned long)h);return 3;}}while(0)
int main()
{
    setvbuf(stdout,nullptr,_IONBF,0);
    char apiPath[32768];
    void (*steamShutdown)()=nullptr;
    DWORD apiLength=GetEnvironmentVariableA("MOTORFEST_STEAM_API",apiPath,sizeof(apiPath));
    if(apiLength) {
        if(apiLength>=sizeof(apiPath))return 6;
        HMODULE api=LoadLibraryA(apiPath);
        auto init=api?reinterpret_cast<unsigned char(*)()>(GetProcAddress(api,"SteamAPI_Init")):nullptr;
        if(api && !init)init=reinterpret_cast<unsigned char(*)()>(GetProcAddress(api,"SteamAPI_InitSafe"));
        steamShutdown=api?reinterpret_cast<void(*)()>(GetProcAddress(api,"SteamAPI_Shutdown")):nullptr;
        if(!init || !steamShutdown || !init())return 6;
        std::puts("STEAM_API_INIT_OK");
    }
    HMODULE bridge=LoadLibraryW(L"beclient_x64.dll");
    if(!bridge){std::printf("BRIDGE_LOAD_FAILED error=%lu\n",GetLastError());return 2;}
    auto version=reinterpret_cast<int(*)()>(GetProcAddress(bridge,"GetVer"));
    if(!version || version()!=250)return 2;
    std::puts("BRIDGE_VERSION_PASS version=250 InitCalled=false");
    Ref<ID3D12Device> device;
    CHECK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,__uuidof(ID3D12Device),device.out()));
    Ref<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC q{};
    CHECK(device->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.out()));
    Ref<ID3D12Resource> texture,readback;
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width=64;td.Height=64;td.DepthOrArraySize=1;td.MipLevels=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,
          nullptr,__uuidof(ID3D12Resource),texture.out()));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
    device->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;
    bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    CHECK(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,__uuidof(ID3D12Resource),readback.out()));
    Ref<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    CHECK(device->CreateDescriptorHeap(&hd,__uuidof(ID3D12DescriptorHeap),heap.out()));
    auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(texture.p,nullptr,rtv);
    Ref<ID3D12CommandAllocator> allocator;Ref<ID3D12GraphicsCommandList> list;Ref<ID3D12Fence> fence;
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),allocator.out()));
    CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,__uuidof(ID3D12GraphicsCommandList),list.out()));
    const float color[]={0.25f,0.5f,0.75f,1.0f};list->ClearRenderTargetView(rtv,color,0,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={texture.p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=texture.p;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=readback.p;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);CHECK(list->Close());
    ID3D12CommandList *commands[]={list.p};queue->ExecuteCommandLists(1,commands);
    CHECK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.out()));CHECK(queue->Signal(fence.p,1));
    ULONGLONG start=GetTickCount64();
    while(fence->GetCompletedValue()<1 && GetTickCount64()-start<15000)Sleep(1);
    auto completed=fence->GetCompletedValue();
    if(completed==UINT64_MAX || completed<1){std::printf("GPU_FENCE_FAILED completed=%llu\n",completed);return 4;}
    unsigned char *pixels=nullptr;D3D12_RANGE range{0,(SIZE_T)bytes},none{0,0};
    CHECK(readback->Map(0,&range,reinterpret_cast<void**>(&pixels)));
    unsigned bad=0;const unsigned expected[]={64,128,191,255};
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64*4;x++){
        unsigned v=pixels[footprint.Offset+y*footprint.Footprint.RowPitch+x];
        if(v+1<expected[x%4] || v>expected[x%4]+1)bad++;
    }
    readback->Unmap(0,&none);
    if(bad){std::printf("GPU_READBACK_FAILED bytes=%u\n",bad);return 5;}
    std::puts("COMBINED_BRIDGE_GPU_PASS pixels=4096 visiblePresentation=false gameplay=false");
    if(steamShutdown) {
        steamShutdown();
        char path[32768],nonce[65],payload[160];
        DWORD n=GetEnvironmentVariableA("MOTORFEST_PROBE_RESULT",path,sizeof(path));
        if(!n || n>=sizeof(path))return 7;
        n=GetEnvironmentVariableA("MOTORFEST_PROBE_NONCE",nonce,sizeof(nonce));
        if(n!=32)return 7;
        for(DWORD i=0;i<n;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return 7;
        int size=std::snprintf(payload,sizeof(payload),"STEAM_BRIDGE_GPU_AND_SHUTDOWN_OK %s\n",nonce);
        HANDLE file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return 8;
        DWORD written=0;BOOL ok=WriteFile(file,payload,(DWORD)size,&written,nullptr);
        if(ok)ok=FlushFileBuffers(file);
        CloseHandle(file);
        if(!ok || written!=(DWORD)size)return 8;
    }
    return 0;
}
