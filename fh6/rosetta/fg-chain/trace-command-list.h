#include <cwctype>
#pragma once
// Owned COM forwarding object for standalone replay only. It is never loaded
// into FH6 and never patches a driver object/vtable or shader binary.
// Microsoft DirectX-Headers d3dcommon.h / WKPDID_D3DDebugObjectNameW.

struct TraceCommandList:ID3D12GraphicsCommandList {
 ID3D12GraphicsCommandList *real{};ID3D12Device *device{};ID3D12Resource *output{};
 D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
 bool enabled{},maskControl{},maskApplied{};TraceDevice *shaders{};const wchar_t *folder{};UINT count{};wchar_t name[128]{};
 struct Shot {ID3D12Resource *buffer{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{};wchar_t name[128]{};};
 Shot shots[64]{};
 struct Field{ID3D12Resource *buffer{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{},rowBytes{};UINT w{},h{},format{};wchar_t name[128]{};} fields[16]{};
 UINT fieldCount{};UINT64 fieldBytes{};bool fieldsCaptured{};D3D12_GPU_VIRTUAL_ADDRESS cbAddress{};
 void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT index,D3D12_GPU_VIRTUAL_ADDRESS address)override{cbAddress=address;real->SetComputeRootConstantBufferView(index,address);}
 void captureConstants(){
  if(!shaders||!cbAddress)return;
  for(UINT k=0;k<shaders->resourceCount;k++){auto p=shaders->resources[k].p;auto d=p->GetDesc();if(d.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER)continue;auto base=p->GetGPUVirtualAddress();if(cbAddress<base||cbAddress-base+192>d.Width)continue;D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};if(FAILED(p->GetHeapProperties(&heap,&flags))||heap.Type!=D3D12_HEAP_TYPE_UPLOAD)continue;BYTE *data=nullptr;D3D12_RANGE read{};if(SUCCEEDED(p->Map(0,&read,(void**)&data))){HANDLE f=open(folder,L"interpolation-constants.bin",GENERIC_WRITE,CREATE_NEW);DWORD done;if(f!=INVALID_HANDLE_VALUE){WriteFile(f,data+(cbAddress-base),192,&done,nullptr);CloseHandle(f);}D3D12_RANGE empty{};p->Unmap(0,&empty);}break;}
 }
 void replaceMaskForControl(ID3D12Resource *resource,D3D12_RESOURCE_STATES state){
  if(!maskControl)return;
  auto desc=resource->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
  HANDLE f=open(folder,L"override-mask.bin",GENERIC_READ,OPEN_EXISTING);if(f==INVALID_HANDLE_VALUE)return;
  LARGE_INTEGER length{};if(!GetFileSizeEx(f,&length)||length.QuadPart!=(LONGLONG)bytes){CloseHandle(f);return;}
  D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource *upload=nullptr;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)&upload))){CloseHandle(f);return;}
  BYTE *data=nullptr;D3D12_RANGE empty{};if(FAILED(upload->Map(0,&empty,(void**)&data))){CloseHandle(f);return;}DWORD done;bool ok=ReadFile(f,data,(DWORD)bytes,&done,nullptr)&&done==bytes;CloseHandle(f);upload->Unmap(0,nullptr);if(!ok)return;
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={resource,0,state,D3D12_RESOURCE_STATE_COPY_DEST};real->ResourceBarrier(1,&barrier);
  D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;dst.pResource=resource;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;real->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=state;real->ResourceBarrier(1,&barrier);
  // Retain until this bounded standalone process has completed its queue.
  maskApplied=true;printf("Applied isolated visibility mask control bytes=%llu\n",(unsigned long long)bytes);
 }
 void captureFields(){
  if(!enabled||fieldsCaptured||!shaders||wcscmp(name,L"INTERPOLATION"))return;fieldsCaptured=true;captureConstants();
  for(UINT k=0;k<shaders->resourceCount;k++){
   auto &r=shaders->resources[k];auto d=r.p->GetDesc();if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.MipLevels!=1||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1)continue;
   wchar_t label[128]{};UINT bytes=sizeof(label)-2;if(FAILED(r.p->GetPrivateData(traceDebugNameW,&bytes,label)))continue;label[bytes/2<128?bytes/2:127]=0;
   printf("SDK resource name=%ls format=%u size=%llux%u state=%u\n",label,(unsigned)d.Format,(unsigned long long)d.Width,d.Height,(unsigned)r.state);
   wchar_t lower[128];for(int i=0;i<128;i++)lower[i]=towlower(label[i]);
   if(fieldCount>=16||(!wcsstr(lower,L"motion")&&!wcsstr(lower,L"disocclusion")&&!wcsstr(lower,L"dilatedvelocity")&&!wcsstr(lower,L"dilateddepth")&&!wcsstr(lower,L"previousinterpolationsouce")&&!wcsstr(lower,L"opticalflow_result")&&!wcsstr(lower,L"reconstructeddepthinterpolated")&&!wcsstr(lower,L"reconstructedprevnearestdepth")))continue;
   auto &s=fields[fieldCount];wcscpy(s.name,label);s.w=(UINT)d.Width;s.h=d.Height;s.format=d.Format;
   device->GetCopyableFootprints(&d,0,1,0,&s.fp,nullptr,&s.rowBytes,&s.bytes);if(s.bytes>128ull*1024*1024||fieldBytes+s.bytes>512ull*1024*1024)continue;
   D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=s.bytes;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
   if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&s.buffer)))continue;
   D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={r.p,0,r.state,D3D12_RESOURCE_STATE_COPY_SOURCE};if(r.state!=D3D12_RESOURCE_STATE_COPY_SOURCE)real->ResourceBarrier(1,&barrier);
   D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=r.p;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=s.buffer;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=s.fp;real->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
   if(r.state!=D3D12_RESOURCE_STATE_COPY_SOURCE){barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.StateAfter=r.state;real->ResourceBarrier(1,&barrier);}
   printf("Field %u name=%ls format=%u size=%ux%u pitch=%u rowBytes=%llu bytes=%llu\n",fieldCount,label,s.format,s.w,s.h,s.fp.Footprint.RowPitch,(unsigned long long)s.rowBytes,(unsigned long long)s.bytes);fieldBytes+=s.bytes;fieldCount++;
   if(!wcscmp(label,L"FI_DisocclusionMask"))replaceMaskForControl(r.p,r.state);
  }
 }
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out)override{
  if(iid==IID_IUnknown||iid==IID_ID3D12Object||iid==IID_ID3D12DeviceChild||iid==IID_ID3D12CommandList||iid==IID_ID3D12GraphicsCommandList){*out=this;AddRef();return S_OK;}
  return real->QueryInterface(iid,out);
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return real->AddRef();}
 ULONG STDMETHODCALLTYPE Release()override{return real->Release();}
#include "trace-command-list-forward.h"
 void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState *pso)override{
  real->SetPipelineState(pso);name[0]=0;UINT bytes=sizeof(name)-sizeof(wchar_t);
  if(pso && SUCCEEDED(pso->GetPrivateData(traceDebugNameW,&bytes,name)))name[bytes/2<128?bytes/2:127]=0;name[127]=0;
  if(enabled && shaders && (!wcscmp(name,L"INTERPOLATION")||!wcscmp(name,L"GAME_MOTION_VECTOR_FIELD")||!wcscmp(name,L"DISOCCLUSION_MASK"))){wchar_t file[160];swprintf(file,160,L"%ls.dxil",name);shaders->save(pso,folder,file);}
 }
 void STDMETHODCALLTYPE ResourceBarrier(UINT n,const D3D12_RESOURCE_BARRIER *b)override{
  for(UINT i=0;i<n;i++)if(b[i].Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && b[i].Transition.pResource==output)state=b[i].Transition.StateAfter;
  if(shaders)shaders->barrier(n,b);real->ResourceBarrier(n,b);
 }
 void STDMETHODCALLTYPE Dispatch(UINT x,UINT y,UINT z)override{
  captureFields();real->Dispatch(x,y,z);capture(x,y,z);
 }
 void STDMETHODCALLTYPE DrawInstanced(UINT vertices,UINT instances,UINT start,UINT startInstance)override{real->DrawInstanced(vertices,instances,start,startInstance);capture(vertices,instances,0);}
 void STDMETHODCALLTYPE DrawIndexedInstanced(UINT indices,UINT instances,UINT start,INT base,UINT startInstance)override{real->DrawIndexedInstanced(indices,instances,start,base,startInstance);capture(indices,instances,0);}
 void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature *sig,UINT max,ID3D12Resource *args,UINT64 offset,ID3D12Resource *counts,UINT64 countOffset)override{real->ExecuteIndirect(sig,max,args,offset,counts,countOffset);capture(max,0,0);}
 void capture(UINT x,UINT y,UINT z){
  if(!enabled||count>=64)return;
  auto &s=shots[count];wcscpy(s.name,name);const auto d=output->GetDesc();
  D3D12_RESOURCE_DESC region=d;region.Width=1200;region.Height=1066;
  device->GetCopyableFootprints(&region,0,1,0,&s.fp,nullptr,nullptr,&s.bytes);
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=s.bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&s.buffer)))return;
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={output,0,state,D3D12_RESOURCE_STATE_COPY_SOURCE};
  if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE)real->ResourceBarrier(1,&barrier);
  D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=output;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.pResource=s.buffer;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=s.fp;
  D3D12_BOX box{1200,1200,0,2400,2266,1};real->CopyTextureRegion(&dst,0,0,0,&src,&box);
  if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE){barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.StateAfter=state;real->ResourceBarrier(1,&barrier);}
  printf("Stage %u PSO=%ls dispatch=%u,%u,%u outputState=%u\n",count,name,x,y,z,(unsigned)state);count++;
 }
 bool save(const wchar_t *dir){
  for(UINT i=0;i<count;i++){
   auto &s=shots[i];BYTE *p=nullptr;D3D12_RANGE range{0,(SIZE_T)s.bytes};if(FAILED(s.buffer->Map(0,&range,(void**)&p)))return false;
   wchar_t filename[40];swprintf(filename,40,L"stage-%02u.bin",i);HANDLE file=open(dir,filename,GENERIC_WRITE,CREATE_NEW);if(file==INVALID_HANDLE_VALUE)return false;
   bool ok=true;for(UINT y=0;y<1066;y++){DWORD done;if(!WriteFile(file,p+y*s.fp.Footprint.RowPitch,1200*4,&done,nullptr)||done!=1200*4){ok=false;break;}}
   CloseHandle(file);D3D12_RANGE noWrite{};s.buffer->Unmap(0,&noWrite);s.buffer->Release();s.buffer=nullptr;if(!ok)return false;
  }
  for(UINT i=0;i<fieldCount;i++){
   auto &s=fields[i];BYTE *p=nullptr;D3D12_RANGE range{0,(SIZE_T)s.bytes};if(FAILED(s.buffer->Map(0,&range,(void**)&p)))return false;
   wchar_t filename[40];swprintf(filename,40,L"field-%02u.bin",i);HANDLE f=open(dir,filename,GENERIC_WRITE,CREATE_NEW);if(f==INVALID_HANDLE_VALUE)return false;DWORD done;bool ok=WriteFile(f,p,(DWORD)s.bytes,&done,nullptr)&&done==s.bytes;CloseHandle(f);D3D12_RANGE empty{};s.buffer->Unmap(0,&empty);s.buffer->Release();if(!ok)return false;
  }
  return true;
 }
};
