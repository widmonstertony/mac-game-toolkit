#pragma once
// Own forwarding device for replay: retain the AMD-provided DXIL submitted to
// the public CreateComputePipelineState API. No Apple binary/cache inspection.
static const GUID traceDebugNameW={0x4cca5fd8,0x921f,0x42c8,{0x85,0x66,0x70,0xca,0xf2,0xa9,0xb7,0x41}};
struct TraceDevice:ID3D12Device {
 ID3D12Device *real{};UINT count{},resourceCount{};
 struct Resource{ID3D12Resource *p{};D3D12_RESOURCE_STATES state{};} resources[512]{};
 void remember(REFIID iid,void **out,D3D12_RESOURCE_STATES state,HRESULT hr){if(SUCCEEDED(hr)&&iid==IID_ID3D12Resource&&resourceCount<512){auto p=(ID3D12Resource*)*out;p->AddRef();resources[resourceCount++]={p,state};}}
 HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES *heap,D3D12_HEAP_FLAGS flags,const D3D12_RESOURCE_DESC *desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE *clear,REFIID iid,void **out)override{auto hr=real->CreateCommittedResource(heap,flags,desc,state,clear,iid,out);remember(iid,out,state,hr);return hr;}
 HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap *heap,UINT64 offset,const D3D12_RESOURCE_DESC *desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE *clear,REFIID iid,void **out)override{auto hr=real->CreatePlacedResource(heap,offset,desc,state,clear,iid,out);remember(iid,out,state,hr);return hr;}
 void barrier(UINT n,const D3D12_RESOURCE_BARRIER *b){for(UINT i=0;i<n;i++)if(b[i].Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)for(UINT k=0;k<resourceCount;k++)if(resources[k].p==b[i].Transition.pResource)resources[k].state=b[i].Transition.StateAfter;}
 struct Entry{ID3D12PipelineState *pso{};BYTE *code{};SIZE_T size{};} entries[128]{};
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out)override{
  if(iid==IID_IUnknown||iid==IID_ID3D12Object||iid==IID_ID3D12Device){*out=this;AddRef();return S_OK;}
  return real->QueryInterface(iid,out);
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return real->AddRef();}
 ULONG STDMETHODCALLTYPE Release()override{return real->Release();}
#include "trace-device-forward.h"
 HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,REFIID iid,void **out)override{
  const HRESULT hr=real->CreateComputePipelineState(desc,iid,out);
  if(SUCCEEDED(hr)&&iid==IID_ID3D12PipelineState&&count<128&&desc->CS.BytecodeLength<=1048576){
   auto &v=entries[count];v.code=(BYTE*)HeapAlloc(GetProcessHeap(),0,desc->CS.BytecodeLength);
   if(v.code){v.pso=(ID3D12PipelineState*)*out;v.size=desc->CS.BytecodeLength;memcpy(v.code,desc->CS.pShaderBytecode,v.size);count++;}
  }
  return hr;
 }
 bool save(ID3D12PipelineState *pso,const wchar_t *dir,const wchar_t *fileName=L"interpolation-input.dxil"){
  for(auto &v:entries)if(v.pso==pso){HANDLE f=open(dir,fileName,GENERIC_WRITE,CREATE_NEW);if(f==INVALID_HANDLE_VALUE)return false;DWORD done;bool ok=WriteFile(f,v.code,(DWORD)v.size,&done,nullptr)&&done==v.size;CloseHandle(f);printf("Saved public AMD shader input bytes=%llu\n",(unsigned long long)v.size);return ok;}
  return false;
 }
};
