#pragma once
#include "native-ui.h"
#include "gpu-capture.h"
#include <api/include/dx12/ffx_api_dx12.h>
#ifdef HISTORY_VISIBILITY_FIX
#include "history-post.h"
#endif
// Only compiled into the separate FH6 candidate provider. Ordinary audit DLLs
// retain descriptor pointer identity and never record GPU work.
static SRWLOCK nativeLock=SRWLOCK_INIT;
struct NativeQueue {ffxContext context{};ID3D12CommandQueue *queue{};};
struct NativeContext {
    struct Config { uint64_t frame{}; bool valid{},enabled{},async{};FfxApiResource hudless{}; } config[8];
    ffxContext context{};
    ID3D12Device *device{};
    NativeUiCompositor compositor;
#ifdef HISTORY_VISIBILITY_FIX
    HistoryPost history;
#endif
    FgGpuCapture capture;
    CapturePrepare prepared[8]{};
    ID3D12CommandQueue *queue{};
    uint64_t applied{},declined{};
};
static NativeQueue nativeQueues[4];
static NativeContext nativeContexts[4];
extern "C" __declspec(dllexport) unsigned FH6CaptureActive() {
    unsigned count=0;for(auto &c:nativeContexts)count+=InterlockedCompareExchange(&c.capture.activeJobs,0,0);return count;
}
static void nativeCreated(ffxContext context,ffxCreateContextDescHeader *desc,ffxReturnCode_t result) {
    if(result || !context || !desc)return;
    ID3D12CommandQueue *queue=nullptr;
    if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12)
        queue=((ffxCreateContextDescFrameGenerationSwapChainWrapDX12*)desc)->gameQueue;
    else if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12)
        queue=((ffxCreateContextDescFrameGenerationSwapChainNewDX12*)desc)->gameQueue;
    else if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12)
        queue=((ffxCreateContextDescFrameGenerationSwapChainForHwndDX12*)desc)->gameQueue;
    AcquireSRWLockExclusive(&nativeLock);
    if(queue && queue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT) {
        for(auto &q:nativeQueues)if(!q.context) {q.context=context;q.queue=queue;queue->AddRef();break;}
    }
    if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION) {
        ID3D12Device *device=nullptr;
        for(auto h=desc->pNext;h;h=h->pNext)if(h->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12)
            device=((ffxCreateBackendDX12Desc*)h)->device;
        if(device)for(auto &c:nativeContexts)if(!c.context && !c.device) {
            c.context=context;
            c.device=device;device->AddRef();
            c.capture.setup(self,report);
            // Opening video/HDR/disabled contexts must remain a pure forward.
            // No compiler DLL, shader compilation, root signature or GPU PSO
            // creation until a matching active native-UI frame actually exists.
            if(report) {char line[180];snprintf(line,sizeof(line),"{\"event\":\"native_ui_context\",\"context\":\"%llx\",\"pipelineDeferred\":true}",
                (unsigned long long)context);report(line);}
            break;
        }
    }
    ReleaseSRWLockExclusive(&nativeLock);
}
static void nativeConfigured(ffxContext context,const ffxConfigureDescHeader *desc,ffxReturnCode_t result) {
    if(result || !desc || desc->type!=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION)return;
    const auto &p=*(const ffxConfigureDescFrameGeneration*)desc;
    AcquireSRWLockExclusive(&nativeLock);
    for(auto &c:nativeContexts)if(c.context==context) {
        auto &cfg=c.config[p.frameID%8];cfg.frame=p.frameID;
        cfg.valid=!p.header.pNext;cfg.enabled=p.frameGenerationEnabled;cfg.async=p.allowAsyncWorkloads;cfg.hudless=p.HUDLessColor;
        // The SDK chooses gameQueue for interpolation only with async=false.
        // A unique matching direct queue is required; no cross-queue guess.
        if(p.frameGenerationEnabled && !p.allowAsyncWorkloads && !c.queue) {
            ID3D12CommandQueue *match=nullptr;bool ambiguous=false;
            for(auto &q:nativeQueues)if(q.queue) {
                ID3D12Device *d=nullptr;
                if(SUCCEEDED(q.queue->GetDevice(IID_ID3D12Device,(void**)&d))) {
                    if(d==c.device) {if(match && match!=q.queue)ambiguous=true;match=q.queue;}
                    d->Release();
                }
            }
            if(match && !ambiguous) {c.queue=match;match->AddRef();}
        }
        break;
    }
    ReleaseSRWLockExclusive(&nativeLock);
}
template<typename Prepare> static void nativePrepared(ffxContext context,const Prepare &p) {
    AcquireSRWLockExclusive(&nativeLock);
    for(auto &c:nativeContexts)if(c.context==context) {
        auto &data=c.prepared[p.frameID%8];data.frame=p.frameID;data.known=true;data.milliseconds=p.frameTimeDelta;
        data.render[0]=p.renderSize.width;data.render[1]=p.renderSize.height;
        data.scale[0]=p.motionVectorScale.x;data.scale[1]=p.motionVectorScale.y;
        data.jitter[0]=p.jitterOffset.x;data.jitter[1]=p.jitterOffset.y;break;
    }
    ReleaseSRWLockExclusive(&nativeLock);
}
#ifdef HISTORY_VISIBILITY_FIX
template<typename Prepare> static void historyPrepared(ffxContext context,const Prepare &p) {
    AcquireSRWLockExclusive(&nativeLock);
    for(auto &c:nativeContexts)if(c.context==context){
        const auto &cfg=c.config[p.frameID%8];ChainFrame frame{};
        if(c.queue&&cfg.valid&&cfg.frame==p.frameID&&cfg.enabled&&!cfg.async&&getFrame&&getFrame(p.frameID,&frame)&&frame.hasConstants&&frame.inverted==1&&frame.cameraMotionIncluded==1&&!frame.motion3D&&!frame.jittered){
            bool ready=c.history.initialize(c.device)&&c.history.prepare(p);
            if(report&&(p.frameID<5||p.frameID%600==0||(!ready&&c.history.declined++<4))){char line[260];snprintf(line,sizeof(line),"{\"event\":\"history_prepare\",\"frame\":%llu,\"recorded\":%s,\"reason\":\"%s\",\"pipelineHr\":%u}",(unsigned long long)p.frameID,ready?"true":"false",c.history.reason,(unsigned)c.history.status);report(line);}
        }break;
    }
    ReleaseSRWLockExclusive(&nativeLock);
}
#endif
static ffxReturnCode_t nativeDispatch(ffxContext *context,const ffxDispatchDescHeader *desc) {
    if(!desc || desc->type!=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION) {
        if(context && desc && desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE)
            nativePrepared(*context,*(const ffxDispatchDescFrameGenerationPrepare*)desc);
        else if(context && desc && desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2)
            nativePrepared(*context,*(const ffxDispatchDescFrameGenerationPrepareV2*)desc);
        auto result=dispatch?dispatch(context,desc):FFX_API_RETURN_NO_PROVIDER;
#ifdef HISTORY_VISIBILITY_FIX
        if(!result&&context&&desc){
            if(desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE)historyPrepared(*context,*(const ffxDispatchDescFrameGenerationPrepare*)desc);
            else if(desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2)historyPrepared(*context,*(const ffxDispatchDescFrameGenerationPrepareV2*)desc);
        }
#endif
        return result;
    }
    auto &p=*(const ffxDispatchDescFrameGeneration*)desc;
    AcquireSRWLockExclusive(&nativeLock);
    NativeContext *c=nullptr;
    for(auto &candidate:nativeContexts)if(context && candidate.context==*context){c=&candidate;break;}
    NativeUiCompositor::Binding *binding=nullptr;FfxApiResource hudless{};ChainFrame frame{};
    if(c && c->queue && getFrame) {
        const auto &cfg=c->config[p.frameID%8];
        if(cfg.valid && cfg.frame==p.frameID && !cfg.async){
            c->capture.retire((ID3D12Resource*)p.outputs[0].resource,p.frameID,c->queue);
#ifdef HISTORY_VISIBILITY_FIX
            c->history.retire((ID3D12Resource*)p.outputs[0].resource,p.frameID,c->queue);
#endif
        }
        if(cfg.valid && cfg.frame==p.frameID && cfg.enabled && !cfg.async && getFrame(p.frameID,&frame)) {
            const auto w=p.presentColor.description.width,h=p.presentColor.description.height;
            if(!c->compositor.device && p.presentColor.description.format==FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM &&
               NativeUiCompositor::full(frame.tags[2],w,h) && NativeUiCompositor::full(frame.tags[3],w,h) &&
               cfg.hudless.resource==(void*)frame.tags[2].native) {
                c->compositor.initialize(c->device);
                if(report) {char line[210];snprintf(line,sizeof(line),"{\"event\":\"native_ui_pipeline\",\"frame\":%llu,\"pipelineHr\":%u}",
                    (unsigned long long)p.frameID,(unsigned)c->compositor.status);report(line);}
            }
            binding=c->compositor.prepare(frame,cfg.hudless,p);hudless=cfg.hudless;
        }
    }
    ffxReturnCode_t result;
    if(binding) {
        auto sceneOnly=p;sceneOnly.presentColor=hudless;
        result=dispatch?dispatch(context,&sceneOnly.header):FFX_API_RETURN_NO_PROVIDER;
        if(result==FFX_API_RETURN_OK) {
            #ifdef HISTORY_VISIBILITY_FIX
            bool corrected=c->history.record(frame,c->prepared[p.frameID%8],p,binding->scene);
            if(report&&(c->applied<8||p.frameID%600==0)){char line[250];snprintf(line,sizeof(line),"{\"event\":\"history_post\",\"frame\":%llu,\"dispatched\":%s,\"reason\":\"%s\"}",(unsigned long long)p.frameID,corrected?"true":"false",c->history.reason);report(line);}
            c->history.submitted(p.frameID,(ID3D12Resource*)p.outputs[0].resource,corrected);
#endif
            auto job=c->capture.begin(c->device,frame,c->prepared[p.frameID%8],p);
            auto list=(ID3D12GraphicsCommandList*)p.commandList;
            if(job)FgGpuCapture::copies(list,job,0,6);
            c->compositor.record(list,binding);
            if(job)FgGpuCapture::copies(list,job,6,7);
        }
        if(report && (c->applied++<8 || p.frameID%60==0)) {
            char line[450];snprintf(line,sizeof(line),"{\"event\":\"native_ui_dispatch\",\"frame\":%llu,\"scene\":\"%llx\",\"ui\":\"%llx\",\"originalPresent\":\"%llx\",\"generated\":\"%llx\",\"frames\":%u,\"result\":%u}",
                (unsigned long long)p.frameID,(unsigned long long)binding->scene,(unsigned long long)binding->ui,
                (unsigned long long)p.presentColor.resource,(unsigned long long)binding->output,p.numGeneratedFrames,result);report(line);
        }
    } else {
        // Unknown/absent resources, async queues, extensions or formats retain
        // the existing algorithm; do not switch off FG or fake success.
        result=dispatch?dispatch(context,desc):FFX_API_RETURN_NO_PROVIDER;
        if(c && report && (c->declined++<4 || p.frameID%600==0)) {
            char line[220];snprintf(line,sizeof(line),"{\"event\":\"native_ui_passthrough\",\"frame\":%llu,\"queueKnown\":%u,\"nativeTags\":%u,\"result\":%u}",
                (unsigned long long)p.frameID,(unsigned)(c->queue!=nullptr),frame.tagsMask,result);report(line);
        }
    }
    ReleaseSRWLockExclusive(&nativeLock);return result;
}
static void nativeDestroyed(ffxContext context,ffxReturnCode_t result) {
    if(result)return;
    AcquireSRWLockExclusive(&nativeLock);
    for(auto &c:nativeContexts)if(c.context==context) {
        bool drained=!c.applied;
#ifdef HISTORY_VISIBILITY_FIX
        drained=drained&&!c.history.recorded;
#endif
        if(c.queue && !drained) {
            ID3D12Fence *fence=nullptr;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(event && SUCCEEDED(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence)) &&
               SUCCEEDED(c.queue->Signal(fence,1)) && SUCCEEDED(fence->SetEventOnCompletion(1,event)))
                drained=WaitForSingleObject(event,10000)==WAIT_OBJECT_0;
            if(fence)fence->Release();if(event)CloseHandle(event);
        }
        if(drained) {
#ifdef HISTORY_VISIBILITY_FIX
            c.history.release();
#endif
            if(c.queue)c.capture.drained(c.queue);c.compositor.release();if(c.queue)c.queue->Release();if(c.device)c.device->Release();c.device=nullptr;c.queue=nullptr;c.context=nullptr;for(auto &cfg:c.config)cfg={};for(auto &prep:c.prepared)prep={};c.applied=c.declined=0;}
        // On a GPU timeout retain this bounded slot rather than free in-flight
        // descriptor heaps/resources. No unbounded allocation or wait/retry.
        else if(report)report("{\"event\":\"native_ui_retained_after_gpu_timeout\"}");
    }
    for(auto &q:nativeQueues)if(q.context==context){q.queue->Release();q={};}
    ReleaseSRWLockExclusive(&nativeLock);
}
