#pragma once
// Test-only observer of the real FFX scheduler's composition callbacks.
// Readbacks are consumed only after FFX WaitForPresents; never on its threads.
struct ApplePresentProbe {
    struct Event {Texture pixels;uint64_t id{};bool generated{},native{};};
    Event events[Frames*2+4];
    bool nativeFrame[Frames]{};
    volatile LONG count{},errors{},dispatches{};
    ffxContext *context{};
    PfnFfxDispatch dispatch{};
    unsigned (*middle)(){};
    bool init(ID3D12Device *device){
        for(auto &e:events)if(!e.pixels.init(device,DXGI_FORMAT_R8G8B8A8_UNORM))return false;
        return true;
    }
    static D3D12_RESOURCE_STATES state(UINT v){
        UINT s=0;
        if(v&FFX_API_RESOURCE_STATE_PIXEL_READ)s|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if(v&FFX_API_RESOURCE_STATE_COMPUTE_READ)s|=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if(v&FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)s|=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if(v&FFX_API_RESOURCE_STATE_COPY_SRC)s|=D3D12_RESOURCE_STATE_COPY_SOURCE;
        if(v&FFX_API_RESOURCE_STATE_COPY_DEST)s|=D3D12_RESOURCE_STATE_COPY_DEST;
        if(v&FFX_API_RESOURCE_STATE_RENDER_TARGET)s|=D3D12_RESOURCE_STATE_RENDER_TARGET;
        return D3D12_RESOURCE_STATES(s);
    }
    static void barrier(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,
                        D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        if(before==after)return;D3D12_RESOURCE_BARRIER b{};
        b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
    }
    static ffxReturnCode_t generate(ffxDispatchDescFrameGeneration *p,void *user){
        auto &self=*(ApplePresentProbe*)user;const unsigned before=self.middle();
        auto result=self.dispatch(self.context,&p->header);
        if(p->frameID<Frames)self.nativeFrame[p->frameID]=self.middle()>before;
        else InterlockedIncrement(&self.errors);
        InterlockedIncrement(&self.dispatches);return result;
    }
    static ffxReturnCode_t present(ffxCallbackDescFrameGenerationPresent *p,void *user){
        auto &self=*(ApplePresentProbe*)user;
        LONG index=InterlockedIncrement(&self.count)-1;
        if(index>=LONG(Frames*2+4)||p->frameID>=Frames){InterlockedIncrement(&self.errors);return FFX_API_RETURN_ERROR;}
        auto &e=self.events[index];e.id=p->frameID;e.generated=p->isGeneratedFrame;e.native=self.nativeFrame[e.id];
        auto list=(ID3D12GraphicsCommandList*)p->commandList;
        auto source=(ID3D12Resource*)p->currentBackBuffer.resource;
        auto output=(ID3D12Resource*)p->outputSwapChainBuffer.resource;
        if(!source||!output||!list||source==output){InterlockedIncrement(&self.errors);return FFX_API_RETURN_ERROR;}
        const auto oldSource=state(p->currentBackBuffer.state),oldOutput=state(p->outputSwapChainBuffer.state);
        barrier(list,source,oldSource,D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(list,output,oldOutput,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(output,source);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=e.pixels.readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=e.pixels.fp;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier(list,output,D3D12_RESOURCE_STATE_COPY_DEST,oldOutput);
        barrier(list,source,D3D12_RESOURCE_STATE_COPY_SOURCE,oldSource);
        return FFX_API_RETURN_OK;
    }
    bool verify(unsigned expectedNative){
        if(errors||count!=LONG(Frames*2)||dispatches!=Frames)return false;
        unsigned nativePairs=0,uiErrors=0,originalErrors=0;static BYTE pixels[W*H*4];
        for(LONG n=0;n<count;n++){
            auto &e=events[n];if(!e.pixels.read(pixels,W*4))return false;
            if(e.id!=uint64_t(n/2)||e.generated!=(n%2==0))return false;
            for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){
                const bool car=x>=20+e.id*2&&x<52+e.id*2&&y>=40&&y<72;
                const BYTE base[3]={BYTE(car?100:25),BYTE(car?30:40),BYTE(car?20:60)};
                const BYTE alpha=(y<12&&x<40)?128:0,overlay[3]={BYTE(alpha?32+e.id*3:0),BYTE(alpha?96-e.id*3:0),BYTE(alpha/8)};
                for(UINT k=0;k<3;k++){
                    const BYTE expected=BYTE(overlay[k]+(1-alpha/255.f)*base[k]*(car?1.2f:1.f)+.5f);
                    if(!e.generated&&pixels[(y*W+x)*4+k]!=expected)originalErrors++;
                    if(alpha&&abs(int(pixels[(y*W+x)*4+k])-int(expected))>1)uiErrors++;
                }
            }
            if(!e.native)continue;
            unsigned area=0;double center=0;
            // Cover every non-UI row, so a duplicate below the object cannot
            // pass merely because it fell outside a narrow centroid ROI.
            for(UINT y=12;y<H;y++)for(UINT x=0;x<W;x++)if(pixels[(y*W+x)*4]>70){area++;center+=x;}
            if(area)center/=area;const double expected=35.5+2*e.id-(e.generated?1:0);
            printf("PRESENT index=%ld frame=%llu generated=%u native=%u area=%u centroid=%.3f expected=%.3f\n",n,(unsigned long long)e.id,e.generated,e.native,area,center,expected);
            if(area!=1024||fabs(center-expected)>.01)return false;
            // A generated callback must be immediately followed by its own
            // unmodified original, not another midpoint or another frame ID.
            if(e.generated){if(n+1>=count||events[n+1].generated||events[n+1].id!=e.id)return false;nativePairs++;}
        }
        printf("PRESENT summary callbacks=%ld dispatches=%ld native_pairs=%u errors=%ld ui_errors=%u original_errors=%u\n",count,dispatches,nativePairs,errors,uiErrors,originalErrors);
        return nativePairs==expectedNative&&nativePairs>=8&&!uiErrors&&!originalErrors;
    }
    void releaseAfterPresents(){for(auto &e:events){e.pixels.gpu->Release();e.pixels.upload->Release();e.pixels.readback->Release();}}
};
