// Authored interface/lifetime test. No game or NVIDIA algorithm is emulated.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <sl.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include <cstdio>
#include <cstring>
#include "chain.h"

#ifdef SL_FIXTURE
extern "C" __declspec(dllexport) sl::Result slSetConstants(const sl::Constants&,const sl::FrameToken&,const sl::ViewportHandle&) { return sl::Result::eOk; }
extern "C" __declspec(dllexport) sl::Result slSetTagForFrame(const sl::FrameToken&,const sl::ViewportHandle&,const sl::ResourceTag*,uint32_t,sl::CommandBuffer*) { return sl::Result::eOk; }
extern "C" __declspec(dllexport) sl::Result slSetTag(const sl::ViewportHandle&,const sl::ResourceTag*,uint32_t,sl::CommandBuffer*) { return sl::Result::eOk; }
extern "C" __declspec(dllexport) sl::Result slEvaluateFeature(sl::Feature,const sl::FrameToken &f,const sl::BaseStructure**,uint32_t,sl::CommandBuffer*)
{
    HMODULE m=GetModuleHandleW(L"fh6-fg-chain.asi");
    auto current=m?(ChainEvaluateFrame)GetProcAddress(m,"FH6ChainEvaluateFrame"):nullptr;
    return current && current()==(uint32_t)f?sl::Result::eOk:sl::Result::eErrorInvalidParameter;
}
#elif FFX_FIXTURE
static const void *lastDesc; static uint64_t lastType;
extern "C" __declspec(dllexport) const void *FixtureLastDesc() { return lastDesc; }
extern "C" __declspec(dllexport) ffxReturnCode_t ffxCreateContext(ffxContext *c,ffxCreateContextDescHeader *d,const ffxAllocationCallbacks*) { lastDesc=d;lastType=d?d->type:0;if(c)*c=(void*)0x1234;return FFX_API_RETURN_OK; }
extern "C" __declspec(dllexport) ffxReturnCode_t ffxDestroyContext(ffxContext*,const ffxAllocationCallbacks*) { return FFX_API_RETURN_OK; }
extern "C" __declspec(dllexport) ffxReturnCode_t ffxQuery(ffxContext*,ffxQueryDescHeader *d) { lastDesc=d;return FFX_API_RETURN_ERROR; }
extern "C" __declspec(dllexport) ffxReturnCode_t ffxConfigure(ffxContext*,const ffxConfigureDescHeader *d) { lastDesc=d;lastType=d?d->type:0;return FFX_API_RETURN_OK; }
extern "C" __declspec(dllexport) ffxReturnCode_t ffxDispatch(ffxContext*,const ffxDispatchDescHeader *d) { lastDesc=d;lastType=d?d->type:0;return FFX_API_RETURN_OK; }
#else
struct Token:sl::FrameToken { uint32_t id;Token(uint32_t i):id(i){}operator uint32_t()const override{return id;} };
// Load the IAT entry after installing the observer. An optimizer may cache it
// across InitializeASI inside one long caller, which is not a loader-time test.
__attribute__((noinline)) static FARPROC resolve(HMODULE m,const char *n) { return GetProcAddress(m,n); }
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static int checkGpuContents(ID3D12Device *device,ID3D12Resource *texture)
{
    const auto desc=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows;UINT64 bytes,total;
    device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&bytes,&total);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;
    bd.Height=bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource *upload,*readback;
    CHECK(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)&upload)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    CHECK(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&readback)));
    BYTE *mapped;D3D12_RANGE empty{0,0};CHECK(SUCCEEDED(upload->Map(0,&empty,(void**)&mapped)));
    for(UINT y=0;y<desc.Height;y++) for(UINT x=0;x<desc.Width;x++)
        *(float*)(mapped+fp.Offset+y*fp.Footprint.RowPitch+x*4)=float(y*desc.Width+x)+0.25f;
    upload->Unmap(0,nullptr);
    ID3D12CommandQueue *queue;ID3D12CommandAllocator *allocator;ID3D12GraphicsCommandList *list;
    D3D12_COMMAND_QUEUE_DESC q{};CHECK(SUCCEEDED(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,(void**)&queue)));
    CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&allocator)));
    CHECK(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_ID3D12GraphicsCommandList,(void**)&list)));
    D3D12_TEXTURE_COPY_LOCATION in{},gpu{},out{};
    in.pResource=upload;in.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;in.PlacedFootprint=fp;
    gpu.pResource=texture;gpu.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    out.pResource=readback;out.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;out.PlacedFootprint=fp;
    list->CopyTextureRegion(&gpu,0,0,0,&in,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&barrier);list->CopyTextureRegion(&out,0,0,0,&gpu,nullptr);CHECK(SUCCEEDED(list->Close()));
    ID3D12CommandList *lists[]={list};queue->ExecuteCommandLists(1,lists);
    ID3D12Fence *fence;CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);
    CHECK(SUCCEEDED(queue->Signal(fence,1))&&SUCCEEDED(fence->SetEventOnCompletion(1,event)));
    CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    D3D12_RANGE range{0,(SIZE_T)total};CHECK(SUCCEEDED(readback->Map(0,&range,(void**)&mapped)));
    unsigned errors=0;
    for(UINT y=0;y<desc.Height;y++) for(UINT x=0;x<desc.Width;x++)
        if(*(float*)(mapped+fp.Offset+y*fp.Footprint.RowPitch+x*4)!=float(y*desc.Width+x)+0.25f) errors++;
    readback->Unmap(0,nullptr);CHECK(errors==0);
    printf("PASS real D3DMetal GPU copy/fence/readback pixels=%u errors=%u\n",(unsigned)(desc.Width*desc.Height),errors);
    CloseHandle(event);fence->Release();list->Release();allocator->Release();queue->Release();upload->Release();readback->Release();
    return 0;
}
int main()
{
    setvbuf(stdout,nullptr,_IONBF,0);
    HMODULE a=LoadLibraryW(L"fh6-fg-chain.asi"); CHECK(a);
    auto init=(void(*)())GetProcAddress(a,"InitializeASI");auto get=(ChainGet)GetProcAddress(a,"FH6ChainGet");
    auto current=(ChainEvaluateFrame)GetProcAddress(a,"FH6ChainEvaluateFrame");CHECK(init&&get&&current);init();
    HMODULE s=LoadLibraryW(L"sl.interposer.dll");CHECK(s);
    auto constants=(PFun_slSetConstants*)resolve(s,"slSetConstants");
    auto tags=(PFun_slSetTagForFrame*)resolve(s,"slSetTagForFrame");
    auto eval=(PFun_slEvaluateFeature*)resolve(s,"slEvaluateFeature");CHECK(constants&&tags&&eval);
    ID3D12Device *device;CHECK(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)&device)));
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=128;rd.Height=96;
    rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Format=DXGI_FORMAT_R32_FLOAT;
    ID3D12Resource *depth;CHECK(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&depth)));
    sl::Resource resource(sl::ResourceType::eTex2d,depth,D3D12_RESOURCE_STATE_COPY_DEST);
    sl::Extent extent{};extent.left=8;extent.top=4;extent.width=112;extent.height=88;
    sl::ResourceTag tag(&resource,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&extent);
    sl::Constants c{};c.cameraNear=0.25f;c.cameraFar=1000;c.mvecScale={0.5f,-0.5f};c.jitterOffset={0.25f,-0.125f};
    c.cameraMotionIncluded=sl::Boolean::eTrue;c.depthInverted=sl::Boolean::eTrue;
    Token t(47),late(46);sl::ViewportHandle view(0);
    const auto before=c;const auto tagBefore=tag;
    CHECK(constants(c,t,view)==sl::Result::eOk);CHECK(tags(t,view,&tag,1,nullptr)==sl::Result::eOk);
    CHECK(!memcmp(&c,&before,sizeof(c))&&!memcmp(&tag,&tagBefore,sizeof(tag)));
    CHECK(eval(sl::kFeatureDLSS,t,nullptr,0,nullptr)==sl::Result::eOk);CHECK(current()==UINT64_MAX);
    ChainFrame f{};CHECK(get(47,&f));CHECK(f.frame==47&&f.hasConstants&&f.tags[0].native==(uint64_t)depth);
    CHECK(f.tags[0].width==128&&f.tags[0].height==96&&f.tags[0].left==8&&f.tags[0].top==4&&f.tags[0].extentWidth==112&&f.tags[0].extentHeight==88);
    c.jitterOffset={-0.25f,0.125f};CHECK(constants(c,late,view)==sl::Result::eOk);ChainFrame old{};CHECK(get(46,&old));CHECK(get(47,&f));CHECK(f.jitter[0]==0.25f&&old.jitter[0]==-0.25f);
    sl::ResourceTag untag(nullptr,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent);
    CHECK(tags(late,view,&untag,1,nullptr)==sl::Result::eOk);CHECK(get(46,&old)&&old.tagsMask==0);CHECK(get(47,&f)&&f.tagsMask==1);
    puts("PASS actual D3DMetal resource descriptors, original constants/tags unchanged, late frame isolation, untag, evaluate token scope");
    HMODULE proxy=LoadLibraryW(L"framegeneration-audit.dll");CHECK(proxy);
    auto create=(PfnFfxCreateContext)GetProcAddress(proxy,"ffxCreateContext");auto configure=(PfnFfxConfigure)GetProcAddress(proxy,"ffxConfigure");
    auto dispatch=(PfnFfxDispatch)GetProcAddress(proxy,"ffxDispatch");auto query=(PfnFfxQuery)GetProcAddress(proxy,"ffxQuery");CHECK(create&&configure&&dispatch&&query);
    ffxCreateContextDescFrameGeneration cd{};cd.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    cd.displaySize={128,96};cd.maxRenderSize={128,96};ffxContext ctx=nullptr;
    CHECK(create(&ctx,&cd.header,nullptr)==FFX_API_RETURN_OK&&ctx==(void*)0x1234);
    auto original=GetModuleHandleW(L"framegeneration-original.dll");CHECK(original);
    auto last=(const void*(*)())GetProcAddress(original,"FixtureLastDesc");CHECK(last&&last()==&cd.header);
    ffxConfigureDescFrameGeneration cfg{};cfg.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;cfg.frameID=47;cfg.frameGenerationEnabled=true;cfg.generationRect={8,4,112,88};
    CHECK(configure(&ctx,&cfg.header)==FFX_API_RETURN_OK&&last()==&cfg.header);
    ffxDispatchDescFrameGenerationPrepare p{};p.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;p.frameID=47;
    p.renderSize={112,88};p.motionVectorScale={56,-44};p.jitterOffset={0.25f,-0.125f};p.depth.resource=depth;p.depth.description.width=128;p.depth.description.height=96;
    const auto beforePrepare=p;CHECK(dispatch(&ctx,&p.header)==FFX_API_RETURN_OK&&last()==&p.header&&!memcmp(&p,&beforePrepare,sizeof(p)));
    ffxDispatchDescFrameGeneration g{};g.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;g.frameID=47;g.numGeneratedFrames=1;g.generationRect={8,4,112,88};
    CHECK(dispatch(&ctx,&g.header)==FFX_API_RETURN_OK&&last()==&g.header&&g.numGeneratedFrames==1);
    ffxQueryDescHeader q{};CHECK(query(&ctx,&q)==FFX_API_RETURN_ERROR&&last()==&q);
    puts("PASS FFX descriptor pointer identity, contexts, 2x request, rectangles, data and failure propagation unchanged");
    CHECK(checkGpuContents(device,depth)==0);
    return 0;
}
#endif
