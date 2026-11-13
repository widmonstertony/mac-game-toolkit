// Owned GPU queue/copy check; no game, licence or account access.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>

#define CHECK(x) do { HRESULT hr=(x); if(FAILED(hr)){printf("FAIL line=%d hr=%08lx\n",__LINE__,(unsigned long)hr);return 1;} } while(0)
int main() {
 ID3D12Device *device=nullptr;
 CHECK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_ID3D12Device,(void**)&device));
 ID3D12CommandQueue *queue=nullptr;D3D12_COMMAND_QUEUE_DESC q{};
 CHECK(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,(void**)&queue));
 ID3D12CommandAllocator *allocator=nullptr;
 CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&allocator));
 ID3D12GraphicsCommandList *list=nullptr;
 CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_ID3D12GraphicsCommandList,(void**)&list));
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ID3D12Resource *resources[3]{};
 const D3D12_HEAP_TYPE types[]={D3D12_HEAP_TYPE_UPLOAD,D3D12_HEAP_TYPE_DEFAULT,D3D12_HEAP_TYPE_READBACK};
 const D3D12_RESOURCE_STATES states[]={D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST};
 for(unsigned i=0;i<3;i++){D3D12_HEAP_PROPERTIES heap{};heap.Type=types[i];CHECK(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,states[i],nullptr,IID_ID3D12Resource,(void**)&resources[i]));}
 unsigned *mapped=nullptr;D3D12_RANGE none{0,0};CHECK(resources[0]->Map(0,&none,(void**)&mapped));
 for(unsigned i=0;i<64;i++)mapped[i]=0x1367a800u+i;
 resources[0]->Unmap(0,nullptr);
 list->CopyBufferRegion(resources[1],0,resources[0],0,256);
 D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={resources[1],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
 list->CopyBufferRegion(resources[2],0,resources[1],0,256);CHECK(list->Close());
 ID3D12CommandList *commands[]={list};queue->ExecuteCommandLists(1,commands);
 ID3D12Fence *fence=nullptr;CHECK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence));CHECK(queue->Signal(fence,1));
 HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return 2;
 CHECK(fence->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,15000)!=WAIT_OBJECT_0){puts("FAIL GPU fence timeout");return 3;}
 D3D12_RANGE all{0,256};CHECK(resources[2]->Map(0,&all,(void**)&mapped));
 for(unsigned i=0;i<64;i++)if(mapped[i]!=0x1367a800u+i){printf("FAIL readback word=%u\n",i);return 4;}
 resources[2]->Unmap(0,&none);puts("PASS D3D12 device + real GPU queue copy + fence + 64-word exact readback");
 CloseHandle(event);for(auto r:resources)r->Release();fence->Release();list->Release();allocator->Release();queue->Release();device->Release();return 0;
}
