// Isolated real-GPU PE -> Wine Unix -> MetalFX -> D3D12 staging test.
// No windows, input injection, game DLL injection or swapchain presentation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <cstdint>
#include <cstdarg>
#include <initializer_list>
#include "bridge-abi.h"
extern "C" void *memcpy(void *d,const void *s,size_t n){auto a=(volatile char*)d;auto b=(const volatile char*)s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
extern "C" void *memset(void *d,int v,size_t n){auto a=(volatile char*)d;for(size_t i=0;i<n;i++)a[i]=v;return d;}
static void log(const char *fmt,...){char b[1024];va_list a;va_start(a,fmt);using F=int(__cdecl*)(char*,size_t,const char*,va_list);static auto f=(F)GetProcAddress(LoadLibraryW(L"msvcrt.dll"),"_vsnprintf");if(!f)ExitProcess(90);f(b,sizeof(b)-1,fmt,a);va_end(a);b[sizeof(b)-1]=0;DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),b,lstrlenA(b),&n,nullptr);}
#define CHECK(x) do{if(!(x)){log("FAIL line=%d condition=%s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static constexpr UINT W=192,H=128,RW=96,RH=64;
#include "../../rosetta/fg-chain/probe-gpu.h"
struct StagedTexture:Texture {
 bool initStaged(ID3D12Device*d,DXGI_FORMAT fmt,UINT w,UINT h){
  if(!init(d,fmt,D3D12_RESOURCE_FLAG_NONE,w,h))return false;
  // Native Metal no-copy buffers require full host-page allocations. Recreate
  // both staging resources with 64 KiB-rounded storage, keeping D3D footprints.
  upload->Release();readback->Release();upload=readback=nullptr;
  total=(total+65535)&~UINT64(65535);
  D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=total;
  r.Height=r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
  if(FAILED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)&upload)))return false;
  hp.Type=D3D12_HEAP_TYPE_READBACK;
  return SUCCEEDED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&readback));
 }
 FxBuffer map(bool output,uint32_t fmt){
  CHECK(fp.Offset==0);void *p=nullptr;D3D12_RANGE range{0,output?0:(SIZE_T)total};
  CHECK(SUCCEEDED((output?upload:readback)->Map(0,&range,&p)));
  log("mapped format=%u output=%u ptr=%p bytes=%llu pitch=%u\n",fmt,output,p,total,fp.Footprint.RowPitch);
  CHECK((uint64_t(p)&16383)==0);
  return {uint64_t(p),total,fp.Footprint.Width,fp.Footprint.Height,fp.Footprint.RowPitch,fmt};
 }
 void uploadMapped(ID3D12GraphicsCommandList*l){
  transition(l,D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=upload;a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=fp;
  b.pResource=gpu;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&b,0,0,0,&a,nullptr);
 }
};
extern "C" void entry(){
 auto dll=LoadLibraryW(L"fh6metalfx.dll");CHECK(dll);
 auto call=(LONG(WINAPI*)(unsigned,void*))GetProcAddress(dll,"MetalFxBridgeCall");CHECK(call);
 uint64_t caps=0;CHECK(call(FX_CAPS,&caps)==0&&(caps&6)==6);log("caps=%llu\n",caps);
 GPU g;CHECK(g.init());
 for(const wchar_t* name:{L"d3d12.dll",L"d3d12core.dll",L"apd12.dll",L"fh6metalfx.dll"}){wchar_t path[1024]{};auto mod=GetModuleHandleW(name);if(mod)GetModuleFileNameW(mod,path,1024);log("module=%ls handle=%p path=%ls\n",name,mod,path);}
 HMODULE deviceModule{};auto deviceMethod=(*(void***)g.d)[0];GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(const char*)deviceMethod,&deviceModule);
 wchar_t devicePath[1024]{};if(deviceModule)GetModuleFileNameW(deviceModule,devicePath,1024);log("device_method=%p module=%p path=%ls\n",deviceMethod,deviceModule,devicePath);
 CHECK(deviceModule&&deviceModule==GetModuleHandleW(L"apd12.dll"));
 StagedTexture color,depth,motion,out;
#ifdef FX_TEST_UPSCALE_CHAIN
 constexpr UINT CW=RW,CH=RH;
 StagedTexture scaled;CHECK(scaled.initStaged(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,W,H));
 FxCreate sr{1,RW,RH,W,H,0,0};CHECK(call(FX_UPSCALE_CREATE,&sr)==0);
#else
 constexpr UINT CW=W,CH=H;
#endif
 CHECK(color.initStaged(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,CW,CH));CHECK(depth.initStaged(g.d,DXGI_FORMAT_R32_FLOAT,RW,RH));
 CHECK(motion.initStaged(g.d,DXGI_FORMAT_R16G16_FLOAT,RW,RH));CHECK(out.initStaged(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,W,H));
 FxCreate create{1,RW,RH,W,H,0,0};CHECK(call(FX_CREATE,&create)==0);
 FxDispatchCamera cp{};auto &p=cp.frame;p.context=create.context;
 p.color=color.map(false,FX_RGBA8);p.depth=depth.map(false,FX_R32F);p.motion=motion.map(false,FX_RG16F);p.output=out.map(true,FX_RGBA8);
#ifdef FX_TEST_UPSCALE_CHAIN
 FxUpscale u{};u.context=sr.context;u.color=p.color;u.depth=p.depth;u.motion=p.motion;
 u.output=scaled.map(true,FX_RGBA8);u.motionScaleX=float(RW)/W;u.motionScaleY=float(RH)/H;
 u.preExposure=u.exposure=1;u.depthReversed=1;p.color=scaled.map(false,FX_RGBA8);
#endif
 p.nearPlane=.1f;p.farPlane=1000;p.fovDegrees=60;p.aspectRatio=float(W)/H;p.deltaSeconds=1.f/60;p.depthReversed=1;p.motionScaleX=p.motionScaleY=1;
 cp.camera.flags=1;for(int i=0;i<4;i++)cp.camera.worldToView[i*5]=1;
 cp.camera.viewToClip[0]=1.732050807568877f/p.aspectRatio;cp.camera.viewToClip[5]=1.732050807568877f;
 cp.camera.viewToClip[10]=p.nearPlane/(p.nearPlane-p.farPlane);cp.camera.viewToClip[11]=1;
 cp.camera.viewToClip[14]=p.nearPlane*p.farPlane/(p.farPlane-p.nearPlane);
 auto c=(BYTE*)VirtualAlloc(nullptr,W*H*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 auto d=(float*)VirtualAlloc(nullptr,RW*RH*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 auto m=(uint16_t*)VirtualAlloc(nullptr,RW*RH*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 auto result=(BYTE*)VirtualAlloc(nullptr,W*H*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(c&&d&&m&&result);
 for(unsigned frame=0;frame<12;frame++){
  unsigned left=30+4*frame;
  for(unsigned y=0;y<CH;y++)for(unsigned x=0;x<CW;x++){unsigned ox=x*W/CW,oy=y*H/CH;bool s=ox>=left&&ox<left+32&&oy>=48&&oy<80;auto pixel=c+4*(y*CW+x);pixel[0]=pixel[1]=pixel[2]=s?255:8;pixel[3]=255;}
  for(unsigned y=0;y<RH;y++)for(unsigned x=0;x<RW;x++){bool s=2*x>=left&&2*x<left+32&&2*y>=48&&2*y<80;d[y*RW+x]=s?.9f:.1f;m[2*(y*RW+x)]=s?0xc400:0;m[2*(y*RW+x)+1]=0;}
  CHECK(g.begin());CHECK(color.write(g.l,c,CW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));CHECK(depth.write(g.l,(BYTE*)d,RW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));CHECK(motion.write(g.l,(BYTE*)m,RW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
  color.copyOut(g.l);depth.copyOut(g.l);motion.copyOut(g.l);CHECK(g.end());
#ifdef FX_TEST_UPSCALE_CHAIN
  u.frameId=frame+100;u.reset=frame==0;CHECK(call(FX_UPSCALE_DISPATCH,&u)==0);
  CHECK(g.begin());scaled.uploadMapped(g.l);scaled.copyOut(g.l);CHECK(g.end());
#endif
  p.frameId=frame+100;p.reset=frame==0;LONG status=call(FX_DISPATCH_CAMERA,&cp);log("dispatch=%ld frame=%u generated=%u gpu_ms=%.3f wall_ms=%.3f\n",status,frame,p.generated,p.gpuMilliseconds,p.wallMilliseconds);CHECK(status==0);CHECK(p.generated==(frame>=2));
  CHECK(g.begin());out.uploadMapped(g.l);out.copyOut(g.l);CHECK(g.end());CHECK(out.read(result,W*4));
  size_t count=0,sum=0;for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)if(result[4*(y*W+x)]>128){count++;sum+=x;}
  double center=count?double(sum)/count:-1,expected=left+(frame>=2?13.5:15.5),error=center-expected;
  log("D3D12 readback frame=%u count=%zu center=%.3f expected=%.3f fence=%llu\n",frame,count,center,expected,g.value);
#ifdef FX_TEST_UPSCALE_CHAIN
  CHECK(count>=900&&count<=1140&&error>-1.5&&error<1.5);
#else
  CHECK(count==1024&&error>-.15&&error<.15);
#endif
 }
 FxDestroy destroy{create.context};CHECK(call(FX_DESTROY,&destroy)==0);
 D3D12_RANGE noWrite{};color.readback->Unmap(0,&noWrite);depth.readback->Unmap(0,&noWrite);motion.readback->Unmap(0,&noWrite);out.upload->Unmap(0,nullptr);
#ifdef FX_TEST_UPSCALE_CHAIN
 FxDestroy ds{sr.context};CHECK(call(FX_UPSCALE_DESTROY,&ds)==0);
 scaled.readback->Unmap(0,&noWrite);scaled.upload->Unmap(0,nullptr);
 log("PASS temporal SR -> D3D12 resource -> MetalFX FG -> D3D12, 10 measured midpoints; no live presentation test\n");
#endif
 log("PASS PE/Unix camera ABI, D3D12 input fences, mapped MetalFX midpoint, D3D12 output fences and pixels; no presentation test\n");ExitProcess(0);
}
