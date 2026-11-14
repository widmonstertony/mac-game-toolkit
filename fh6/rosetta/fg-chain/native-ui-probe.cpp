// Real official FFX 3.1 GPU contrast, not a mock shader/provider success code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <api/include/dx12/ffx_api_dx12.h>
#include <framegeneration/include/dx12/ffx_api_framegeneration_dx12.h>
#include <sl.h>
#include <sl_consts.h>
#include "native-ui.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static const UINT W=128,H=96;
struct Token:sl::FrameToken{uint32_t id;Token(uint32_t i):id(i){}operator uint32_t()const override{return id;}};
__attribute__((noinline)) static FARPROC resolve(HMODULE m,const char *name){return GetProcAddress(m,name);}
#include "probe-gpu.h"
static ChainFrame frame(uint64_t id,Texture &scene,Texture &ui,UINT w=W,UINT h=H){ChainFrame f{};f.frame=id;f.viewport=0;f.tagsMask=12;
 for(int k=2;k<=3;k++){auto &r=f.tags[k];r.native=(uint64_t)(k==2?scene.gpu:ui.gpu);r.lifecycle=1;r.format=DXGI_FORMAT_R8G8B8A8_UNORM;r.width=w;r.height=h;r.state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}
 return f;}
static FfxApiResource res(Texture &t){return ffxApiGetResourceDX12(t.gpu,FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);}
static int mathTest(GPU &g){
 const UINT w=127,h=95;Texture scene,ui,out;CHECK(scene.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,w,h));CHECK(ui.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,w,h));CHECK(out.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,w,h));
 BYTE a[w*h*4],b[w*h*4],read[w*h*4];
 for(UINT i=0;i<w*h;i++){a[i*4]=40;a[i*4+1]=80;a[i*4+2]=120;a[i*4+3]=255;const BYTE alpha=(i%3==0?0:i%3==1?128:255);b[i*4]=alpha/4;b[i*4+1]=alpha/2;b[i*4+2]=alpha/3;b[i*4+3]=alpha;}
 CHECK(g.begin());CHECK(scene.write(g.l,a,w*4,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));CHECK(ui.write(g.l,b,w*4,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));CHECK(out.write(g.l,a,w*4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
 NativeUiCompositor c;const bool initialized=c.initialize(g.d);printf("compositor pipeline stage=%s status=%08x\n",c.stage,(unsigned)c.status);CHECK(initialized);auto f=frame(10,scene,ui,w,h);ffxDispatchDescFrameGeneration p{};p.frameID=10;p.numGeneratedFrames=1;p.commandList=g.l;p.presentColor=res(scene);p.outputs[0]=ffxApiGetResourceDX12(out.gpu,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);p.generationRect={0,0,(int)w,(int)h};
 auto binding=c.prepare(f,res(scene),p);CHECK(binding);CHECK(c.prepare(f,res(scene),p)==binding);
 auto invalid=f;invalid.frame=9;CHECK(!c.prepare(invalid,res(scene),p));invalid=f;invalid.tags[3].lifecycle=0;CHECK(!c.prepare(invalid,res(scene),p));invalid=f;invalid.tags[3].left=1;CHECK(!c.prepare(invalid,res(scene),p));invalid=f;invalid.tags[3].format=DXGI_FORMAT_R16G16B16A16_FLOAT;CHECK(!c.prepare(invalid,res(scene),p));
 c.record(g.l,binding);out.copyOut(g.l);CHECK(g.end());CHECK(out.read(read,w*4));unsigned errors=0;
 for(UINT i=0;i<w*h;i++)for(UINT k=0;k<3;k++){const float expected=b[i*4+k]+(1.f-b[i*4+3]/255.f)*a[i*4+k];if(fabsf(read[i*4+k]-expected)>1.01f)errors++;}
 CHECK(errors==0);printf("PASS native premultiplied UI real GPU pixels=%u errors=%u alpha=0/128/255 odd-size=%ux%u; stale/lifetime/offset/format guards\n",w*h,errors,w,h);c.release();return 0;
}
static int nativeDepthCopyTest(GPU &g) {
 Texture depth;CHECK(depth.init(g.d,DXGI_FORMAT_R32G8X24_TYPELESS,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
 const UINT rowBytes=(UINT)depth.rowBytes;
 printf("Depth copy layout: format=%u rowPitch=%u rowBytes=%u total=%llu\n",(unsigned)depth.fp.Footprint.Format,depth.fp.Footprint.RowPitch,rowBytes,(unsigned long long)depth.total);
 CHECK(rowBytes==W*4 || rowBytes==W*8);
 BYTE data[W*H*8]{},read[W*H*8]{};
 for(UINT i=0;i<W*H;i++){const float value=float(i%101)/101.f;memcpy(data+i*(rowBytes/W),&value,4);}
 CHECK(g.begin());CHECK(depth.write(g.l,data,rowBytes,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));depth.copyOut(g.l);CHECK(g.end());CHECK(depth.read(read,rowBytes));
 unsigned errors=0;for(UINT i=0;i<W*H;i++){float a,b;memcpy(&a,data+i*(rowBytes/W),4);memcpy(&b,read+i*(rowBytes/W),4);if(a!=b)errors++;}
 printf("Native game depth format19 plane0 readback: copyFormat=%u pixelStride=%u errors=%u pixels=%u\n",(unsigned)depth.fp.Footprint.Format,rowBytes/W,errors,W*H);CHECK(errors==0);return 0;
}
int wmain(int argc,wchar_t **argv){
 setvbuf(stdout,nullptr,_IONBF,0);CHECK(argc==3 || (argc==4 && !wcscmp(argv[3],L"--missing-ui")));
 const bool missingUi=argc==4;
 GPU g;CHECK(g.init());CHECK(mathTest(g)==0);CHECK(nativeDepthCopyTest(g)==0);
 HMODULE observer=LoadLibraryW(L"fh6-fg-chain.asi");CHECK(observer);
 auto init=(void(*)())GetProcAddress(observer,"InitializeASI");CHECK(init);init();
 HMODULE slFixture=LoadLibraryW(L"sl.interposer.dll");CHECK(slFixture);
 auto nativeConstants=(PFun_slSetConstants*)resolve(slFixture,"slSetConstants");
 auto nativeTags=(PFun_slSetTagForFrame*)resolve(slFixture,"slSetTagForFrame");CHECK(nativeConstants&&nativeTags);
 HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);CHECK(dll);
 auto create=(PfnFfxCreateContext)GetProcAddress(dll,"ffxCreateContext");auto destroy=(PfnFfxDestroyContext)GetProcAddress(dll,"ffxDestroyContext");auto query=(PfnFfxQuery)GetProcAddress(dll,"ffxQuery");auto configure=(PfnFfxConfigure)GetProcAddress(dll,"ffxConfigure");auto dispatch=(PfnFfxDispatch)GetProcAddress(dll,"ffxDispatch");CHECK(create&&destroy&&query&&configure&&dispatch);
 HMODULE candidate=LoadLibraryExW(argv[2],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);CHECK(candidate);
 auto candidateCreate=(PfnFfxCreateContext)GetProcAddress(candidate,"ffxCreateContext");auto candidateDestroy=(PfnFfxDestroyContext)GetProcAddress(candidate,"ffxDestroyContext");auto candidateConfigure=(PfnFfxConfigure)GetProcAddress(candidate,"ffxConfigure");auto candidateDispatch=(PfnFfxDispatch)GetProcAddress(candidate,"ffxDispatch");CHECK(candidateCreate&&candidateDestroy&&candidateConfigure&&candidateDispatch);
 HWND window=CreateWindowExW(0,L"STATIC",L"FH6 isolated GPU validation",WS_OVERLAPPEDWINDOW,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(window);
 IDXGIFactory1 *factory=nullptr;CHECK(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory1,(void**)&factory)));
 IDXGISwapChain4 *swapchain=nullptr;ffxContext swapContext=nullptr;
 DXGI_SWAP_CHAIN_DESC1 sc{};sc.Width=W;sc.Height=H;sc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sc.SampleDesc.Count=1;sc.BufferCount=3;sc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 sw{};sw.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;sw.swapchain=&swapchain;sw.hwnd=window;sw.desc=&sc;sw.dxgiFactory=factory;sw.gameQueue=g.q;
 CHECK(candidateCreate(&swapContext,&sw.header,nullptr)==0);CHECK(swapchain);
 uint64_t count=8,ids[8]{};const char *names[8]{};ffxQueryDescGetVersions versions{{FFX_API_QUERY_DESC_TYPE_GET_VERSIONS},FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,g.d,&count,ids,names};CHECK(query(nullptr,&versions.header)==0&&count==1);printf("REAL provider=%s\n",names[0]);
 Texture depth,motion,scene,ui,present,outputs[3];CHECK(depth.init(g.d,DXGI_FORMAT_R32_FLOAT));CHECK(motion.init(g.d,DXGI_FORMAT_R16G16_FLOAT));CHECK(scene.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM));CHECK(ui.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM));CHECK(present.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM));for(auto &o:outputs)CHECK(o.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
 ffxContext contexts[3]{};NativeUiCompositor compositors[3];
 // Match the startup lifecycle: create a 10-bit context, configure FG OFF,
 // then destroy it. The candidate must not create a compositor pipeline here.
 {
  ffxCreateBackendDX12Desc backend{{FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12},g.d};
  ffxCreateContextDescFrameGeneration boot{{FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&backend.header},0,{W,H},{W,H},FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM};
  ffxContext bootContext=nullptr;CHECK(candidateCreate(&bootContext,&boot.header,nullptr)==0);
  ffxConfigureDescFrameGeneration off{};off.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;off.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
  CHECK(candidateConfigure(&bootContext,&off.header)==0);CHECK(candidateDestroy(&bootContext,nullptr)==0);
  puts("PASS 10-bit opening context with FG disabled forwarded without native UI initialization");
 }
 for(int k=0;k<3;k++){
  ffxOverrideVersion selected{{FFX_API_DESC_TYPE_OVERRIDE_VERSION},ids[0]};ffxCreateBackendDX12Desc backend{{FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12,&selected.header},g.d};
  ffxCreateContextDescFrameGeneration cd{{FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&backend.header},FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED,{W,H},{W,H},FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM};
  CHECK((k==2?candidateCreate:create)(&contexts[k],&cd.header,nullptr)==0);CHECK(compositors[k].initialize(g.d));
 }
 BYTE source[W*H*4],overlay[W*H*4],final[W*H*4],zero[W*H*4]{};float z[W*H];uint16_t mv[W*H*2];BYTE pixels[3][W*H*4]{};
 auto historyActive=(unsigned(*)())resolve(candidate,"FH6HistoryActive");
 if(historyActive){
  // The game genuinely enables FG on loading screens with no scene/UI tags.
  // Disabled-FG boot coverage alone did not expose this startup condition.
  for(UINT i=0;i<W*H;i++){source[i*4]=25;source[i*4+1]=40;source[i*4+2]=60;source[i*4+3]=255;z[i]=.3f;mv[i*2]=mv[i*2+1]=0;}
  for(UINT64 id=500;id<520;id++){
   CHECK(g.begin());const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
   CHECK(depth.write(g.l,(BYTE*)z,W*4,read)&&motion.write(g.l,(BYTE*)mv,W*4,read)&&scene.write(g.l,source,W*4,read));CHECK(outputs[2].write(g.l,zero,W*4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
   Token token((uint32_t)id);sl::ViewportHandle view(0);sl::Constants c{};c.depthInverted=sl::Boolean::eTrue;c.cameraMotionIncluded=sl::Boolean::eTrue;c.motionVectors3D=sl::Boolean::eFalse;c.reset=sl::Boolean::eFalse;float identity[16]{};for(int k=0;k<16;k++)identity[k]=k%5==0?1.f:0.f;memcpy(&c.clipToPrevClip,identity,64);CHECK(nativeConstants(c,token,view)==sl::Result::eOk);
   sl::ResourceTag tags[]={sl::ResourceTag(nullptr,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eOnlyValidNow),sl::ResourceTag(nullptr,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eOnlyValidNow)};CHECK(nativeTags(token,view,tags,2,nullptr)==sl::Result::eOk);
   ffxConfigureDescFrameGeneration cfg{};cfg.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;cfg.frameID=id;cfg.frameGenerationEnabled=true;cfg.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;CHECK(candidateConfigure(&contexts[2],&cfg.header)==0);
   ffxDispatchDescFrameGenerationPrepareV2 prep{};prep.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;prep.frameID=id;prep.commandList=g.l;prep.renderSize={W,H};prep.motionVectorScale={1,1};prep.frameTimeDelta=16.6667f;prep.reset=id==500;prep.cameraNear=.1f;prep.cameraFar=50000;prep.cameraFovAngleVertical=1.05f;prep.viewSpaceToMetersFactor=1;prep.depth=res(depth);prep.motionVectors=res(motion);prep.cameraUp[1]=prep.cameraRight[0]=prep.cameraForward[2]=1;CHECK(candidateDispatch(&contexts[2],&prep.header)==0);
   ffxDispatchDescFrameGeneration fg{};fg.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;fg.frameID=id;fg.commandList=g.l;fg.numGeneratedFrames=1;fg.reset=id==500;fg.presentColor=res(scene);fg.outputs[0]=ffxApiGetResourceDX12(outputs[2].gpu,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);fg.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;CHECK(candidateDispatch(&contexts[2],&fg.header)==0);CHECK(g.end());
  }
  printf("Enabled FG loading screen: owned history pipelines=%u expected=0\n",historyActive());CHECK(historyActive()==0);
 }
 for(UINT64 id=0;id<12;id++){
  for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){
   const UINT i=y*W+x;const bool car=x>=20+id*2&&x<52+id*2&&y>=40&&y<72;
   source[i*4]=car?100:25;source[i*4+1]=car?30:40;source[i*4+2]=car?20:60;source[i*4+3]=255;
   z[i]=car?.8f:.2f;mv[i*2]=car?0xc000:0;mv[i*2+1]=0;
   const BYTE alpha=(y<12&&x<40)?128:0;overlay[i*4]=alpha/2;overlay[i*4+1]=alpha/4;overlay[i*4+2]=alpha/8;overlay[i*4+3]=alpha;
   for(UINT k=0;k<3;k++){const float value=source[i*4+k]*(car?1.2f:1.f);final[i*4+k]=(BYTE)(overlay[i*4+k]+(1-alpha/255.f)*value+.5f);}final[i*4+3]=255;
  }
  CHECK(g.begin());const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  CHECK(depth.write(g.l,(BYTE*)z,W*4,read));CHECK(motion.write(g.l,(BYTE*)mv,W*4,read));CHECK(scene.write(g.l,source,W*4,read));CHECK(ui.write(g.l,overlay,W*4,read));CHECK(present.write(g.l,final,W*4,read));
  Token token((uint32_t)id);sl::ViewportHandle viewport(0);sl::Constants constants{};constants.cameraNear=.1f;constants.cameraFar=50000;constants.depthInverted=sl::Boolean::eTrue;constants.cameraMotionIncluded=sl::Boolean::eTrue;constants.motionVectors3D=sl::Boolean::eFalse;constants.reset=sl::Boolean::eFalse;
  float identity[16]{};for(int k=0;k<16;k++)identity[k]=k%5==0?1.f:0.f;memcpy(&constants.clipToPrevClip,identity,64);
  CHECK(nativeConstants(constants,token,viewport)==sl::Result::eOk);
  sl::Resource sceneTag(sl::ResourceType::eTex2d,scene.gpu,read),uiTag(sl::ResourceType::eTex2d,ui.gpu,read),depthTag(sl::ResourceType::eTex2d,depth.gpu,read),motionTag(sl::ResourceType::eTex2d,motion.gpu,read);
  sl::ResourceTag tags[]={sl::ResourceTag(&sceneTag,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent),sl::ResourceTag(&uiTag,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent),sl::ResourceTag(&depthTag,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent),sl::ResourceTag(&motionTag,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent)};
  CHECK(nativeTags(token,viewport,tags+(missingUi?2:0),missingUi?2:4,nullptr)==sl::Result::eOk);
  for(int k=0;k<3;k++){
   CHECK(outputs[k].write(g.l,zero,W*4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
   ffxConfigureDescFrameGeneration cfg{};cfg.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;cfg.frameID=id;cfg.frameGenerationEnabled=true;cfg.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;if(!missingUi || k==1)cfg.HUDLessColor=res(scene);cfg.generationRect={0,0,W,H};CHECK((k==2?candidateConfigure:configure)(&contexts[k],&cfg.header)==0);
   ffxDispatchDescFrameGenerationPrepareV2 prep{};prep.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;prep.frameID=id;prep.commandList=g.l;prep.renderSize={W,H};prep.motionVectorScale={1,1};prep.frameTimeDelta=16.6667f;prep.reset=id==0;prep.cameraNear=.1f;prep.cameraFar=50000;prep.cameraFovAngleVertical=1.05f;prep.viewSpaceToMetersFactor=1;prep.depth=res(depth);prep.motionVectors=res(motion);prep.cameraUp[1]=prep.cameraRight[0]=prep.cameraForward[2]=1;CHECK((k==2?candidateDispatch:dispatch)(&contexts[k],&prep.header)==0);
   ffxDispatchDescFrameGeneration p{};p.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;p.commandList=g.l;p.frameID=id;p.numGeneratedFrames=1;p.reset=id==0;p.presentColor=res(k==1?scene:present);p.outputs[0]=ffxApiGetResourceDX12(outputs[k].gpu,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);p.generationRect={0,0,W,H};p.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;p.minMaxLuminance[1]=100;
   auto f=frame(id,scene,ui);NativeUiCompositor::Binding *binding=nullptr;
   if(k>0){binding=compositors[k].prepare(f,res(scene),p);CHECK(binding);}
   // Reference explicitly interpolates scene-only, candidate takes the same
   // same-frame validated native scene instead of difference-extracted final.
   const auto original=p;
   CHECK((k==2?candidateDispatch:dispatch)(&contexts[k],&p.header)==0);
   CHECK(!memcmp(&original,&p,sizeof(p)));
   if(k==1)compositors[k].record(g.l,binding);
   outputs[k].copyOut(g.l);
  }
  CHECK(g.end());for(int k=0;k<3;k++)CHECK(outputs[k].read(pixels[k],W*4));
 }
 unsigned refErrors=0,oldDiff=0,generatedDifferent=0,uiErrors=0;
 for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){const UINT i=y*W+x;for(UINT k=0;k<3;k++){
  if(abs(int(pixels[1][i*4+k])-int(pixels[2][i*4+k]))>1)refErrors++;
  if(y>=36&&y<76&&abs(int(pixels[0][i*4+k])-int(pixels[2][i*4+k]))>2)oldDiff++;
 if(y>=36&&y<76&&abs(int(source[i*4+k])-int(pixels[2][i*4+k]))>2)generatedDifferent++;
  if(y<12&&x<40){const float expected=overlay[i*4+k]+(1-overlay[i*4+3]/255.f)*source[i*4+k];if(fabsf(pixels[2][i*4+k]-expected)>2)uiErrors++;}
 }}
 printf("GPU contrast: reference_errors=%u non_ui_difference_components=%u generated_differs_from_base_components=%u ui_errors=%u frames=12 contexts=3\n",refErrors,oldDiff,generatedDifferent,uiErrors);
 if(missingUi) CHECK(!memcmp(pixels[0],pixels[2],sizeof(pixels[0])));
 else {CHECK(refErrors==0);CHECK(oldDiff>100);CHECK(generatedDifferent>0);CHECK(uiErrors==0);}
 for(int k=0;k<3;k++){CHECK((k==2?candidateDestroy:destroy)(&contexts[k],nullptr)==0);compositors[k].release();}
 swapchain->Release();CHECK(candidateDestroy(&swapContext,nullptr)==0);factory->Release();DestroyWindow(window);
 auto activeCaptures=(unsigned(*)())GetProcAddress(candidate,"FH6CaptureActive");CHECK(activeCaptures);
 const auto deadline=GetTickCount64()+10000;while(activeCaptures() && GetTickCount64()<deadline)Sleep(10);CHECK(activeCaptures()==0);
 if(missingUi) puts("PASS missing HUDless/UI passthrough matches the original provider byte for byte");
 else puts("PASS official FFX generated output + native UI; postprocess difference outside native UI triggers old extraction; candidate matches scene-only reference. This is synthetic GPU evidence, not proof FH6 car ghosting is fixed.");
 return 0;
}
