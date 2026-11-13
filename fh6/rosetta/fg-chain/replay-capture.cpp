// Replay captured game pixels through the unchanged official FG provider.
// No Steam calls, game launches, GPU capture hooks or live resource access.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <sl.h>
#include <sl_consts.h>
#include <cstdio>
#include <cstring>
#include <api/include/dx12/ffx_api_dx12.h>
#include <framegeneration/include/dx12/ffx_api_framegeneration_dx12.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include "chain.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
__attribute__((noinline)) static FARPROC resolve(HMODULE m,const char *name){return GetProcAddress(m,name);}
static const UINT W=128,H=96;
struct Token:sl::FrameToken{uint32_t id;Token(uint32_t i):id(i){}operator uint32_t()const override{return id;}};
#include "probe-gpu.h"
#include "replay-input.h"
#include "trace-device.h"
#include "trace-command-list.h"
static FfxApiResource res(Texture &t){return ffxApiGetResourceDX12(t.gpu,FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);}
int wmain(int argc,wchar_t **argv){
 setvbuf(stdout,nullptr,_IONBF,0);CHECK(argc>=5 && argc<=9);bool tight=false,trace=false,maskControl=false,provider=false;for(int i=5;i<argc;i++){if(!wcscmp(argv[i],L"tight-max-render"))tight=true;else if(!wcscmp(argv[i],L"trace"))trace=true;else if(!wcscmp(argv[i],L"mask-control"))maskControl=true;else if(!wcscmp(argv[i],L"provider"))provider=true;else CHECK(false);}Input in[2];CHECK(load(argv[2],in[0]));CHECK(load(argv[3],in[1]));
 const auto &a=in[0].h,&b=in[1].h;CHECK(a.w==b.w&&a.h==b.h&&a.rw==b.rw&&a.rh==b.rh&&a.flags==b.flags);
 CHECK(in[1].frame.frame==in[0].frame.frame+1);CHECK(a.flags==FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED);
 GPU g;CHECK(g.init());
 HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);CHECK(dll);
 auto create=(PfnFfxCreateContext)GetProcAddress(dll,"ffxCreateContext");auto destroy=(PfnFfxDestroyContext)GetProcAddress(dll,"ffxDestroyContext");
 auto configure=(PfnFfxConfigure)GetProcAddress(dll,"ffxConfigure");auto dispatch=(PfnFfxDispatch)GetProcAddress(dll,"ffxDispatch");CHECK(create&&destroy&&configure&&dispatch);
 CHECK(!provider||(!trace&&!maskControl));
 PFun_slSetConstants *setConstants=nullptr;PFun_slSetTagForFrame *setTags=nullptr;ffxContext swapContext=nullptr;IDXGISwapChain4 *swapchain=nullptr;IDXGIFactory1 *factory=nullptr;HWND window=nullptr;
 if(provider){
  HMODULE observer=LoadLibraryW(L"fh6-fg-chain.asi");CHECK(observer);auto initialize=(void(*)())GetProcAddress(observer,"InitializeASI");CHECK(initialize);initialize();HMODULE sl=LoadLibraryW(L"sl.interposer.dll");CHECK(sl);setConstants=(PFun_slSetConstants*)resolve(sl,"slSetConstants");setTags=(PFun_slSetTagForFrame*)resolve(sl,"slSetTagForFrame");CHECK(setConstants&&setTags);
  window=CreateWindowExW(0,L"STATIC",L"Owned FH6 provider replay",WS_OVERLAPPEDWINDOW,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(window);CHECK(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory1,(void**)&factory)));
  DXGI_SWAP_CHAIN_DESC1 sc{};sc.Width=W;sc.Height=H;sc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sc.SampleDesc.Count=1;sc.BufferCount=3;sc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 sw{};sw.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;sw.swapchain=&swapchain;sw.hwnd=window;sw.desc=&sc;sw.dxgiFactory=factory;sw.gameQueue=g.q;CHECK(create(&swapContext,&sw.header,nullptr)==0&&swapchain);
 }
 TraceDevice tracedDevice;tracedDevice.real=g.d;
 ffxCreateBackendDX12Desc backend{{FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12},trace?static_cast<ID3D12Device*>(&tracedDevice):g.d};
 const UINT maxW=tight?a.rw:a.w,maxH=tight?a.rh:a.h;
 printf("Replay maxRender=%ux%u actualRender=%ux%u\n",maxW,maxH,a.rw,a.rh);
 ffxCreateContextDescFrameGeneration cd{{FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&backend.header},a.flags,{a.w,a.h},{maxW,maxH},FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM};
 ffxContext context=nullptr;CHECK(create(&context,&cd.header,nullptr)==0);
 Texture depth,motion,scene,out,ui;CHECK(depth.init(g.d,DXGI_FORMAT_R32G8X24_TYPELESS,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,a.rw,a.rh));
 CHECK(depth.rowBytes==a.rw*4);CHECK(motion.init(g.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,a.rw,a.rh));
 CHECK(scene.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,a.w,a.h));
 CHECK(out.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,a.w,a.h));
 if(provider)CHECK(ui.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,a.w,a.h));
 TraceCommandList traced;traced.real=g.l;traced.device=g.d;traced.output=out.gpu;traced.shaders=&tracedDevice;traced.folder=argv[4];traced.maskControl=maskControl;CHECK(!maskControl||trace);
 BYTE *zeroMotion=(BYTE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,SIZE_T(a.rw)*a.rh*4);CHECK(zeroMotion);
 BYTE *output=(BYTE*)HeapAlloc(GetProcessHeap(),0,SIZE_T(a.w)*a.h*4);CHECK(output);
 CHECK(!trace || (a.w==3472 && a.h==2266));
 BYTE *zeroUI=provider?(BYTE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,SIZE_T(a.w)*a.h*4):nullptr;CHECK(!provider||zeroUI);
 for(UINT id=0;id<5;id++){
  const auto &v=in[id==4?1:0];const auto &h=v.h;const auto &f=v.frame;
  CHECK(g.begin());const auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  CHECK(depth.write(g.l,v.depth,h.rw*4,readable));CHECK(motion.write(g.l,id<4?zeroMotion:v.motion,h.rw*4,readable));CHECK(scene.write(g.l,v.scene,h.w*4,readable));out.transition(g.l,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(provider){
   CHECK(ui.write(g.l,zeroUI,h.w*4,readable));Token token(id);sl::ViewportHandle view(0);sl::Constants c{};c.cameraNear=f.nearPlane;c.cameraFar=f.farPlane;c.cameraFOV=f.fov;c.cameraAspectRatio=f.aspect;c.depthInverted=sl::Boolean::eTrue;c.cameraMotionIncluded=sl::Boolean::eTrue;c.motionVectors3D=sl::Boolean::eFalse;c.reset=sl::Boolean::eFalse;c.jitterOffset={f.jitter[0],f.jitter[1]};c.mvecScale={f.scale[0],f.scale[1]};memcpy(&c.cameraViewToClip,f.projection,64);memcpy(&c.clipToPrevClip,f.clipToPrevious,64);if(id<4){float identity[16]{};for(int k=0;k<16;k++)identity[k]=k%5==0?1.f:0.f;memcpy(&c.clipToPrevClip,identity,64);}CHECK(setConstants(c,token,view)==sl::Result::eOk);
   sl::Resource st(sl::ResourceType::eTex2d,scene.gpu,readable),ut(sl::ResourceType::eTex2d,ui.gpu,readable),dt(sl::ResourceType::eTex2d,depth.gpu,readable),mt(sl::ResourceType::eTex2d,motion.gpu,readable);
   sl::ResourceTag tags[]={sl::ResourceTag(&st,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent),sl::ResourceTag(&ut,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent),sl::ResourceTag(&dt,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent),sl::ResourceTag(&mt,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent)};CHECK(setTags(token,view,tags,4,nullptr)==sl::Result::eOk);
  }
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
 CHECK(!maskControl||traced.maskApplied);CHECK(traced.save(argv[4]));CHECK(destroy(&context,nullptr)==0);if(provider){swapchain->Release();CHECK(destroy(&swapContext,nullptr)==0);factory->Release();DestroyWindow(window);}puts("Completed two-image dataset replay with four warmup submissions. Cold history is reconstructed; not an exact reproduction of the game's long history.");return 0;
}
