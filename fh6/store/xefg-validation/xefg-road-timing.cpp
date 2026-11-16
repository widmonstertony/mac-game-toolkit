#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "xess-fg-sdk/xefg_swapchain_d3d12.h"
#include "xess-fg-sdk/xell_d3d12.h"
static void output(const char *s) { DWORD n=0; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)lstrlenA(s), &n, nullptr); }
static int probe_printf(const char *format, ...) { char text[1024];va_list ap;va_start(ap,format);using F=int(__cdecl*)(char*,size_t,const char*,va_list);static auto f=(F)GetProcAddress(LoadLibraryW(L"msvcrt.dll"),"_vsnprintf");if(!f)return -1;int n=f(text,sizeof(text)-1,format,ap);va_end(ap);text[sizeof(text)-1]=0;output(text);return n;}
static int probe_puts(const char *s){output(s);output("\n");return 0;}
extern "C" void *memcpy(void *d,const void *s,size_t n){auto a=(volatile unsigned char*)d;auto b=(const volatile unsigned char*)s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
extern "C" void *memset(void *d,int v,size_t n){auto a=(volatile unsigned char*)d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)v;return d;}
#define printf probe_printf
#define puts probe_puts
static constexpr UINT W=3472,H=2266,RW=2314,RH=1510;
#include "../../rosetta/fg-chain/probe-gpu.h"

static GPU captureGpu;
static Texture captured;
static volatile LONG captureArmed, captureCount;
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
static PresentFn originalPresent;
static Present1Fn originalPresent1;
static void captureBuffer(IDXGISwapChain *base){
 if(!captureArmed)return;
 IDXGISwapChain3 *sc{};if(FAILED(base->QueryInterface(IID_IDXGISwapChain3,(void**)&sc)))return;
 ID3D12Resource* bb{};if(FAILED(sc->GetBuffer(sc->GetCurrentBackBufferIndex(),IID_ID3D12Resource,(void**)&bb))){sc->Release();return;}
 if(captureGpu.begin()){
  auto old=captured.gpu;captured.gpu=bb;captured.state=D3D12_RESOURCE_STATE_PRESENT;captured.copyOut(captureGpu.l);captured.gpu=old;
  if(captureGpu.end()){
   BYTE* pixels=(BYTE*)VirtualAlloc(nullptr,W*H*4,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
   if(pixels&&captured.read(pixels,W*4)){
    int n=(int)InterlockedIncrement(&captureCount);char path[256];using F=int(__cdecl*)(char*,const char*,...);auto fmt=(F)GetProcAddress(LoadLibraryW(L"msvcrt.dll"),"sprintf");fmt(path,"xefg-replay/capture-%d.rgba",n);
    HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);DWORD wrote=0;if(f!=INVALID_HANDLE_VALUE){WriteFile(f,pixels,W*H*4,&wrote,nullptr);CloseHandle(f);}printf("captured-present=%d bytes=%lu\n",n,wrote);
   }
   if(pixels)VirtualFree(pixels,0,MEM_RELEASE);
  }else puts("capture-fence-failed");
 }
 bb->Release();sc->Release();
}
static HRESULT STDMETHODCALLTYPE hookPresent(IDXGISwapChain *sc,UINT sync,UINT flags){captureBuffer(sc);return originalPresent(sc,sync,flags);}
static HRESULT STDMETHODCALLTYPE hookPresent1(IDXGISwapChain1 *sc,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS *p){captureBuffer(sc);return originalPresent1(sc,sync,flags,p);}
static BYTE* loadFile(const char* name,DWORD size){HANDLE f=CreateFileA(name,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return nullptr;BYTE* b=(BYTE*)VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD n=0;BOOL ok=ReadFile(f,b,size,&n,nullptr);CloseHandle(f);return ok&&n==size?b:nullptr;}

static void report(const char *s,xefg_swapchain_logging_level_t l,void*){printf("xefg[%d] %s\n",int(l),s);}
static HMODULE loadSdk(const wchar_t *name){
 static wchar_t path[32768];DWORD length=GetModuleFileNameW(nullptr,path,32768);if(!length||length>=32768)return nullptr;
 int end=lstrlenW(path);while(end>0&&path[end-1]!=L'\\')--end;
 if(end+12+lstrlenW(name)>=32768)return nullptr;
 path[end]=0;lstrcatW(path,L"xess-fg-sdk\\");lstrcatW(path,name);
 return LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
}
static int run(){
 HMODULE dll=loadSdk(L"libxess_fg.dll");
 if(!dll){printf("load-error=%lu\n",GetLastError());return 1;}
 #define API(n) auto n=(decltype(&::n))GetProcAddress(dll,#n);if(!n){puts("missing " #n);return 2;}
 API(xefgSwapChainGetVersion) API(xefgSwapChainD3D12CreateContext) API(xefgSwapChainSetLoggingCallback)
 API(xefgSwapChainD3D12BuildPipelines) API(xefgSwapChainGetProperties) API(xefgSwapChainDestroy)
 API(xefgSwapChainD3D12InitFromSwapChain) API(xefgSwapChainD3D12InitFromSwapChainDesc) API(xefgSwapChainD3D12GetSwapChainPtr)
 API(xefgSwapChainD3D12TagFrameResource) API(xefgSwapChainTagFrameConstants)
 API(xefgSwapChainSetLatencyReduction) API(xefgSwapChainSetPresentId) API(xefgSwapChainSetEnabled) API(xefgSwapChainGetLastPresentStatus)
 xefg_swapchain_version_t v{};printf("version-result=%d ",xefgSwapChainGetVersion(&v));printf("%u.%u.%u.%u\n",v.major,v.minor,v.patch,v.reserved);
 GPU gpu;if(!gpu.init())return 3;
 HMODULE ll=loadSdk(L"libxell.dll");if(!ll)return 30;
 auto llCreate=(decltype(&::xellD3D12CreateContext))GetProcAddress(ll,"xellD3D12CreateContext");
 auto llMarker=(decltype(&::xellAddMarkerData))GetProcAddress(ll,"xellAddMarkerData");
 auto llDestroy=(decltype(&::xellDestroyContext))GetProcAddress(ll,"xellDestroyContext");
 auto llSleepMode=(decltype(&::xellSetSleepMode))GetProcAddress(ll,"xellSetSleepMode");
 auto llSleep=(decltype(&::xellSleep))GetProcAddress(ll,"xellSleep");
 if(!llCreate||!llMarker||!llDestroy||!llSleepMode||!llSleep)return 31;
 xell_context_handle_t llc{};auto llr=llCreate(gpu.d,&llc);printf("xell-create=%d\n",int(llr));if(llr<0)return 32;
 xefg_swapchain_handle_t context{};auto r=xefgSwapChainD3D12CreateContext(gpu.d,&context);printf("create=%d\n",int(r));if(r<0)return 4;
 xefgSwapChainSetLoggingCallback(context,XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG,report,nullptr);
 r=xefgSwapChainSetLatencyReduction(context,llc);printf("bind-xell=%d\n",int(r));if(r<0)return 33;
 xefg_swapchain_properties_t props{};r=xefgSwapChainGetProperties(context,&props);printf("properties=%d maxInterpolations=%u\n",int(r),props.maxSupportedInterpolations);
 r=xefgSwapChainD3D12BuildPipelines(context,nullptr,1,XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH);printf("build-pipelines=%d\n",int(r));

 if(r<0)return 5;
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XeFG isolated GPU fixture";RegisterClassW(&wc);
 HWND wnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"XeFG GPU check",WS_OVERLAPPEDWINDOW,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);if(!wnd)return 6;
 ShowWindow(wnd,SW_SHOWNOACTIVATE);
 IDXGIFactory2 *factory{};HMODULE dxgi=LoadLibraryW(L"dxgi.dll");using FactoryFn=HRESULT(WINAPI*)(UINT,REFIID,void**);auto createFactory=(FactoryFn)GetProcAddress(dxgi,"CreateDXGIFactory2");if(!createFactory||FAILED(createFactory(0,IID_IDXGIFactory2,(void**)&factory)))return 7;
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=W;desc.Height=H;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=3;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
 xefg_swapchain_d3d12_init_params_t params{};params.initFlags=XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;params.maxInterpolatedFrames=1;params.uiMode=XEFG_SWAPCHAIN_UI_MODE_AUTO;
 IDXGISwapChain1 *realSc{};HRESULT createHr=factory->CreateSwapChainForHwnd(gpu.q,wnd,&desc,nullptr,nullptr,&realSc);if(FAILED(createHr))return 40;
 captureGpu.d=gpu.d;captureGpu.q=gpu.q;
 if(FAILED(gpu.d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&captureGpu.a))||FAILED(gpu.d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,captureGpu.a,nullptr,IID_ID3D12GraphicsCommandList,(void**)&captureGpu.l))||FAILED(captureGpu.l->Close())||FAILED(gpu.d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&captureGpu.f)))return 41;
 captureGpu.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!captured.init(gpu.d,DXGI_FORMAT_R8G8B8A8_UNORM))return 42;
 void** vt=*(void***)realSc;DWORD protect;
 if(!VirtualProtect(vt,40*sizeof(void*),PAGE_EXECUTE_READWRITE,&protect))return 43;
 originalPresent=(PresentFn)vt[8];originalPresent1=(Present1Fn)vt[22];vt[8]=(void*)hookPresent;vt[22]=(void*)hookPresent1;VirtualProtect(vt,40*sizeof(void*),protect,&protect);
 params.pApplicationSwapChain=realSc;
 r=xefgSwapChainD3D12InitFromSwapChain(context,gpu.q,&params);printf("swapchain-init=%d\n",int(r));if(r<0)return 8;
 IDXGISwapChain3 *sc{};r=xefgSwapChainD3D12GetSwapChainPtr(context,IID_IDXGISwapChain3,(void**)&sc);printf("swapchain-pointer=%d\n",int(r));if(r<0)return 9;
 xell_sleep_params_t sleepParams{};sleepParams.bLowLatencyMode=1;
 llr=llSleepMode(llc,&sleepParams);printf("xell-sleep-mode=%d\n",int(llr));if(llr<0)return 34;
 r=xefgSwapChainSetEnabled(context,1);printf("enabled=%d\n",int(r));if(r<0)return 10;
 Texture color[3],motion[3],depth[3];for(int j=0;j<3;j++)if(!color[j].init(gpu.d,DXGI_FORMAT_R8G8B8A8_UNORM)||!motion[j].init(gpu.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,RW,RH)||!depth[j].init(gpu.d,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,RW,RH))return 11;
 BYTE *colors[2]={loadFile("xefg-replay/color-0.bin",W*H*4),loadFile("xefg-replay/color-1.bin",W*H*4)};
 BYTE *depths[2]={loadFile("xefg-replay/depth-0.bin",RW*RH*4),loadFile("xefg-replay/depth-1.bin",RW*RH*4)};
 BYTE *motions[2]={(BYTE*)VirtualAlloc(nullptr,RW*RH*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE),loadFile("xefg-replay/motion-1.bin",RW*RH*4)};
 auto oldConstants=(xefg_swapchain_frame_constant_data_t*)loadFile("xefg-replay/constants-0.bin",sizeof(xefg_swapchain_frame_constant_data_t));
 auto newConstants=(xefg_swapchain_frame_constant_data_t*)loadFile("xefg-replay/constants-1.bin",sizeof(xefg_swapchain_frame_constant_data_t));
 if(!colors[0]||!colors[1]||!depths[0]||!depths[1]||!motions[0]||!motions[1]||!oldConstants||!newConstants)return 44;
 int doubleFrames=0; LARGE_INTEGER tq,ta,tb,tc,td;QueryPerformanceFrequency(&tq);
 for(UINT frame=1;frame<=13;frame++){
  MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
  QueryPerformanceCounter(&ta);llSleep(llc,frame);QueryPerformanceCounter(&tb);llMarker(llc,frame,XELL_SIMULATION_START);llMarker(llc,frame,XELL_SIMULATION_END);llMarker(llc,frame,XELL_RENDERSUBMIT_START);
  int j=frame%3,index=frame==13?1:0;BYTE *c=colors[index],*mv=motions[index],*z=depths[index];
  if(!gpu.begin())return 12;
  color[j].write(gpu.l,c,W*4,D3D12_RESOURCE_STATE_COPY_SOURCE);motion[j].write(gpu.l,mv,RW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);depth[j].write(gpu.l,z,RW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12Resource *bb{};if(FAILED(sc->GetBuffer(sc->GetCurrentBackBufferIndex(),IID_ID3D12Resource,(void**)&bb)))return 13;
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={bb,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST};gpu.l->ResourceBarrier(1,&b);gpu.l->CopyResource(bb,color[j].gpu);b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;gpu.l->ResourceBarrier(1,&b);
  color[j].transition(gpu.l,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12Resource* inputs[]={color[j].gpu,motion[j].gpu,depth[j].gpu};xefg_swapchain_resource_type_t types[]={XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,XEFG_SWAPCHAIN_RES_MOTION_VECTOR,XEFG_SWAPCHAIN_RES_DEPTH};
  for(int k=0;k<3;k++){xefg_swapchain_d3d12_resource_data_t data{};data.type=types[k];data.validity=XEFG_SWAPCHAIN_RV_ONLY_NOW;data.resourceSize=k==0?xefg_swapchain_2d_t{W,H}:xefg_swapchain_2d_t{RW,RH};data.pResource=inputs[k];data.incomingState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;r=xefgSwapChainD3D12TagFrameResource(context,gpu.l,frame,&data);if(r<0){printf("tag-resource=%d k=%d\n",int(r),k);return 14;}}
  xefg_swapchain_frame_constant_data_t constants=index?*newConstants:*oldConstants;constants.resetHistory=frame==1;
  r=xefgSwapChainTagFrameConstants(context,frame,&constants);if(r<0)return 15;if(!gpu.end())return 16;
  r=xefgSwapChainSetPresentId(context,frame);if(r<0)return 17;
  llMarker(llc,frame,XELL_RENDERSUBMIT_END);llMarker(llc,frame,XELL_PRESENT_START);
  QueryPerformanceCounter(&tc);
  HRESULT hr=sc->Present(1,0);QueryPerformanceCounter(&td);printf("timing[%u] sleep_ms=%.3f prepare_ms=%.3f present_ms=%.3f\n",frame,1000.0*(tb.QuadPart-ta.QuadPart)/tq.QuadPart,1000.0*(tc.QuadPart-tb.QuadPart)/tq.QuadPart,1000.0*(td.QuadPart-tc.QuadPart)/tq.QuadPart);llMarker(llc,frame,XELL_PRESENT_END);printf("present[%u]=%08lx ",frame,(unsigned long)hr);bb->Release();if(FAILED(hr))return 18;
  Sleep(34);xefg_swapchain_present_status_t status{};r=xefgSwapChainGetLastPresentStatus(context,&status);printf("status=%d frames=%u fgResult=%d enabled=%u\n",int(r),status.framesPresented,int(status.frameGenResult),status.isFrameGenEnabled);if(r==0&&status.framesPresented==2&&status.frameGenResult==0)doubleFrames++;
 }
 printf("actual-double-present-calls=%d\n",doubleFrames);InterlockedExchange(&captureArmed,0);sc->Release();xefgSwapChainDestroy(context);llDestroy(llc);DestroyWindow(wnd);return doubleFrames>0?0:19;

}
extern "C" void entry(){ExitProcess(run());}
