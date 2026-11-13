#pragma once
#include "native-ui.h"
#include <api/include/dx12/ffx_api_dx12.h>
// Only compiled into the separate FH6 candidate provider. Ordinary audit DLLs
// retain descriptor pointer identity and never record GPU work.
static SRWLOCK nativeLock=SRWLOCK_INIT;
struct NativeQueue {ffxContext context{};ID3D12CommandQueue *queue{};};
struct NativeContext {
    struct Config { uint64_t frame{}; bool valid{},enabled{},async{};FfxApiResource hudless{}; } config[8];
    ffxContext context{};
    NativeUiCompositor compositor;
    ID3D12CommandQueue *queue{};
    uint64_t applied{},declined{};
};
static NativeQueue nativeQueues[4];
static NativeContext nativeContexts[4];
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
        if(device)for(auto &c:nativeContexts)if(!c.context && !c.compositor.device) {
            c.context=context;
            c.compositor.initialize(device);
            if(report) {char line[180];snprintf(line,sizeof(line),"{\"event\":\"native_ui_context\",\"context\":\"%llx\",\"pipelineHr\":%u}",
                (unsigned long long)context,(unsigned)c.compositor.status);report(line);}
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
        if(!c.queue) {
            ID3D12CommandQueue *match=nullptr;bool ambiguous=false;
            for(auto &q:nativeQueues)if(q.queue) {
                ID3D12Device *d=nullptr;
                if(SUCCEEDED(q.queue->GetDevice(IID_ID3D12Device,(void**)&d))) {
                    if(d==c.compositor.device) {if(match && match!=q.queue)ambiguous=true;match=q.queue;}
                    d->Release();
                }
            }
            if(match && !ambiguous) {c.queue=match;match->AddRef();}
        }
        break;
    }
    ReleaseSRWLockExclusive(&nativeLock);
}
static ffxReturnCode_t nativeDispatch(ffxContext *context,const ffxDispatchDescHeader *desc) {
    if(!desc || desc->type!=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION)
        return dispatch?dispatch(context,desc):FFX_API_RETURN_NO_PROVIDER;
    auto &p=*(const ffxDispatchDescFrameGeneration*)desc;
    AcquireSRWLockExclusive(&nativeLock);
    NativeContext *c=nullptr;
    for(auto &candidate:nativeContexts)if(context && candidate.context==*context){c=&candidate;break;}
    NativeUiCompositor::Binding *binding=nullptr;FfxApiResource hudless{};ChainFrame frame{};
    if(c && c->queue && getFrame) {
        const auto &cfg=c->config[p.frameID%8];
        if(cfg.valid && cfg.frame==p.frameID && cfg.enabled && !cfg.async && getFrame(p.frameID,&frame)) {
            binding=c->compositor.prepare(frame,cfg.hudless,p);hudless=cfg.hudless;
        }
    }
    ffxReturnCode_t result;
    if(binding) {
        auto sceneOnly=p;sceneOnly.presentColor=hudless;
        result=dispatch?dispatch(context,&sceneOnly.header):FFX_API_RETURN_NO_PROVIDER;
        if(result==FFX_API_RETURN_OK)c->compositor.record((ID3D12GraphicsCommandList*)p.commandList,binding);
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
        if(c.queue && !drained) {
            ID3D12Fence *fence=nullptr;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(event && SUCCEEDED(c.compositor.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence)) &&
               SUCCEEDED(c.queue->Signal(fence,1)) && SUCCEEDED(fence->SetEventOnCompletion(1,event)))
                drained=WaitForSingleObject(event,10000)==WAIT_OBJECT_0;
            if(fence)fence->Release();if(event)CloseHandle(event);
        }
        if(drained) {c.compositor.release();if(c.queue)c.queue->Release();c.queue=nullptr;c.context=nullptr;for(auto &cfg:c.config)cfg={};c.applied=c.declined=0;}
        // On a GPU timeout retain this bounded slot rather than free in-flight
        // descriptor heaps/resources. No unbounded allocation or wait/retry.
        else if(report)report("{\"event\":\"native_ui_retained_after_gpu_timeout\"}");
    }
    for(auto &q:nativeQueues)if(q.context==context){q.queue->Release();q={};}
    ReleaseSRWLockExclusive(&nativeLock);
}
