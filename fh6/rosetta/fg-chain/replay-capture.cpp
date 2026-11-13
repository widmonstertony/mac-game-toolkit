// Replay captured game pixels through the unchanged official FG provider.
// No Steam calls, game launches, GPU capture hooks or live resource access.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <api/include/dx12/ffx_api_dx12.h>
#include <framegeneration/include/dx12/ffx_api_framegeneration_dx12.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include "chain.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static const UINT W=128,H=96;
#include "probe-gpu.h"
#pragma pack(push,1)
struct Header {UINT64 magic;UINT version,size,w,h,rw,rh,flags,transfer;float ms,scale[2],jitter[2];UINT pitch[7],frameSize,reserved;};
#pragma pack(pop)
static_assert(sizeof(Header)==96,"replay header ABI");
struct Input {Header h{};ChainFrame frame{};BYTE *depth{},*motion{},*scene{};};
static HANDLE open(const wchar_t *dir,const wchar_t *name,DWORD access,DWORD creation){
 wchar_t p[32768];if(wcslen(dir)+wcslen(name)+2>=32768)return INVALID_HANDLE_VALUE;
 wcscpy(p,dir);wcscat(p,L"\\");wcscat(p,name);return CreateFileW(p,access,FILE_SHARE_READ,nullptr,creation,0,nullptr);
}
#include "trace-device.h"
#include "trace-command-list.h"
static bool read(HANDLE f,void *p,DWORD size){DWORD done;return ReadFile(f,p,size,&done,nullptr)&&done==size;}
static BYTE *pixels(const wchar_t *dir,const wchar_t *name,UINT w,UINT h,UINT pitch){
 if(!w||!h||w>16384||h>16384||pitch<w*4||pitch>131072)return nullptr;
 const SIZE_T bytes=SIZE_T(pitch)*h;if(bytes>256*1024*1024)return nullptr;
 HANDLE f=open(dir,name,GENERIC_READ,OPEN_EXISTING);if(f==INVALID_HANDLE_VALUE)return nullptr;
 LARGE_INTEGER length{};if(!GetFileSizeEx(f,&length)||length.QuadPart!=(LONGLONG)bytes){CloseHandle(f);return nullptr;}
 auto raw=(BYTE*)HeapAlloc(GetProcessHeap(),0,bytes),tight=(BYTE*)HeapAlloc(GetProcessHeap(),0,SIZE_T(w)*h*4);
 bool ok=raw&&tight&&read(f,raw,(DWORD)bytes);CloseHandle(f);
 if(ok)for(UINT y=0;y<h;y++)memcpy(tight+SIZE_T(y)*w*4,raw+SIZE_T(y)*pitch,w*4);
 if(raw)HeapFree(GetProcessHeap(),0,raw);if(!ok&&tight){HeapFree(GetProcessHeap(),0,tight);tight=nullptr;}return tight;
}
static bool load(const wchar_t *dir,Input &v){
 HANDLE f=open(dir,L"replay-input.bin",GENERIC_READ,OPEN_EXISTING);if(f==INVALID_HANDLE_VALUE)return false;
 bool ok=read(f,&v.h,sizeof(v.h))&&read(f,&v.frame,sizeof(v.frame));CloseHandle(f);
 if(!ok||v.h.magic!=0x31594c5052484646ull||v.h.version!=1||v.h.size!=sizeof(v.h)||v.h.frameSize!=sizeof(v.frame))return false;
 v.depth=pixels(dir,L"depth.bin",v.h.rw,v.h.rh,v.h.pitch[0]);
 v.motion=pixels(dir,L"motion.bin",v.h.rw,v.h.rh,v.h.pitch[1]);
 v.scene=pixels(dir,L"scene.bin",v.h.w,v.h.h,v.h.pitch[2]);
 return v.depth&&v.motion&&v.scene;
}
static FfxApiResource res(Texture &t){return ffxApiGetResourceDX12(t.gpu,FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);}
int wmain(int argc,wchar_t **argv){
 setvbuf(stdout,nullptr,_IONBF,0);CHECK(argc>=5 && argc<=8);bool tight=false,trace=false,maskControl=false;for(int i=5;i<argc;i++){if(!wcscmp(argv[i],L"tight-max-render"))tight=true;else if(!wcscmp(argv[i],L"trace"))trace=true;else if(!wcscmp(argv[i],L"mask-control"))maskControl=true;else CHECK(false);}Input in[2];CHECK(load(argv[2],in[0]));CHECK(load(argv[3],in[1]));
 const auto &a=in[0].h,&b=in[1].h;CHECK(a.w==b.w&&a.h==b.h&&a.rw==b.rw&&a.rh==b.rh&&a.flags==b.flags);
 CHECK(in[1].frame.frame==in[0].frame.frame+1);CHECK(a.flags==FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED);
 GPU g;CHECK(g.init());
 HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);CHECK(dll);
 auto create=(PfnFfxCreateContext)GetProcAddress(dll,"ffxCreateContext");auto destroy=(PfnFfxDestroyContext)GetProcAddress(dll,"ffxDestroyContext");
 auto configure=(PfnFfxConfigure)GetProcAddress(dll,"ffxConfigure");auto dispatch=(PfnFfxDispatch)GetProcAddress(dll,"ffxDispatch");CHECK(create&&destroy&&configure&&dispatch);
 TraceDevice tracedDevice;tracedDevice.real=g.d;
 ffxCreateBackendDX12Desc backend{{FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12},trace?static_cast<ID3D12Device*>(&tracedDevice):g.d};
 const UINT maxW=tight?a.rw:a.w,maxH=tight?a.rh:a.h;
 printf("Replay maxRender=%ux%u actualRender=%ux%u\n",maxW,maxH,a.rw,a.rh);
 ffxCreateContextDescFrameGeneration cd{{FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&backend.header},a.flags,{a.w,a.h},{maxW,maxH},FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM};
 ffxContext context=nullptr;CHECK(create(&context,&cd.header,nullptr)==0);
 Texture depth,motion,scene,out;CHECK(depth.init(g.d,DXGI_FORMAT_R32G8X24_TYPELESS,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,a.rw,a.rh));
 CHECK(depth.rowBytes==a.rw*4);CHECK(motion.init(g.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,a.rw,a.rh));
 CHECK(scene.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,a.w,a.h));
 CHECK(out.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,a.w,a.h));
 TraceCommandList traced;traced.real=g.l;traced.device=g.d;traced.output=out.gpu;traced.shaders=&tracedDevice;traced.folder=argv[4];traced.maskControl=maskControl;CHECK(!maskControl||trace);
 BYTE *zeroMotion=(BYTE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,SIZE_T(a.rw)*a.rh*4);CHECK(zeroMotion);
 BYTE *output=(BYTE*)HeapAlloc(GetProcessHeap(),0,SIZE_T(a.w)*a.h*4);CHECK(output);
 CHECK(!trace || (a.w==3472 && a.h==2266));
 for(UINT id=0;id<5;id++){
  const auto &v=in[id==4?1:0];const auto &h=v.h;const auto &f=v.frame;
  CHECK(g.begin());const auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  CHECK(depth.write(g.l,v.depth,h.rw*4,readable));CHECK(motion.write(g.l,id<4?zeroMotion:v.motion,h.rw*4,readable));CHECK(scene.write(g.l,v.scene,h.w*4,readable));out.transition(g.l,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ffxConfigureDescFrameGeneration cfg{};cfg.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;cfg.frameID=id;cfg.frameGenerationEnabled=true;cfg.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;cfg.HUDLessColor=res(scene);cfg.generationRect={0,0,(int)h.w,(int)h.h};CHECK(configure(&context,&cfg.header)==0);
  ffxDispatchDescFrameGenerationPrepareCameraInfo camera{};camera.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
  memcpy(camera.cameraPosition,f.cameraBasis,12);memcpy(camera.cameraUp,f.cameraBasis+3,12);memcpy(camera.cameraRight,f.cameraBasis+6,12);memcpy(camera.cameraForward,f.cameraBasis+9,12);
  ffxDispatchDescFrameGenerationPrepare prep{};prep.header={FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE,&camera.header};prep.frameID=id;prep.commandList=trace?static_cast<ID3D12GraphicsCommandList*>(&traced):g.l;prep.renderSize={h.rw,h.rh};prep.motionVectorScale={h.scale[0],h.scale[1]};prep.jitterOffset={h.jitter[0],h.jitter[1]};prep.frameTimeDelta=h.ms;prep.cameraNear=f.nearPlane;prep.cameraFar=f.farPlane;prep.cameraFovAngleVertical=f.fov;prep.viewSpaceToMetersFactor=0;prep.depth=res(depth);prep.motionVectors=res(motion);CHECK(dispatch(&context,&prep.header)==0);
  ffxDispatchDescFrameGeneration fg{};fg.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;fg.frameID=id;fg.commandList=trace?static_cast<ID3D12GraphicsCommandList*>(&traced):g.l;fg.numGeneratedFrames=1;fg.reset=id==0;fg.presentColor=res(scene);fg.outputs[0]=ffxApiGetResourceDX12(out.gpu,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);fg.generationRect={0,0,(int)h.w,(int)h.h};fg.backbufferTransferFunction=h.transfer;fg.minMaxLuminance[1]=100;
  if(id==4 && trace){traced.enabled=true;traced.state=out.state;fg.commandList=&traced;}
  CHECK(dispatch(&context,&fg.header)==0);out.state=traced.state;out.copyOut(g.l);CHECK(g.end());CHECK(out.read(output,h.w*4));
  if(id>=3){const wchar_t *name=id==3?L"warmup-generated.bin":L"replay-generated.bin";HANDLE file=open(argv[4],name,GENERIC_WRITE,CREATE_NEW);CHECK(file!=INVALID_HANDLE_VALUE);DWORD done;CHECK(WriteFile(file,output,h.w*h.h*4,&done,nullptr)&&done==h.w*h.h*4);CloseHandle(file);}
  printf("GPU completed id=%u captured_source_frame=%llu reset=%u size=%ux%u\n",id,(unsigned long long)f.frame,id==0,h.w,h.h);
 }
 CHECK(!maskControl||traced.maskApplied);CHECK(traced.save(argv[4]));CHECK(destroy(&context,nullptr)==0);puts("Completed two-image dataset replay with four warmup submissions. Cold history is reconstructed; not an exact reproduction of the game's long history.");return 0;
}
