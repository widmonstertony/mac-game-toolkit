#pragma once
// Shared small GPU submission/upload helper for owned standalone probes.
struct GPU {
 ID3D12Device *d{}; ID3D12CommandQueue *q{}; ID3D12CommandAllocator *a{}; ID3D12GraphicsCommandList *l{}; ID3D12Fence *f{}; HANDLE event{};UINT64 value{};
 bool init(){D3D12_COMMAND_QUEUE_DESC desc{};return SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)&d))&&
 SUCCEEDED(d->CreateCommandQueue(&desc,IID_ID3D12CommandQueue,(void**)&q))&&SUCCEEDED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&a))&&
 SUCCEEDED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a,nullptr,IID_ID3D12GraphicsCommandList,(void**)&l))&&SUCCEEDED(l->Close())&&
 SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&f))&&(event=CreateEventW(nullptr,FALSE,FALSE,nullptr));}
 bool begin(){return SUCCEEDED(a->Reset())&&SUCCEEDED(l->Reset(a,nullptr));}
 bool end(){if(FAILED(l->Close()))return false;ID3D12CommandList *lists[]={l};q->ExecuteCommandLists(1,lists);return SUCCEEDED(q->Signal(f,++value))&&SUCCEEDED(f->SetEventOnCompletion(value,event))&&WaitForSingleObject(event,30000)==WAIT_OBJECT_0;}
};
struct Texture {
 ID3D12Resource *gpu{},*upload{},*readback{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total{},rowBytes{};D3D12_RESOURCE_STATES state{};
 bool init(ID3D12Device *d,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE,UINT w=W,UINT h=H){
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=format;desc.Flags=flags;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;state=D3D12_RESOURCE_STATE_COPY_DEST;
  if(FAILED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_ID3D12Resource,(void**)&gpu)))return false;
  UINT rows;d->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&rowBytes,&total);
  desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=total;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  heap.Type=D3D12_HEAP_TYPE_UPLOAD;
  if(FAILED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)&upload)))return false;
  heap.Type=D3D12_HEAP_TYPE_READBACK;
  return SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&readback));
 }
 void transition(ID3D12GraphicsCommandList *l,D3D12_RESOURCE_STATES next){if(state==next)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={gpu,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,next};l->ResourceBarrier(1,&b);state=next;}
 bool write(ID3D12GraphicsCommandList *l,const BYTE *pixels,UINT rowBytes,D3D12_RESOURCE_STATES next){
  if(rowBytes>fp.Footprint.RowPitch)return false;
  BYTE *p;D3D12_RANGE empty{};if(FAILED(upload->Map(0,&empty,(void**)&p)))return false;
  for(UINT y=0;y<fp.Footprint.Height;y++)memcpy(p+fp.Offset+y*fp.Footprint.RowPitch,pixels+y*rowBytes,rowBytes);upload->Unmap(0,nullptr);
  transition(l,D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;to.pResource=gpu;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  l->CopyTextureRegion(&to,0,0,0,&from,nullptr);transition(l,next);return true;
 }
 void copyOut(ID3D12GraphicsCommandList *l){const auto old=state;transition(l,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=gpu;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;l->CopyTextureRegion(&to,0,0,0,&from,nullptr);transition(l,old);}
 bool read(BYTE *pixels,UINT rowBytes){BYTE *p;D3D12_RANGE r{0,(SIZE_T)total};if(FAILED(readback->Map(0,&r,(void**)&p)))return false;for(UINT y=0;y<fp.Footprint.Height;y++)memcpy(pixels+y*rowBytes,p+fp.Offset+y*fp.Footprint.RowPitch,rowBytes);D3D12_RANGE noWrites{};readback->Unmap(0,&noWrites);return true;}
};
