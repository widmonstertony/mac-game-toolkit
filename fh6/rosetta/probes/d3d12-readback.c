#define WIDL_C_INLINE_WRAPPERS
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#define TRY(x) do {HRESULT h=(x);if(FAILED(h)){printf("FAIL %s %#lx\n",#x,(unsigned long)h);return 1;}}while(0)
int main(void){
 ID3D12Device *d;ID3D12CommandQueue *q;ID3D12CommandAllocator *a;ID3D12GraphicsCommandList *l;ID3D12Resource *tex,*buf;ID3D12DescriptorHeap *heap;ID3D12Fence *f;
 TRY(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&d));
 D3D12_COMMAND_QUEUE_DESC qd={0};TRY(ID3D12Device_CreateCommandQueue(d,&qd,&IID_ID3D12CommandQueue,(void**)&q));TRY(ID3D12Device_CreateCommandAllocator(d,0,&IID_ID3D12CommandAllocator,(void**)&a));TRY(ID3D12Device_CreateCommandList(d,0,0,a,NULL,&IID_ID3D12GraphicsCommandList,(void**)&l));
 D3D12_HEAP_PROPERTIES hp={D3D12_HEAP_TYPE_DEFAULT,0,0,1,1};D3D12_RESOURCE_DESC td={0};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=128;td.Height=64;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
 TRY(ID3D12Device_CreateCommittedResource(d,&hp,0,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&tex));
 D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,0,0};TRY(ID3D12Device_CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&heap));D3D12_CPU_DESCRIPTOR_HANDLE handle=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);ID3D12Device_CreateRenderTargetView(d,tex,NULL,handle);FLOAT color[]={1,0.5f,0,1};ID3D12GraphicsCommandList_ClearRenderTargetView(l,handle,color,0,NULL);
 D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=tex;b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;ID3D12GraphicsCommandList_ResourceBarrier(l,1,&b);
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;UINT rows;UINT64 rowbytes,size;ID3D12Device_GetCopyableFootprints(d,&td,0,1,0,&fp,&rows,&rowbytes,&size);hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd={0};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=size;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;TRY(ID3D12Device_CreateCommittedResource(d,&hp,0,&bd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&buf));
 D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=tex;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=buf;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;ID3D12GraphicsCommandList_CopyTextureRegion(l,&dst,0,0,0,&src,NULL);TRY(ID3D12GraphicsCommandList_Close(l));ID3D12CommandList *lists[]={(ID3D12CommandList*)l};ID3D12CommandQueue_ExecuteCommandLists(q,1,lists);TRY(ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void**)&f));HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL);TRY(ID3D12CommandQueue_Signal(q,f,1));TRY(ID3D12Fence_SetEventOnCompletion(f,1,event));if(WaitForSingleObject(event,15000)!=WAIT_OBJECT_0)return 2;
 BYTE *data;D3D12_RANGE range={0,size};TRY(ID3D12Resource_Map(buf,0,&range,(void**)&data));unsigned errors=0;for(unsigned y=0;y<64;y++)for(unsigned x=0;x<128;x++){BYTE *p=data+fp.Offset+y*fp.Footprint.RowPitch+x*4;if(p[0]!=255||p[1]<127||p[1]>128||p[2]!=0||p[3]!=255)errors++;}ID3D12Resource_Unmap(buf,0,NULL);printf("GPU clear/copy/fence/readback pixels=8192 errors=%u\n",errors);return errors?3:0;
}
