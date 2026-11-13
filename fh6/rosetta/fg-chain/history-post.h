#pragma once
#include "history-post-bytecode.h"
#include <cmath>
#include <initializer_list>
// Called under nativeLock, only for one proven direct game queue with FG async
// disabled. Snapshot copies are recorded during Prepare, inside input lifetime.
// No readback, waits, logger locks, or per-frame PSO creation in this path.
struct HistoryPost {
 enum{SlotCount=8,BindingCount=32};
 struct Slot{ID3D12Resource *depth{},*motion{};uint64_t frame{};unsigned refs{};bool recorded{},usable{};} slots[SlotCount];
 struct Flight{ID3D12Resource *output{};uint64_t frame{},fenceValue{};Slot *current{},*previous{};bool active{};} flights[SlotCount];
 struct Binding{ID3D12DescriptorHeap *heap{};Slot *current{},*previous{};ID3D12Resource *scene{},*output{};} bindings[BindingCount];
 ID3D12Device *device{};ID3D12Fence *fence{};ID3D12RootSignature *root{};ID3D12PipelineState *pipeline{};
 uint64_t serial{},applied{},declined{};UINT rw{},rh{};UINT64 allocated{};bool recorded{},disabled{};
 HRESULT status{E_PENDING};const char *reason{"not-initialized"};
 bool initialize(ID3D12Device *d){
  if(device)return device==d&&!disabled&&SUCCEEDED(status);
  device=d;device->AddRef();
  D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,5}};
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,24};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=params;ID3DBlob *blob=nullptr;
  auto module=GetModuleHandleW(L"d3d12.dll");auto serialize=module?(decltype(&D3D12SerializeRootSignature))GetProcAddress(module,"D3D12SerializeRootSignature"):nullptr;
  reason="serialize";status=serialize?serialize(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr):E_NOINTERFACE;
  if(SUCCEEDED(status)){reason="root";status=d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void**)&root);}if(blob)blob->Release();
  if(SUCCEEDED(status)){D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=root;p.CS={historyPostBytecode,sizeof(historyPostBytecode)};reason="pipeline";status=d->CreateComputePipelineState(&p,IID_ID3D12PipelineState,(void**)&pipeline);}
  if(SUCCEEDED(status)){reason="fence";status=d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence);}
  if(SUCCEEDED(status))reason="ready";return SUCCEEDED(status);
 }
 void poll(){
  if(!fence)return;auto done=fence->GetCompletedValue();if(done==UINT64_MAX){disabled=true;reason="device-removed";return;}
  for(auto &f:flights)if(f.active&&f.fenceValue&&done>=f.fenceValue){if(f.current&&f.current->refs)f.current->refs--;if(f.previous&&f.previous->refs)f.previous->refs--;if(f.output)f.output->Release();f={};}
 }
 void retire(ID3D12Resource *output,uint64_t frame,ID3D12CommandQueue *queue){
  // The pinned SDK queued this exact output's composition-availability wait
  // before calling us. A signal now covers its previous recorded FG command
  // list; never signal a new frame here and pretend that frame is completed.
  for(auto &f:flights)if(f.active&&!f.fenceValue&&f.output==output&&f.frame<frame){auto value=++serial;if(SUCCEEDED(queue->Signal(fence,value)))f.fenceValue=value;}
  poll();
 }
 static bool input(ID3D12Resource *r,UINT w,UINT h,bool depth){if(!r)return false;auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&d.MipLevels==1&&d.DepthOrArraySize==1&&d.SampleDesc.Count==1&&(depth?(d.Format==DXGI_FORMAT_R32G8X24_TYPELESS||d.Format==DXGI_FORMAT_R32_FLOAT):d.Format==DXGI_FORMAT_R16G16_FLOAT);}
 bool make(Slot &s,ID3D12Resource *depth,ID3D12Resource *motion){
  if(s.depth)return s.depth->GetDesc().Format==depth->GetDesc().Format;
  auto dd=depth->GetDesc(),md=motion->GetDesc();dd.Flags=dd.Format==DXGI_FORMAT_R32G8X24_TYPELESS?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_NONE;md.Flags=D3D12_RESOURCE_FLAG_NONE;
  UINT64 bytes=0;for(auto d:{dd,md}){auto info=device->GetResourceAllocationInfo(0,1,&d);if(info.SizeInBytes==UINT64_MAX||info.SizeInBytes>512ull*1024*1024)return false;bytes+=info.SizeInBytes;}if(allocated+bytes>512ull*1024*1024){reason="snapshot-budget";return false;}
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&dd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&s.depth)))return false;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&md,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&s.motion))){s.depth->Release();s.depth=nullptr;return false;}allocated+=bytes;return true;
 }
 template<class Prepare> bool prepare(const Prepare &p){
  if(!pipeline||disabled)return false;
  unsigned extensions=0;for(auto next=p.header.pNext;next;next=next->pNext){if(++extensions>4||(next->type!=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO&&next->type!=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12)){reason="prepare-extension";return false;}}
  poll();if(disabled)return false;
  auto depth=(ID3D12Resource*)p.depth.resource,motion=(ID3D12Resource*)p.motionVectors.resource;auto w=p.renderSize.width,h=p.renderSize.height;
  auto list=(ID3D12GraphicsCommandList*)p.commandList;
  if(!w||!h||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||!input(depth,w,h,true)||!input(motion,w,h,false)||p.depth.state!=FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ||p.motionVectors.state!=FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ){reason="prepare-layout";return false;}
  if(rw&&(rw!=w||rh!=h)){reason="resolution-changed";return false;}rw=w;rh=h;
  auto &s=slots[p.frameID%SlotCount];
  if(s.recorded&&s.frame==p.frameID){s.usable=false;reason="duplicate-prepare";return false;}
  if(s.refs||flights[p.frameID%SlotCount].active){reason="history-still-in-flight";return false;}
  if(!make(s,depth,motion)){reason="snapshot-allocation";return false;}
  ID3D12Resource *sources[]={depth,motion},*targets[]={s.depth,s.motion};
  for(unsigned k=0;k<2;k++){
   D3D12_RESOURCE_BARRIER b[2]{};for(auto &v:b)v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
   b[0].Transition={sources[k],0,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
   b[1].Transition={targets[k],0,s.recorded?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST};
   list->ResourceBarrier(s.recorded?2:1,b);
   D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=sources[k];from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=targets[k];to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
   b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
   b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(2,b);
  }
  s.frame=p.frameID;s.refs=1;s.recorded=s.usable=true;recorded=true;reason="prepared";return true;
 }
 Slot *find(uint64_t frame){auto &s=slots[frame%SlotCount];return s.recorded&&s.usable&&s.frame==frame?&s:nullptr;}
 Binding *binding(Slot *current,Slot *previous,ID3D12Resource *scene,ID3D12Resource *output){
  Binding *free=nullptr;for(auto &b:bindings){if(b.heap&&b.current==current&&b.previous==previous&&b.scene==scene&&b.output==output)return &b;if(!b.heap&&!free)free=&b;}if(!free){reason="binding-budget";return nullptr;}
  D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=6;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(device->CreateDescriptorHeap(&desc,IID_ID3D12DescriptorHeap,(void**)&free->heap)))return nullptr;
  auto handle=free->heap->GetCPUDescriptorHandleForHeapStart();UINT increment=device->GetDescriptorHandleIncrementSize(desc.Type);ID3D12Resource *inputs[]={current->depth,current->motion,previous->depth,previous->motion,scene};
  for(auto r:inputs){D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=r->GetDesc().Format==DXGI_FORMAT_R32G8X24_TYPELESS?DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:r->GetDesc().Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;device->CreateShaderResourceView(r,&srv,handle);handle.ptr+=increment;}
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R8G8B8A8_UNORM;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(output,nullptr,&uav,handle);
  free->current=current;free->previous=previous;free->scene=scene;free->output=output;scene->AddRef();output->AddRef();return free;
 }
 bool record(const ChainFrame &f,const CapturePrepare &p,const ffxDispatchDescFrameGeneration &fg,ID3D12Resource *scene){
  auto current=find(fg.frameID),previous=fg.frameID?find(fg.frameID-1):nullptr;
  if(disabled||!current||!previous||!p.known||p.frame!=fg.frameID||f.frame!=fg.frameID||!f.hasConstants||f.inverted!=1||f.cameraMotionIncluded!=1||f.motion3D||f.jittered||f.reset||fg.reset||fg.header.pNext||fg.numGeneratedFrames!=1||fg.backbufferTransferFunction!=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB){reason="history-or-convention";return false;}
  if(fg.outputs[0].description.format!=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM||fg.outputs[0].state!=FFX_API_RESOURCE_STATE_UNORDERED_ACCESS){reason="output-format";return false;}
  if(!std::isfinite(p.scale[0])||!std::isfinite(p.scale[1])){reason="invalid-motion-scale";return false;}
  for(auto v:f.clipToPrevious)if(!std::isfinite(v)){reason="invalid-camera";return false;}
  auto out=(ID3D12Resource*)fg.outputs[0].resource;auto b=binding(current,previous,scene,out);if(!b)return false;
  struct Constants{UINT rw,rh,w,h;float matrix[16],scale[2];UINT padding[2];} constants{rw,rh,fg.presentColor.description.width,fg.presentColor.description.height};static_assert(sizeof(constants)==96,"history shader layout");memcpy(constants.matrix,f.clipToPrevious,64);constants.scale[0]=p.scale[0]/rw;constants.scale[1]=p.scale[1]/rh;
  auto list=(ID3D12GraphicsCommandList*)fg.commandList;D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=out;list->ResourceBarrier(1,&barrier);list->SetDescriptorHeaps(1,&b->heap);list->SetComputeRootSignature(root);list->SetPipelineState(pipeline);list->SetComputeRootDescriptorTable(0,b->heap->GetGPUDescriptorHandleForHeapStart());list->SetComputeRoot32BitConstants(1,24,&constants,0);list->Dispatch((constants.w+7)/8,(constants.h+7)/8,1);list->ResourceBarrier(1,&barrier);
  previous->refs++;applied++;reason="applied";return true;
 }
 void submitted(uint64_t frame,ID3D12Resource *output,bool usedPrevious){
  auto s=find(frame);if(!s)return;auto &f=flights[frame%SlotCount];if(f.active)return;f.active=true;f.frame=frame;f.current=s;f.previous=usedPrevious?find(frame-1):nullptr;f.output=output;output->AddRef();
 }
 void release(){for(auto &b:bindings){if(b.scene)b.scene->Release();if(b.output)b.output->Release();if(b.heap)b.heap->Release();b={};}for(auto &f:flights){if(f.output)f.output->Release();f={};}for(auto &s:slots){if(s.depth)s.depth->Release();if(s.motion)s.motion->Release();s={};}if(fence)fence->Release();if(pipeline)pipeline->Release();if(root)root->Release();if(device)device->Release();device=nullptr;fence=nullptr;pipeline=nullptr;root=nullptr;serial=applied=declined=0;rw=rh=0;allocated=0;recorded=disabled=false;status=E_PENDING;reason="not-initialized";}
};
