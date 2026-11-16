#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "xess-sdk/xess_d3d12.h"
static void output(const char *s) { DWORD n=0; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)lstrlenA(s), &n, nullptr); }
static int probe_printf(const char *format, ...) { char text[1024];va_list ap;va_start(ap,format);using F=int(__cdecl*)(char*,size_t,const char*,va_list);static auto f=(F)GetProcAddress(LoadLibraryW(L"msvcrt.dll"),"_vsnprintf");if(!f)return -1;int n=f(text,sizeof(text)-1,format,ap);va_end(ap);text[sizeof(text)-1]=0;output(text);return n;}
static int probe_puts(const char *s){output(s);output("\n");return 0;}
extern "C" void *memcpy(void *d,const void *s,size_t n){auto a=(volatile unsigned char*)d;auto b=(const volatile unsigned char*)s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
extern "C" void *memset(void *d,int v,size_t n){auto a=(volatile unsigned char*)d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)v;return d;}
#define printf probe_printf
#define puts probe_puts
static constexpr UINT W=3472,H=2266,RW=2314,RH=1510;
#include "../../rosetta/fg-chain/probe-gpu.h"
static void report(const char *text,xess_logging_level_t level){printf("xess[%d] %s\n",int(level),text);}
int wmain(int argc,wchar_t **argv){
 if(argc!=2)return 1;
 HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(!dll){printf("load error=%lu\n",GetLastError());return 2;}
 #define API(name) auto name=(decltype(&::name))GetProcAddress(dll,#name);if(!name){puts("missing " #name);return 3;}
 API(xessGetVersion) API(xessD3D12CreateContext) API(xessD3D12Init) API(xessD3D12Execute) API(xessDestroyContext) API(xessSetLoggingCallback)
 xess_version_t v{};printf("version result=%d ",xessGetVersion(&v));printf("%u.%u.%u.%u\n",v.major,v.minor,v.patch,v.reserved);
 GPU gpu; if(!gpu.init()){puts("D3D12 device failed");return 4;}
 xess_context_handle_t context{};auto result=xessD3D12CreateContext(gpu.d,&context);printf("create=%d\n",result);if(result<0)return 5;
 xessSetLoggingCallback(context,XESS_LOGGING_LEVEL_DEBUG,report);
 xess_d3d12_init_params_t init{};init.outputResolution={W,H};init.qualitySetting=XESS_QUALITY_SETTING_QUALITY;init.initFlags=XESS_INIT_FLAG_INVERTED_DEPTH|XESS_INIT_FLAG_LDR_INPUT_COLOR;
 result=xessD3D12Init(context,&init);printf("init=%d\n",result);if(result<0)return 6;
 Texture color,motion,depth,output;
 if(!color.init(gpu.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,RW,RH)||!motion.init(gpu.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,RW,RH)||!depth.init(gpu.d,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,RW,RH)||!output.init(gpu.d,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,W,H))return 7;
 auto c=(BYTE*)VirtualAlloc(nullptr,RW*RH*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE),mv=(BYTE*)VirtualAlloc(nullptr,RW*RH*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE),d=(BYTE*)VirtualAlloc(nullptr,RW*RH*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE),out=(BYTE*)VirtualAlloc(nullptr,W*H*8,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!c||!mv||!d||!out)return 14;for(UINT y=0;y<RH;y++)for(UINT x=0;x<RW;x++){UINT p=(y*RW+x)*4;c[p]=x*255/RW;c[p+1]=y*255/RH;c[p+2]=80;c[p+3]=255;float z=.5f;memcpy(d+p,&z,4);}
 if(!gpu.begin()||!color.write(gpu.l,c,RW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||!motion.write(gpu.l,mv,RW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||!depth.write(gpu.l,d,RW*4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||!gpu.end())return 8;
 xess_d3d12_execute_params_t e{};e.pColorTexture=color.gpu;e.pVelocityTexture=motion.gpu;e.pDepthTexture=depth.gpu;e.pOutputTexture=output.gpu;e.exposureScale=1;e.inputWidth=RW;e.inputHeight=RH;
 ID3D12QueryHeap *qh{}; ID3D12Resource *qr{}; UINT64 freq=0;
 D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=2;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
 D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=16;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 if(FAILED(gpu.d->CreateQueryHeap(&qd,IID_ID3D12QueryHeap,(void**)&qh))||FAILED(gpu.d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&qr))||FAILED(gpu.q->GetTimestampFrequency(&freq))||!freq)return 20;
 LARGE_INTEGER a,b,encodeStart,encodeEnd,submitStart,cpuFreq;QueryPerformanceFrequency(&cpuFreq);
 for(int i=0;i<6;i++){QueryPerformanceCounter(&a);if(!gpu.begin())return 9;output.transition(gpu.l,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);e.resetHistory=i==0;gpu.l->EndQuery(qh,D3D12_QUERY_TYPE_TIMESTAMP,0);QueryPerformanceCounter(&encodeStart);result=xessD3D12Execute(context,gpu.l,&e);QueryPerformanceCounter(&encodeEnd);gpu.l->EndQuery(qh,D3D12_QUERY_TYPE_TIMESTAMP,1);gpu.l->ResolveQueryData(qh,D3D12_QUERY_TYPE_TIMESTAMP,0,2,qr,0);printf("execute[%d]=%d\n",i,result);if(result<0)return 10;output.copyOut(gpu.l);QueryPerformanceCounter(&submitStart);if(!gpu.end()){puts("GPU fence failed");return 11;}QueryPerformanceCounter(&b);printf("phase[%d] execute_cpu_ms=%.3f submit_fence_ms=%.3f\n",i,1000.0*(encodeEnd.QuadPart-encodeStart.QuadPart)/cpuFreq.QuadPart,1000.0*(b.QuadPart-submitStart.QuadPart)/cpuFreq.QuadPart);UINT64 *ticks{};D3D12_RANGE range{0,16};if(FAILED(qr->Map(0,&range,(void**)&ticks)))return 21;printf("timing[%d] query_interval_ms_NOT_GPU_TIME=%.3f cpu_submit_wait_ms=%.3f\n",i,1000.0*(ticks[1]-ticks[0])/freq,1000.0*(b.QuadPart-a.QuadPart)/cpuFreq.QuadPart);D3D12_RANGE empty{};qr->Unmap(0,&empty);}
 if(!output.read(out,W*8))return 12;size_t nonzero=0;for(UINT i=0;i<W*H*8;i++)nonzero+=out[i]!=0;printf("completed fence=%llu output_nonzero_bytes=%zu/%zu\n",gpu.value,nonzero,size_t(W*H*8));
 xessDestroyContext(context);return nonzero?0:13;
}

// Put the official XeSS SDK DLL beside this isolated executable.
extern "C" void entry(){wchar_t name[]=L"probe";static wchar_t dll[32768];DWORD n=GetModuleFileNameW(nullptr,dll,32768);if(!n||n>=32768)ExitProcess(30);while(n&&dll[n-1]!=L'\\')--n;if(n+12>=32768)ExitProcess(31);dll[n]=0;lstrcatW(dll,L"libxess.dll");wchar_t *args[]={name,dll};ExitProcess(wmain(2,args));}
