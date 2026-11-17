#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#define CHECK(x) do {HRESULT h=(x);if(FAILED(h)){printf("FAIL line=%d hr=%08lx\n",__LINE__,(unsigned long)h);return 1;}}while(0)
int main(){
 IDXGIFactory6 *f=nullptr; IDXGIFactory7 *f7=nullptr;
 CHECK(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));
 HRESULT h=f->QueryInterface(IID_PPV_ARGS(&f7));printf("FACTORY7 hr=%08lx\n",(unsigned long)h);if(FAILED(h))return 1;
 IUnknown *u=nullptr,*u7=nullptr;CHECK(f->QueryInterface(IID_PPV_ARGS(&u)));CHECK(f7->QueryInterface(IID_PPV_ARGS(&u7)));
 if(u!=u7)return 2;u->Release();u7->Release();
 HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr); DWORD cookie=0;
 CHECK(f7->RegisterAdaptersChangedEvent(event,&cookie));
 if(!cookie||WaitForSingleObject(event,650)!=WAIT_TIMEOUT||!f7->IsCurrent())return 3;
 if(SUCCEEDED(f7->RegisterAdaptersChangedEvent(nullptr,&cookie)))return 4;
 // Separate registration survives closing the caller's handle, then unregisters cleanly.
 DWORD second=0;CHECK(f7->RegisterAdaptersChangedEvent(event,&second));CloseHandle(event);CHECK(f7->UnregisterAdaptersChangedEvent(second));
 IDXGIAdapter1 *a=nullptr; ID3D12Device *d=nullptr;
 CHECK(f7->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a)));
 CHECK(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
 D3D12_COMMAND_QUEUE_DESC qd={}; ID3D12CommandQueue *q=nullptr;CHECK(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
 ID3D12CommandAllocator *alloc=nullptr;CHECK(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
 ID3D12GraphicsCommandList *list=nullptr;CHECK(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_PPV_ARGS(&list)));
 D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_UPLOAD;hp.CreationNodeMask=hp.VisibleNodeMask=1;
 D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=4096;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ID3D12Resource *upload=nullptr,*readback=nullptr;CHECK(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
 hp.Type=D3D12_HEAP_TYPE_READBACK;CHECK(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
 uint32_t *p=nullptr; D3D12_RANGE empty={0,0};CHECK(upload->Map(0,&empty,(void**)&p));for(unsigned i=0;i<1024;++i)p[i]=0x6d6f7400u^i;upload->Unmap(0,nullptr);
 list->CopyBufferRegion(readback,0,upload,0,4096);CHECK(list->Close());ID3D12CommandList *base=list;q->ExecuteCommandLists(1,&base);
 ID3D12Fence *fence=nullptr;CHECK(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 CHECK(q->Signal(fence,1));CHECK(fence->SetEventOnCompletion(1,done));if(WaitForSingleObject(done,10000)!=WAIT_OBJECT_0)return 5;
 CHECK(readback->Map(0,nullptr,(void**)&p));for(unsigned i=0;i<1024;++i)if(p[i]!=(0x6d6f7400u^i))return 6;readback->Unmap(0,&empty);
 printf("GPU_READBACK words=1024 fence=%llu PASS\n",(unsigned long long)fence->GetCompletedValue());
 CloseHandle(done);fence->Release();readback->Release();upload->Release();list->Release();alloc->Release();q->Release();d->Release();a->Release();f7->Release();f->Release();
 printf("FACTORY7_IDENTITY_EVENTS_LIFETIME PASS\n");return 0;
}
