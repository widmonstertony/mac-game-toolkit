// Public FidelityFX provider proxy. Same provider, context, resources and callbacks.
// It observes both sides of the Streamline -> FidelityFX conversion, using real
// frame tokens rather than pointer/jitter/time-window guesses.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include <framegeneration/include/dx12/ffx_api_framegeneration_dx12.h>
#include <upscalers/include/ffx_upscale.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "chain.h"

static HMODULE self;
static INIT_ONCE initOnce=INIT_ONCE_STATIC_INIT;
static PfnFfxCreateContext createContext;
static PfnFfxDestroyContext destroyContext;
static PfnFfxConfigure configure;
static PfnFfxQuery query;
static PfnFfxDispatch dispatch;
static ChainGet getFrame;
static ChainEvaluateFrame getEvaluateFrame;
static ChainReport report;
static volatile LONG recordCount;
static INIT_ONCE observerOnce=INIT_ONCE_STATIC_INIT;
#ifdef NATIVE_UI_FIX
#include "native-ui-provider.h"
#ifdef HISTORY_VISIBILITY_FIX
#include "d3dmetal-latency.h"
#endif
#endif

static BOOL CALLBACK initialize(PINIT_ONCE,PVOID,PVOID*)
{
    wchar_t path[32768]; DWORD n=GetModuleFileNameW(self,path,32768);
    if(!n || n>=32768) return TRUE;
    auto end=wcsrchr(path,L'\\'); if(!end) return TRUE;
#ifdef UPSCALER_PROXY
    wcscpy(end+1,L"upscaler-original.dll");
#else
    wcscpy(end+1,L"framegeneration-original.dll");
#endif
    HMODULE original=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!original || original==self) return TRUE;
    createContext=(PfnFfxCreateContext)GetProcAddress(original,"ffxCreateContext");
    destroyContext=(PfnFfxDestroyContext)GetProcAddress(original,"ffxDestroyContext");
    configure=(PfnFfxConfigure)GetProcAddress(original,"ffxConfigure");
    query=(PfnFfxQuery)GetProcAddress(original,"ffxQuery");
    dispatch=(PfnFfxDispatch)GetProcAddress(original,"ffxDispatch");
    return TRUE;
}
static BOOL CALLBACK initializeObserver(PINIT_ONCE,PVOID,PVOID*)
{
    HMODULE observer=GetModuleHandleW(L"fh6-fg-chain.asi");
    if(!observer) return FALSE;
    getFrame=(ChainGet)GetProcAddress(observer,"FH6ChainGet");
    getEvaluateFrame=(ChainEvaluateFrame)GetProcAddress(observer,"FH6ChainEvaluateFrame");
    report=(ChainReport)GetProcAddress(observer,"FH6ChainReport");
    if(report) report("{\"event\":\"provider_observer_connected\",\"kind\":\""
#ifdef UPSCALER_PROXY
        "sr"
#else
        "fg"
#endif
        "\"}");
    return report && getFrame && getEvaluateFrame;
}
static void connectObserver() { InitOnceExecuteOnce(&observerOnce,initializeObserver,nullptr,nullptr); }
static bool sample(uint64_t frame)
{
    const LONG n=InterlockedIncrement(&recordCount);
    return n<=160 || (n<=400000 && frame!=UINT64_MAX && frame%60==0);
}
static int resource(char *out,size_t size,const FfxApiResource &r)
{
    return snprintf(out,size,"{\"ptr\":\"%llx\",\"size\":[%u,%u],\"format\":%u,\"state\":%u}",
        (unsigned long long)r.resource,r.description.width,r.description.height,
        r.description.format,r.state);
}
static int frameData(char *out,size_t size,const ChainFrame &f)
{
    int n=snprintf(out,size,"{\"frame\":%llu,\"viewport\":%u,\"constants\":%u,\"constantSequence\":%llu,\"evaluateSequence\":%llu,"
        "\"jitter\":[%.9g,%.9g],\"scale\":[%.9g,%.9g],\"camera\":[%.9g,%.9g,%.9g,%.9g],"
        "\"flags\":[%u,%u,%u,%u,%u,%u],\"tags\":[",
        (unsigned long long)f.frame,f.viewport,f.hasConstants,(unsigned long long)f.constantsSequence,
        (unsigned long long)f.evaluateSequence,f.jitter[0],f.jitter[1],f.scale[0],f.scale[1],
        f.nearPlane,f.farPlane,f.fov,f.aspect,f.inverted,f.cameraMotionIncluded,f.motion3D,f.dilated,f.jittered,f.reset);
    for(unsigned i=0;i<6 && n>0 && (size_t)n<size;i++) {
        const auto &r=f.tags[i];
        n+=snprintf(out+n,size-n,"%s{\"type\":%u,\"ptr\":\"%llx\",\"sequence\":%llu,\"life\":%u,\"state\":%u,\"format\":%u,\"size\":[%u,%u],\"extent\":[%u,%u,%u,%u]}",
            i?",":"",r.type,(unsigned long long)r.native,(unsigned long long)r.sequence,r.lifecycle,r.state,r.format,
            r.width,r.height,r.left,r.top,r.extentWidth,r.extentHeight);
    }
    if(n>0 && (size_t)n<size) n+=snprintf(out+n,size-n,"]");
    // Retain exact float bits, including invalid/NaN sentinels, as valid JSON.
    const float *arrays[]={f.projection,f.clipToPrevious,f.cameraBasis};
    const unsigned sizes[]={16,16,12};
    const char *names[]={"projectionBits","clipToPreviousBits","cameraBasisBits"};
    for(unsigned a=0;a<3 && n>0 && (size_t)n<size;a++) {
        n+=snprintf(out+n,size-n,",\"%s\":[",names[a]);
        for(unsigned i=0;i<sizes[a] && n>0 && (size_t)n<size;i++) {
            uint32_t bits;memcpy(&bits,arrays[a]+i,sizeof(bits));
            n+=snprintf(out+n,size-n,"%s%u",i?",":"",bits);
        }
        if(n>0 && (size_t)n<size)n+=snprintf(out+n,size-n,"]");
    }
    if(n>0 && (size_t)n<size)n+=snprintf(out+n,size-n,"}");
    return n;
}
template<typename Prepare> static void logPrepare(ffxContext context,const Prepare &p,ffxReturnCode_t result)
{
    connectObserver(); if(!report || !sample(p.frameID)) return;
    ChainFrame f{}; const bool found=getFrame && getFrame(p.frameID,&f);
    char depth[220],motion[220],native[3200]="null",line[4800];
    resource(depth,sizeof(depth),p.depth);resource(motion,sizeof(motion),p.motionVectors);
    if(found && frameData(native,sizeof(native),f)>=(int)sizeof(native)) strcpy(native,"null");
    int n=snprintf(line,sizeof(line),"{\"event\":\"prepare\",\"context\":\"%llx\",\"contextFrame\":%llu,\"cmd\":\"%llx\",\"result\":%u,\"render\":[%u,%u],"
        "\"jitter\":[%.9g,%.9g],\"scale\":[%.9g,%.9g],\"camera\":[%.9g,%.9g,%.9g,%.9g],\"depth\":%s,\"motion\":%s,\"native\":%s}",
        (unsigned long long)context,(unsigned long long)p.frameID,(unsigned long long)p.commandList,result,p.renderSize.width,p.renderSize.height,
        p.jitterOffset.x,p.jitterOffset.y,p.motionVectorScale.x,p.motionVectorScale.y,
        p.cameraNear,p.cameraFar,p.cameraFovAngleVertical,p.viewSpaceToMetersFactor,depth,motion,native);
    if(n>0 && n<(int)sizeof(line)) report(line);
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxCreateContext(ffxContext *ctx,ffxCreateContextDescHeader *desc,const ffxAllocationCallbacks *mem)
{
    InitOnceExecuteOnce(&initOnce,initialize,nullptr,nullptr);
    connectObserver();
#if defined(NATIVE_UI_FIX) && defined(HISTORY_VISIBILITY_FIX)
    protectD3DMetalLatency(desc);
#endif
    auto result=createContext?createContext(ctx,desc,mem):FFX_API_RETURN_NO_PROVIDER;
#ifdef NATIVE_UI_FIX
    nativeCreated(ctx?*ctx:nullptr,desc,result);
#endif
    if(report) { char line[200];snprintf(line,sizeof(line),"{\"event\":\"create\",\"type\":%llu,\"context\":\"%llx\",\"result\":%u}",
        (unsigned long long)(desc?desc->type:0),(unsigned long long)(ctx?*ctx:nullptr),result); report(line); }
    if(report && desc && desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION) {
        const auto &p=*(const ffxCreateContextDescFrameGeneration*)desc;char line[400];
        snprintf(line,sizeof(line),"{\"event\":\"create_fg\",\"context\":\"%llx\",\"flags\":%u,\"display\":[%u,%u],\"maxRender\":[%u,%u],\"backbufferFormat\":%u}",
            (unsigned long long)(ctx?*ctx:nullptr),p.flags,p.displaySize.width,p.displaySize.height,p.maxRenderSize.width,p.maxRenderSize.height,p.backBufferFormat);
        report(line);
    }
    if(report && desc) {
        ID3D12CommandQueue *queue=nullptr;
        if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12)
            queue=((const ffxCreateContextDescFrameGenerationSwapChainWrapDX12*)desc)->gameQueue;
        else if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12)
            queue=((const ffxCreateContextDescFrameGenerationSwapChainNewDX12*)desc)->gameQueue;
        else if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12)
            queue=((const ffxCreateContextDescFrameGenerationSwapChainForHwndDX12*)desc)->gameQueue;
        if(queue) {char line[200];snprintf(line,sizeof(line),"{\"event\":\"swapchain_queue\",\"context\":\"%llx\",\"queue\":\"%llx\"}",
            (unsigned long long)(ctx?*ctx:nullptr),(unsigned long long)queue);report(line);}
    }
    return result;
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxDestroyContext(ffxContext *ctx,const ffxAllocationCallbacks *mem)
{
    InitOnceExecuteOnce(&initOnce,initialize,nullptr,nullptr);
#ifdef NATIVE_UI_FIX
    const auto original=ctx?*ctx:nullptr;
    auto result=destroyContext?destroyContext(ctx,mem):FFX_API_RETURN_NO_PROVIDER;
    nativeDestroyed(original,result);return result;
#else
    return destroyContext?destroyContext(ctx,mem):FFX_API_RETURN_NO_PROVIDER;
#endif
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxQuery(ffxContext *ctx,ffxQueryDescHeader *desc)
{
    InitOnceExecuteOnce(&initOnce,initialize,nullptr,nullptr);
    return query?query(ctx,desc):FFX_API_RETURN_NO_PROVIDER;
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxConfigure(ffxContext *ctx,const ffxConfigureDescHeader *desc)
{
    InitOnceExecuteOnce(&initOnce,initialize,nullptr,nullptr);
    const auto result=configure?configure(ctx,desc):FFX_API_RETURN_NO_PROVIDER;
    connectObserver();
#ifdef NATIVE_UI_FIX
    nativeConfigured(ctx?*ctx:nullptr,desc,result);
#endif
    if(report && desc && desc->type==FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION && sample(((const ffxConfigureDescFrameGeneration*)desc)->frameID)) {
        const auto &p=*(const ffxConfigureDescFrameGeneration*)desc;
        char color[220],line[800];resource(color,sizeof(color),p.HUDLessColor);
        snprintf(line,sizeof(line),"{\"event\":\"configure\",\"context\":\"%llx\",\"frame\":%llu,\"enabled\":%u,\"rect\":[%d,%d,%d,%d],\"hudless\":%s,\"result\":%u}",
            (unsigned long long)(ctx?*ctx:nullptr),(unsigned long long)p.frameID,(unsigned)p.frameGenerationEnabled,
            p.generationRect.left,p.generationRect.top,p.generationRect.width,p.generationRect.height,color,result);
        report(line);
    }
    if(report && desc && desc->type==FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12 && sample(getEvaluateFrame?getEvaluateFrame():UINT64_MAX)) {
        const auto &p=*(const ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12*)desc;
        char ui[220],line[400];resource(ui,sizeof(ui),p.uiResource);
        snprintf(line,sizeof(line),"{\"event\":\"register_ui\",\"context\":\"%llx\",\"flags\":%u,\"ui\":%s,\"result\":%u}",
            (unsigned long long)(ctx?*ctx:nullptr),p.flags,ui,result);report(line);
    }
    return result;
}
extern "C" __declspec(dllexport) ffxReturnCode_t ffxDispatch(ffxContext *ctx,const ffxDispatchDescHeader *desc)
{
    InitOnceExecuteOnce(&initOnce,initialize,nullptr,nullptr);
#ifdef NATIVE_UI_FIX
    connectObserver();
    const auto result=nativeDispatch(ctx,desc);
#else
    const auto result=dispatch?dispatch(ctx,desc):FFX_API_RETURN_NO_PROVIDER;
#endif
    if(!desc) return result;
    connectObserver();
    if(desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE)
        logPrepare(ctx?*ctx:nullptr,*(const ffxDispatchDescFrameGenerationPrepare*)desc,result);
    else if(desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2)
        logPrepare(ctx?*ctx:nullptr,*(const ffxDispatchDescFrameGenerationPrepareV2*)desc,result);
    else if(report && desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION && sample(((const ffxDispatchDescFrameGeneration*)desc)->frameID)) {
        const auto &p=*(const ffxDispatchDescFrameGeneration*)desc;
        char color[220],output[220],line[900];resource(color,sizeof(color),p.presentColor);resource(output,sizeof(output),p.outputs[0]);
        snprintf(line,sizeof(line),"{\"event\":\"interpolate\",\"context\":\"%llx\",\"frame\":%llu,\"frames\":%u,\"reset\":%u,\"rect\":[%d,%d,%d,%d],\"present\":%s,\"output\":%s,\"result\":%u}",
            (unsigned long long)(ctx?*ctx:nullptr),(unsigned long long)p.frameID,p.numGeneratedFrames,(unsigned)p.reset,
            p.generationRect.left,p.generationRect.top,p.generationRect.width,p.generationRect.height,color,output,result);
        report(line);
    } else if(report && desc->type==FFX_API_DISPATCH_DESC_TYPE_UPSCALE && sample(getEvaluateFrame?getEvaluateFrame():UINT64_MAX)) {
        const auto &p=*(const ffxDispatchDescUpscale*)desc;
        const uint64_t frame=getEvaluateFrame?getEvaluateFrame():UINT64_MAX;
        char color[220],depth[220],motion[220],output[220],line[1700];
        resource(color,sizeof(color),p.color);resource(depth,sizeof(depth),p.depth);resource(motion,sizeof(motion),p.motionVectors);resource(output,sizeof(output),p.output);
        snprintf(line,sizeof(line),"{\"event\":\"upscale\",\"frame\":%llu,\"frameKnown\":%u,\"cmd\":\"%llx\",\"render\":[%u,%u],\"scale\":[%.9g,%.9g],\"jitter\":[%.9g,%.9g],\"color\":%s,\"depth\":%s,\"motion\":%s,\"output\":%s,\"result\":%u}",
            (unsigned long long)frame,(unsigned)(frame!=UINT64_MAX),(unsigned long long)p.commandList,p.renderSize.width,p.renderSize.height,
            p.motionVectorScale.x,p.motionVectorScale.y,p.jitterOffset.x,p.jitterOffset.y,color,depth,motion,output,result);
        report(line);
    }
    return result;
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,void*)
{
    if(reason==DLL_PROCESS_ATTACH) self=module;
    return TRUE;
}
