// Real saved-game-pixel GPU validation of the runtime history ownership path.
// Does not install DLLs, launch a game, or inspect live game memory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <api/include/dx12/ffx_api_dx12.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include "chain.h"
#include "gpu-capture.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static const UINT W=128,H=96;
#include "probe-gpu.h"
#include "replay-input.h"
#include "history-post.h"
static FfxApiResource res(Texture &t){return ffxApiGetResourceDX12(t.gpu,FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);}
int wmain(int argc,wchar_t **argv){
 setvbuf(stdout,nullptr,_IONBF,0);CHECK(argc==4);Input input[2];CHECK(load(argv[1],input[0])&&load(argv[2],input[1]));auto a=input[0].h,b=input[1].h;CHECK(a.w==b.w&&a.h==b.h&&a.rw==b.rw&&a.rh==b.rh);auto generated=pixels(argv[2],L"generated-before-ui.bin",b.w,b.h,b.pitch[5]);CHECK(generated);
 GPU g;CHECK(g.init());HistoryPost history;CHECK(history.initialize(g.d));Texture depth,motion,scene,output[3];CHECK(depth.init(g.d,DXGI_FORMAT_R32G8X24_TYPELESS,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,b.rw,b.rh));CHECK(motion.init(g.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,b.rw,b.rh));CHECK(scene.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,b.w,b.h));for(auto &o:output)CHECK(o.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,b.w,b.h));
 ID3D12QueryHeap *query=nullptr;D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=2;CHECK(SUCCEEDED(g.d->CreateQueryHeap(&qd,IID_ID3D12QueryHeap,(void**)&query)));ID3D12Resource *timing=nullptr;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=16;bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;CHECK(SUCCEEDED(g.d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&timing)));
 for(UINT id=0;id<18;id++){
  auto &v=input[id==17?1:0];auto &o=output[id%3];history.retire(o.gpu,id,g.q);CHECK(g.begin());auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  CHECK(depth.write(g.l,v.depth,b.rw*4,read)&&motion.write(g.l,v.motion,b.rw*4,read)&&scene.write(g.l,v.scene,b.w*4,read));CHECK(o.write(g.l,generated,b.w*4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
  ffxDispatchDescFrameGenerationPrepare prep{};prep.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;prep.frameID=id;prep.commandList=g.l;prep.renderSize={b.rw,b.rh};prep.depth=res(depth);prep.motionVectors=res(motion);CHECK(history.prepare(prep));
  ChainFrame frame=v.frame;frame.frame=id;if(id<17)for(int k=0;k<16;k++)frame.clipToPrevious[k]=k%5==0?1.f:0.f;
  CapturePrepare captured{};captured.frame=id;captured.known=true;captured.scale[0]=v.h.scale[0];captured.scale[1]=v.h.scale[1];
  ffxDispatchDescFrameGeneration fg{};fg.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;fg.frameID=id;fg.commandList=g.l;fg.numGeneratedFrames=1;fg.reset=id==0;fg.presentColor=res(scene);fg.outputs[0]=ffxApiGetResourceDX12(o.gpu,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);fg.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
  // Reset/NaN guards must not record GPU work or acquire a previous reader.
  if(id==17){auto f=frame;f.clipToPrevious[0]=NAN;CHECK(!history.record(f,captured,fg,scene.gpu));auto bad=fg;bad.reset=true;CHECK(!history.record(frame,captured,bad,scene.gpu));}
  if(id==17)g.l->EndQuery(query,D3D12_QUERY_TYPE_TIMESTAMP,0);
  bool corrected=history.record(frame,captured,fg,scene.gpu);if(id==17){g.l->EndQuery(query,D3D12_QUERY_TYPE_TIMESTAMP,1);g.l->ResolveQueryData(query,D3D12_QUERY_TYPE_TIMESTAMP,0,2,timing,0);}CHECK(corrected==(id>0));history.submitted(id,o.gpu,corrected);
  auto blocked=prep;blocked.frameID=id+8;CHECK(!history.prepare(blocked));CHECK(!strcmp(history.reason,"history-still-in-flight"));
  if(id==17)o.copyOut(g.l);CHECK(g.end());printf("GPU history frame=%u dispatched=%u allocated=%llu\n",id,corrected,(unsigned long long)history.allocated);
 }
 auto read=(BYTE*)HeapAlloc(GetProcessHeap(),0,SIZE_T(b.w)*b.h*4);CHECK(read&&output[17%3].read(read,b.w*4));HANDLE f=open(argv[3],L"post-generated.bin",GENERIC_WRITE,CREATE_NEW);CHECK(f!=INVALID_HANDLE_VALUE);DWORD done;CHECK(WriteFile(f,read,b.w*b.h*4,&done,nullptr)&&done==b.w*b.h*4);CloseHandle(f);
 unsigned changed=0,notBase=0;for(UINT i=0;i<b.w*b.h;i++){bool diff=false,base=false;for(UINT k=0;k<3;k++){diff|=read[i*4+k]!=generated[i*4+k];base|=abs(int(read[i*4+k])-int(input[1].scene[i*4+k]))>3;}changed+=diff;notBase+=diff&&base;}
 // Prove Prepare snapshots own their contents even when input resources are
 // reused every frame, slots wrap twice and outputs rotate across three slots.
 for(auto &o:output)history.retire(o.gpu,30,g.q);CHECK(g.begin());CHECK(g.end());history.poll();for(auto &s:history.slots)CHECK(s.refs==0);for(auto &f:history.flights)CHECK(!f.active);
 UINT64 *ticks=nullptr,frequency{};D3D12_RANGE range{0,16};CHECK(SUCCEEDED(timing->Map(0,&range,(void**)&ticks)));CHECK(SUCCEEDED(g.q->GetTimestampFrequency(&frequency))&&frequency);printf("Visibility post GPU ms=%.6f (one saved-frame dispatch; not gameplay FPS)\n",double(ticks[1]-ticks[0])*1000/frequency);D3D12_RANGE empty{};timing->Unmap(0,&empty);timing->Release();query->Release();
 CHECK(history.applied==17);printf("PASS GPU runtime history: frames=18 slotWraps=2 outputs=3 corrected_pixels=%u corrected_still_not_base=%u reset/NaN/in-flight overwrite guards; no outstanding references\n",changed,notBase);history.release();return 0;
}
