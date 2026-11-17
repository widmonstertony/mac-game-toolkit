// Optional outer observation layer. The already-installed provider is loaded
// unchanged as framegeneration-sequence-inner.dll in the same directory.
// No frame synthesis, UI composition, timing correction or rendering fallback
// is implemented here. Never install without forwarding/GPU validation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <new>
#include <api/include/dx12/ffx_api_dx12.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include <framegeneration/include/dx12/ffx_api_framegeneration_dx12.h>
#include "capture.h"
static HMODULE self,inner;
static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
static SRWLOCK lock=SRWLOCK_INIT;
static PfnFfxCreateContext create;
static PfnFfxDestroyContext destroy;
static PfnFfxConfigure configure;
static PfnFfxQuery query;
static PfnFfxDispatch dispatch;
static ChainGet getFrame;
static ChainReport report;
static wchar_t root[32768],request[32768];
static volatile LONG writers;
struct Capture:SequenceCapture {wchar_t folder[32768]{};};
struct Context {
 ffxContext context{};ID3D12Device *device{};ID3D12CommandQueue *queue{};
 UINT w{},h{};Capture *capture{};
 struct Config {uint64_t frame{};bool eligible{};void *hudless{};} config[16];
};
struct Queue {ffxContext context{};ID3D12CommandQueue *queue{};};
static Context contexts[4];static Queue queues[4];static unsigned polls;
static BOOL CALLBACK initialize(PINIT_ONCE,PVOID,PVOID*){
 DWORD n=GetModuleFileNameW(self,root,32768);if(!n||n>32500)return TRUE;
 auto slash=wcsrchr(root,L'\\');if(!slash)return TRUE;slash[1]=0;
 wchar_t path[32768];wcscpy(path,root);wcscat(path,L"framegeneration-sequence-inner.dll");
 inner=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!inner||inner==self)return TRUE;
 create=(PfnFfxCreateContext)GetProcAddress(inner,"ffxCreateContext");destroy=(PfnFfxDestroyContext)GetProcAddress(inner,"ffxDestroyContext");
 configure=(PfnFfxConfigure)GetProcAddress(inner,"ffxConfigure");query=(PfnFfxQuery)GetProcAddress(inner,"ffxQuery");dispatch=(PfnFfxDispatch)GetProcAddress(inner,"ffxDispatch");
 wcscpy(request,root);wcscat(request,L"sequence-request");wcscat(root,L"sequences");return TRUE;
}
static void observer(){if(getFrame&&report)return;auto m=GetModuleHandleW(L"fh6-fg-chain.asi");if(m){getFrame=(ChainGet)GetProcAddress(m,"FH6ChainGet");report=(ChainReport)GetProcAddress(m,"FH6ChainReport");}}
static D3D12_RESOURCE_STATES state(UINT v){UINT s=0;if(v&FFX_API_RESOURCE_STATE_PIXEL_READ)s|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;if(v&FFX_API_RESOURCE_STATE_COMPUTE_READ)s|=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;if(v&FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)s|=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;if(v&FFX_API_RESOURCE_STATE_COPY_SRC)s|=D3D12_RESOURCE_STATE_COPY_SOURCE;if(v&FFX_API_RESOURCE_STATE_COPY_DEST)s|=D3D12_RESOURCE_STATE_COPY_DEST;if(v&FFX_API_RESOURCE_STATE_RENDER_TARGET)s|=D3D12_RESOURCE_STATE_RENDER_TARGET;return D3D12_RESOURCE_STATES(s);}
static bool save(const wchar_t *folder,const wchar_t *name,const void *bytes,UINT64 size){
 wchar_t path[32768];if(wcslen(folder)+wcslen(name)>32760)return false;swprintf(path,32768,L"%ls\\%ls",folder,name);
 auto file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 auto p=(const BYTE*)bytes;bool ok=true;while(size){DWORD done=0,n=size>1048576?1048576:DWORD(size);if(!WriteFile(file,p,n,&done,nullptr)||done!=n){ok=false;break;}p+=n;size-=n;}CloseHandle(file);return ok;
}
static DWORD WINAPI writer(void *ptr){
 auto c=(Capture*)ptr;bool ok=!c->aborted&&c->count==SequenceCapture::Capacity;
 const wchar_t *names[]={L"prepare-current-depth.bin",L"prepare-current-motion.bin",L"scene.bin",L"generated-after-ui.bin",L"scene-before-fg.bin"};
 const char *labels[]={"prepare-current-depth","prepare-current-motion","scene","generated-after-ui","scene-before-fg"};
 for(unsigned index=0;ok&&index<c->count;++index){auto &f=c->frames[index];
  wchar_t folder[32768];swprintf(folder,32768,L"%ls\\frame-%llu",c->folder,(unsigned long long)f.id);
  ok=CreateDirectoryW(folder,nullptr)!=0;if(!ok)break;
  BYTE *pixels=nullptr;D3D12_RANGE range{0,SIZE_T(f.bytes)};ok=SUCCEEDED(f.readback->Map(0,&range,(void**)&pixels));
  if(!ok)break;for(unsigned k=0;k<SequenceCapture::Images;++k)ok=save(folder,names[k],pixels+f.image[k].footprint.Offset,f.image[k].bytes)&&ok;
  D3D12_RANGE none{};f.readback->Unmap(0,&none);ok=save(folder,L"native-frame.bin",&f.native,sizeof(f.native))&&ok;
  char json[4096];int n=snprintf(json,sizeof(json),"{\"schema\":\"fh6-consecutive-v2\",\"frame\":%llu,\"gpu_fence_completed\":true,\"pixel_files_complete\":%s,\"prepare\":{\"known\":true,\"frame\":%llu,\"milliseconds\":%.9g},\"images\":[",(unsigned long long)f.id,ok?"true":"false",(unsigned long long)f.id,f.milliseconds);
  for(unsigned k=0;k<SequenceCapture::Images&&n>0&&n<int(sizeof(json));++k){auto &i=f.image[k];n+=snprintf(json+n,sizeof(json)-n,"%s{\"name\":\"%s\",\"source_frame\":%llu,\"width\":%u,\"height\":%u,\"copy_format\":%u,\"row_pitch\":%u,\"row_bytes\":%llu,\"bytes\":%llu}",k?",":"",labels[k],(unsigned long long)f.id,i.footprint.Footprint.Width,i.footprint.Footprint.Height,UINT(i.footprint.Footprint.Format),i.footprint.Footprint.RowPitch,(unsigned long long)i.rowBytes,(unsigned long long)i.bytes);}
  if(n<=0||n+3>=int(sizeof(json)))ok=false;else {memcpy(json+n,"]}\n",3);ok=save(folder,L"capture.json",json,n+3)&&ok;}
 }
 char summary[180];int n=snprintf(summary,sizeof(summary),"{\"complete\":%s,\"frames\":%u,\"first\":%llu,\"last\":%llu}\n",ok?"true":"false",c->count,(unsigned long long)c->frames[0].id,(unsigned long long)c->frames[c->count-1].id);
 if(n>0&&n<int(sizeof(summary)))save(c->folder,L"sequence.json",summary,n);
 // All jobs were fence-complete before this thread was started.
 c->clearCompleted();delete c;InterlockedDecrement(&writers);FreeLibraryAndExitThread(self,ok?0:1);return 0;
}
static void collect(Context &c){
 if(c.capture&&!c.capture->armed&&!c.capture->count){delete c.capture;c.capture=nullptr;return;}
 if(c.capture&&c.capture->armed)return;
 if(!c.capture||!c.capture->pollComplete())return;
 HMODULE keep{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)&writer,&keep))return;
 InterlockedIncrement(&writers);auto t=CreateThread(nullptr,0,writer,c.capture,0,nullptr);
 if(!t){InterlockedDecrement(&writers);FreeLibrary(keep);return;}CloseHandle(t);c.capture=nullptr;
}
static Context *find(ffxContext *ctx){for(auto &c:contexts)if(ctx&&c.context==*ctx)return &c;return nullptr;}
static bool anyCapture(){for(auto &c:contexts)if(c.capture)return true;return false;}
static void matchQueue(Context &c){if(c.queue)return;ID3D12CommandQueue *match=nullptr;bool ambiguous=false;
 for(auto &q:queues)if(q.queue){ID3D12Device *d=nullptr;if(SUCCEEDED(q.queue->GetDevice(IID_ID3D12Device,(void**)&d))){if(d==c.device){if(match&&match!=q.queue)ambiguous=true;match=q.queue;}d->Release();}}
 if(match&&!ambiguous){c.queue=match;match->AddRef();}}
// A null FFX HUDLessColor is normal on the game's DLSSG-input route.
// The inner provider resolves that route from these same-frame native tags.
// Preserve its configuration; accept only a full validated scene for recording.
static bool fullScene(const ChainResource &r,UINT w,UINT h){
 return r.native&&r.lifecycle==1&&r.format==DXGI_FORMAT_R8G8B8A8_UNORM&&
  r.state==(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)&&
  r.width==w&&r.height==h&&!r.left&&!r.top&&
  ((!r.extentWidth&&!r.extentHeight)||(r.extentWidth==w&&r.extentHeight==h));
}
template<typename Prepare> static void prepared(ffxContext *ctx,const Prepare &p,bool reset){
 AcquireSRWLockExclusive(&lock);observer();auto c=find(ctx);
 if(c){matchQueue(*c);auto &cfg=c->config[p.frameID%16];ChainFrame f{};
  bool eligible=!reset&&c->queue&&cfg.eligible&&cfg.frame==p.frameID&&getFrame&&getFrame(p.frameID,&f)&&f.hasConstants&&!f.reset&&f.frame==p.frameID&&fullScene(f.tags[2],c->w,c->h)&&(!cfg.hudless||cfg.hudless==(void*)f.tags[2].native)&&f.tags[0].native==(uint64_t)p.depth.resource&&f.tags[1].native==(uint64_t)p.motionVectors.resource;
  if(eligible&&!anyCapture()&&writers==0&&++polls%120==1&&GetFileAttributesW(request)!=INVALID_FILE_ATTRIBUTES){
   auto capture=new(std::nothrow) Capture;
   if(capture){CreateDirectoryW(root,nullptr);swprintf(capture->folder,32768,L"%ls\\pid-%lu-start-%llu-%llu",root,GetCurrentProcessId(),(unsigned long long)p.frameID,(unsigned long long)GetTickCount64());
    if(CreateDirectoryW(capture->folder,nullptr)&&DeleteFileW(request)&&capture->arm())c->capture=capture;else delete capture;}
  }
  if(c->capture&&c->capture->armed){
   if(eligible&&p.depth.state==FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ&&p.motionVectors.state==FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ)
    c->capture->prepare(c->device,(ID3D12GraphicsCommandList*)p.commandList,f,p.frameTimeDelta,(ID3D12Resource*)p.depth.resource,(ID3D12Resource*)p.motionVectors.resource,state(p.depth.state),state(p.motionVectors.state),c->w,c->h);
   else c->capture->abort();
  }
 }
 ReleaseSRWLockExclusive(&lock);
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxCreateContext(ffxContext *ctx,ffxCreateContextDescHeader *desc,const ffxAllocationCallbacks *mem){
 InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);auto result=create?create(ctx,desc,mem):FFX_API_RETURN_NO_PROVIDER;
 if(result||!ctx||!*ctx||!desc)return result;AcquireSRWLockExclusive(&lock);
 ID3D12CommandQueue *q=nullptr;
 if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12)q=((ffxCreateContextDescFrameGenerationSwapChainWrapDX12*)desc)->gameQueue;
 if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12)q=((ffxCreateContextDescFrameGenerationSwapChainNewDX12*)desc)->gameQueue;
 if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12)q=((ffxCreateContextDescFrameGenerationSwapChainForHwndDX12*)desc)->gameQueue;
 if(q&&q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT)for(auto &slot:queues)if(!slot.context){slot.context=*ctx;slot.queue=q;q->AddRef();break;}
 if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION){auto p=(ffxCreateContextDescFrameGeneration*)desc;ID3D12Device *d=nullptr;
  for(auto h=desc->pNext;h;h=h->pNext)if(h->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12)d=((ffxCreateBackendDX12Desc*)h)->device;
  if(d)for(auto &c:contexts)if(!c.context){c.context=*ctx;c.device=d;d->AddRef();c.w=p->displaySize.width;c.h=p->displaySize.height;break;}}
 ReleaseSRWLockExclusive(&lock);return result;
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxConfigure(ffxContext *ctx,const ffxConfigureDescHeader *desc){
 InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);auto result=configure?configure(ctx,desc):FFX_API_RETURN_NO_PROVIDER;
 if(!result&&desc&&desc->type==FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION){auto &p=*(ffxConfigureDescFrameGeneration*)desc;
  AcquireSRWLockExclusive(&lock);if(auto c=find(ctx))c->config[p.frameID%16]={p.frameID,p.frameGenerationEnabled&&!p.allowAsyncWorkloads&&!p.header.pNext&&(!p.HUDLessColor.resource||(p.HUDLessColor.description.format==FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM&&p.HUDLessColor.description.width==c->w&&p.HUDLessColor.description.height==c->h)),p.HUDLessColor.resource};ReleaseSRWLockExclusive(&lock);}
 return result;
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxQuery(ffxContext *ctx,ffxQueryDescHeader *desc){InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);return query?query(ctx,desc):FFX_API_RETURN_NO_PROVIDER;}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxDispatch(ffxContext *ctx,const ffxDispatchDescHeader *desc){
 InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);
 const bool fg=desc&&desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
 if(fg){auto &p=*(ffxDispatchDescFrameGeneration*)desc;AcquireSRWLockExclusive(&lock);if(auto c=find(ctx))if(c->capture&&c->queue){auto &cfg=c->config[p.frameID%16];if(cfg.frame==p.frameID&&cfg.eligible)c->capture->retire(c->queue,(ID3D12Resource*)p.outputs[0].resource,p.frameID);collect(*c);
   if(c->capture){ChainFrame f{};
    if(!p.reset&&p.numGeneratedFrames==1&&!p.header.pNext&&getFrame&&getFrame(p.frameID,&f)&&fullScene(f.tags[2],c->w,c->h))
     c->capture->recordBefore((ID3D12GraphicsCommandList*)p.commandList,f,(ID3D12Resource*)f.tags[2].native,D3D12_RESOURCE_STATES(f.tags[2].state));
    else c->capture->abort();}
  }ReleaseSRWLockExclusive(&lock);}
 auto result=dispatch?dispatch(ctx,desc):FFX_API_RETURN_NO_PROVIDER;
 if(!result&&desc){if(desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE)prepared(ctx,*(ffxDispatchDescFrameGenerationPrepare*)desc,false);
  else if(desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2){auto &p=*(ffxDispatchDescFrameGenerationPrepareV2*)desc;prepared(ctx,p,p.reset);}
  else if(fg){auto &p=*(ffxDispatchDescFrameGeneration*)desc;AcquireSRWLockExclusive(&lock);if(auto c=find(ctx))if(c->capture){ChainFrame f{};
   if(!p.reset&&p.numGeneratedFrames==1&&!p.header.pNext&&p.outputs[0].state==FFX_API_RESOURCE_STATE_UNORDERED_ACCESS&&getFrame&&getFrame(p.frameID,&f)&&fullScene(f.tags[2],c->w,c->h))
    c->capture->record((ID3D12GraphicsCommandList*)p.commandList,f,(ID3D12Resource*)f.tags[2].native,(ID3D12Resource*)p.outputs[0].resource,D3D12_RESOURCE_STATES(f.tags[2].state),state(p.outputs[0].state));
   else c->capture->abort();}ReleaseSRWLockExclusive(&lock);}}
 return result;
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxDestroyContext(ffxContext *ctx,const ffxAllocationCallbacks *mem){
 InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);const auto old=ctx?*ctx:nullptr;auto result=destroy?destroy(ctx,mem):FFX_API_RETURN_NO_PROVIDER;
 if(result||!old)return result;AcquireSRWLockExclusive(&lock);
 for(auto &c:contexts)if(c.context==old){if(c.capture){if(c.queue)c.capture->drainSubmitted(c.queue);collect(c);}
  // Retain a bounded slot if GPU completion has not yet been observed.
  if(!c.capture){if(c.queue)c.queue->Release();if(c.device)c.device->Release();c={};}break;}
 for(auto &q:queues)if(q.context==old){q.queue->Release();q={};break;}
 ReleaseSRWLockExclusive(&lock);return result;
}
extern "C" __declspec(dllexport) unsigned FH6SequenceActive(){unsigned n=writers;AcquireSRWLockShared(&lock);for(auto &c:contexts)if(c.capture)++n;ReleaseSRWLockShared(&lock);return n;}
static unsigned innerCount(const char *name){InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);auto fn=inner?(unsigned(*)())GetProcAddress(inner,name):nullptr;return fn?fn():0;}
extern "C" __declspec(dllexport) unsigned FH6CaptureActive(){return innerCount("FH6CaptureActive")+FH6SequenceActive();}
extern "C" __declspec(dllexport) unsigned FH6HistoryActive(){return innerCount("FH6HistoryActive");}
BOOL WINAPI DllMain(HMODULE m,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH)self=m;return TRUE;}
