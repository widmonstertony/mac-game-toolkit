// Isolated real D3DMetal probe. Upload/readback here are measurement only;
// AppleCommandFg itself only records to the caller's command list.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <cstring>
#include <cstdarg>
#include "apple-command-backend.h"
#include "apple-frame-inputs.h"
extern "C" void *memcpy(void *d,const void *s,size_t n){auto a=(volatile unsigned char*)d;auto b=(const volatile unsigned char*)s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
extern "C" void *memset(void *d,int v,size_t n){auto a=(volatile unsigned char*)d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)v;return d;}
static void report(const char *format,...){char b[2048];va_list ap;va_start(ap,format);
 using F=int(__cdecl*)(char*,size_t,const char*,va_list);static auto f=(F)GetProcAddress(LoadLibraryW(L"msvcrt.dll"),"_vsnprintf");
 if(!f)ExitProcess(99);f(b,sizeof(b)-1,format,ap);va_end(ap);b[sizeof(b)-1]=0;DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),b,lstrlenA(b),&n,nullptr);}
static constexpr UINT W=192,H=128;
#include "../../rosetta/fg-chain/probe-gpu.h"
static BYTE *alloc(size_t n){return (BYTE*)VirtualAlloc(nullptr,n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);}
static bool io(HANDLE h,void *p,DWORD n,bool write=false){DWORD done=0;return (write?WriteFile(h,p,n,&done,nullptr):ReadFile(h,p,n,&done,nullptr))&&done==n;}
extern "C" void entry(){
 static char path[2048]{};DWORD len=GetEnvironmentVariableA("FH6_COMMAND_REPLAY_INPUT",path,sizeof(path));
 HANDLE input=INVALID_HANDLE_VALUE;UINT w=W,h=H,rw=W,rh=H,count=12;
 if(len){if(len>=sizeof(path))ExitProcess(2);input=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  unsigned header[6];if(input==INVALID_HANDLE_VALUE||!io(input,header,sizeof(header))||header[0]!=0x31474641)ExitProcess(2);
  w=header[1];h=header[2];rw=header[3];rh=header[4];count=header[5];
  if(!w||!h||!rw||!rh||rw>w||rh>h||w>8192||h>8192||(count!=8&&count!=3))ExitProcess(2);}
 GPU gpu;if(!gpu.init())ExitProcess(3);AppleCommandFg backend;
 if(!backend.initialize(gpu.d,w,h,rw,rh)){report("FAIL initialize %s status=%x\n",backend.reason,backend.status);ExitProcess(4);}
 Texture color,depth,motion,out;char type[4]{};
 bool snapshots=GetEnvironmentVariableA("FH6_COMMAND_SNAPSHOTS",type,sizeof(type))&&type[0]=='1';
 AppleFrameInputs inputs;Texture poisonDepth,poisonMotion,checkDepth,checkMotion;
 bool typeless=GetEnvironmentVariableA("FH6_COMMAND_TYPELESS_DEPTH",type,sizeof(type))&&type[0]=='1';
 if(!color.init(gpu.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,w,h)||
 !depth.init(gpu.d,typeless?DXGI_FORMAT_R32G8X24_TYPELESS:DXGI_FORMAT_R32_FLOAT,typeless?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_NONE,rw,rh)||
 !motion.init(gpu.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,rw,rh)||
 !out.init(gpu.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,w,h))ExitProcess(5);
 auto c=alloc(w*h*4),m=alloc(rw*rh*4),d=alloc(rw*rh*4),o=alloc(w*h*4);if(!c||!m||!d||!o)ExitProcess(6);
 auto zero=alloc(rw*rh*4),verify=alloc(rw*rh*4);if(!zero||!verify)ExitProcess(6);
 if(snapshots){
  if(!inputs.initialize(gpu.d,gpu.q)||
   !poisonDepth.init(gpu.d,depth.gpu->GetDesc().Format,depth.gpu->GetDesc().Flags,rw,rh)||
   !checkDepth.init(gpu.d,depth.gpu->GetDesc().Format,depth.gpu->GetDesc().Flags,rw,rh)||
   !poisonMotion.init(gpu.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,rw,rh)||
   !checkMotion.init(gpu.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,rw,rh))ExitProcess(16);
 }
 char resetControl[4]{};bool resetTest=GetEnvironmentVariableA("FH6_COMMAND_RESET_CONTROL",resetControl,sizeof(resetControl))&&resetControl[0]=='1';
 unsigned pixelFailures=0,generated=0,nonEndpoint=0;ChainFrame f{};
 f.hasConstants=1;f.cameraMotionIncluded=1;f.nearPlane=.1f;f.farPlane=1000;f.fov=1.04719755f;f.aspect=float(w)/h;f.inverted=1;
 f.scale[0]=1.f/rw;f.scale[1]=1.f/rh;f.cameraBasis[4]=f.cameraBasis[6]=f.cameraBasis[11]=1;
 // Row-vector infinite reverse-Z perspective, consistent with the simple camera.
 f.projection[0]=1.7320508f/f.aspect;f.projection[5]=1.7320508f;f.projection[11]=1;f.projection[14]=.1f;
 static char outputRoot[2048]{};if(input!=INVALID_HANDLE_VALUE&&!GetEnvironmentVariableA("FH6_COMMAND_REPLAY_OUTPUT",outputRoot,sizeof(outputRoot)))ExitProcess(6);
 for(unsigned n=0;n<count;++n){
  int left=30+n*4;f.frame=n+1;f.reset=resetTest&&n==6;
  if(input!=INVALID_HANDLE_VALUE){if(!io(input,&f,sizeof(f))||!io(input,c,w*h*4)||!io(input,d,rw*rh*4)||!io(input,m,rw*rh*4))ExitProcess(7);}
  else for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){unsigned k=y*w+x;bool square=x>=left&&x<left+32&&y>=48&&y<80;
   c[k*4]=c[k*4+1]=c[k*4+2]=square?255:8;c[k*4+3]=255;
   ((float*)d)[k]=square?.9f:.1f;((unsigned short*)m)[k*2]=square?0xc400:0;((unsigned short*)m)[k*2+1]=0;}
  if(snapshots&&!inputs.retireReusedOutput(out.gpu,f.frame))ExitProcess(17);
  if(!gpu.begin()||!color.write(gpu.l,c,w*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||
   !depth.write(gpu.l,d,rw*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||
   !motion.write(gpu.l,m,rw*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))ExitProcess(8);
  ID3D12Resource *chosenDepth=depth.gpu,*chosenMotion=motion.gpu;AppleFrameInputs::Slot *slot=nullptr;
  if(snapshots){
   slot=inputs.prepare(gpu.l,f,depth.gpu,motion.gpu,rw,rh,depth.state,motion.state);
   if(!slot){report("FAIL snapshot %s status=%x\n",inputs.reason,inputs.status);ExitProcess(18);}
   if(inputs.prepare(gpu.l,f,depth.gpu,motion.gpu,rw,rh,depth.state,motion.state))ExitProcess(19);
   chosenDepth=slot->depth;chosenMotion=slot->motion;
   // Deliberately overwrite the game's resources AFTER Prepare, before FG.
   // Separate upload resources avoid accidentally changing the earlier upload.
   if(!poisonDepth.write(gpu.l,zero,rw*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||
    !poisonMotion.write(gpu.l,zero,rw*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))ExitProcess(20);
   AppleFrameInputs::copy(gpu.l,poisonDepth.gpu,depth.gpu,poisonDepth.state);
   AppleFrameInputs::copy(gpu.l,poisonMotion.gpu,motion.gpu,poisonMotion.state);
   checkDepth.transition(gpu.l,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   checkMotion.transition(gpu.l,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   AppleFrameInputs::copy(gpu.l,chosenDepth,checkDepth.gpu,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   AppleFrameInputs::copy(gpu.l,chosenMotion,checkMotion.gpu,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   checkDepth.copyOut(gpu.l);checkMotion.copyOut(gpu.l);depth.copyOut(gpu.l);motion.copyOut(gpu.l);
  }
  out.transition(gpu.l,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);bool midpoint=false;
  if(!backend.record(gpu.l,f,color.gpu,chosenDepth,chosenMotion,out.gpu,midpoint)){report("FAIL frame=%llu %s status=%x\n",f.frame,backend.reason,backend.status);ExitProcess(9);}
  if(snapshots&&!inputs.recorded(slot,out.gpu))ExitProcess(21);
  out.copyOut(gpu.l);if(!gpu.end()||!out.read(o,w*4))ExitProcess(10);
  if(snapshots){
   if(!checkDepth.read(verify,rw*4)||memcmp(verify,d,rw*rh*4)||
      !checkMotion.read(verify,rw*4)||memcmp(verify,m,rw*rh*4)||
      !depth.read(verify,rw*4)||memcmp(verify,zero,rw*rh*4)||
      !motion.read(verify,rw*4)||memcmp(verify,zero,rw*rh*4))ExitProcess(22);
   report("snapshot frame=%llu exact_depth_motion=1 overwritten_originals=1\n",f.frame);
  }
  // Alpha normalization alone must never count as a synthesized picture.
  unsigned different=0;for(unsigned k=0;k<w*h*4;++k)if(k%4!=3)different+=o[k]!=c[k];
  if(midpoint){++generated;nonEndpoint+=different>0;}
  report("frame=%llu warmed=%u different_current_rgb_bytes=%u fence=%llu\n",f.frame,midpoint,different,gpu.value);
  if(input==INVALID_HANDLE_VALUE){unsigned area=0,sum=0;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)if(o[(y*w+x)*4]>128){++area;sum+=x;}
   const float centroid=area?float(sum)/area:-1,expected=left+15.5f-(midpoint?2.f:0.f);
   report("square area=%u centroid=%.3f expected=%.3f\n",area,centroid,expected);
   if(area!=1024||fabsf(centroid-expected)>.15f){if(!resetTest)ExitProcess(11);++pixelFailures;}
  }else{static char target[2300];using S=int(__cdecl*)(char*,size_t,const char*,...);auto s=(S)GetProcAddress(GetModuleHandleW(L"msvcrt.dll"),"_snprintf");
   s(target,sizeof(target),"%s\\frame-%llu.rgba",outputRoot,f.frame);
   auto file=CreateFileA(target,GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);if(file==INVALID_HANDLE_VALUE||!io(file,o,w*h*4,true))ExitProcess(12);CloseHandle(file);}
  // Reject repeated frame metadata before recording any native work.
  bool repeated;if(backend.record(gpu.l,f,color.gpu,depth.gpu,motion.gpu,out.gpu,repeated))ExitProcess(13);
 }
 if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
 if(snapshots)inputs.releaseAfterGpuCompletion();
 if(!backend.releaseAfterGpuCompletion())ExitProcess(14);
 if(generated!=count-(resetTest?5:2)||nonEndpoint!=generated)ExitProcess(15);
 if(pixelFailures){report("FAIL reset midpoint pixels mismatched=%u\n",pixelFailures);ExitProcess(11);}
 report("PASS frames=%u warmed=%u non_current=%u real_gpu_fences=%llu quality_verified=%s\n",count,generated,nonEndpoint,gpu.value,input==INVALID_HANDLE_VALUE?"synthetic_only":"false");ExitProcess(0);
}
