#pragma once
// Explicitly armed, bounded consecutive-frame recorder. This is independent of
// the installed FFX provider: it never changes its shader or output pixels.
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "../../../rosetta/fg-chain/chain.h"

struct SequenceCapture {
    static constexpr unsigned Capacity=8, Images=5;
    static constexpr uint64_t MaxBytes=1024ull*1024*1024;
    enum Stage { Empty, Prepared, Recorded, Submitted, Complete, Failed };
    struct Image {
        ID3D12Resource *source{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        D3D12_RESOURCE_STATES state{};
        UINT rows{};UINT64 rowBytes{},bytes{};
    };
    struct Frame {
        Stage stage{};uint64_t id{},bytes{};float milliseconds{};bool beforeRecorded{};
        ChainFrame native{};Image image[Images]{};
        ID3D12Resource *readback{};ID3D12Fence *fence{};
        // Borrowed identity only, never dereferenced. Source references are
        // retained separately until the completion fence has retired.
        ID3D12Resource *outputIdentity{};
    };
    Frame frames[Capacity]{};unsigned count{};bool armed{},aborted{};
    uint64_t allocated{},nextId{};

    bool arm() {
        if(armed||count||allocated)return false;
        armed=true;aborted=false;nextId=0;return true;
    }
    static bool shape(const D3D12_RESOURCE_DESC &d,DXGI_FORMAT format) {
        return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Format==format&&
            d.Width&&d.Height&&d.Width<=16384&&d.Height<=16384&&
            d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1;
    }
    static void release(Frame &f) {
        for(auto &i:f.image)if(i.source)i.source->Release();
        if(f.readback)f.readback->Release();if(f.fence)f.fence->Release();f={};
    }
    // No destructor frees outstanding GPU resources. The caller must drain
    // the exact producer queue, or retain the recorder on a device timeout.
    bool clearCompleted() {
        for(auto &f:frames)if(f.stage!=Empty&&f.stage!=Complete)return false;
        for(auto &f:frames)release(f);
        count=0;allocated=nextId=0;armed=aborted=false;return true;
    }
    void abort(){armed=false;aborted=true;}
    static bool layout(ID3D12Device *device,Frame &f,unsigned slot,
                       const D3D12_RESOURCE_DESC &desc) {
        auto &i=f.image[slot];UINT64 total=0;
        device->GetCopyableFootprints(&desc,0,1,0,&i.footprint,&i.rows,&i.rowBytes,&total);
        if(!i.rows||!i.footprint.Footprint.RowPitch||i.footprint.Footprint.Depth!=1)return false;
        f.bytes=(f.bytes+511)&~uint64_t(511);i.footprint.Offset=f.bytes;
        i.bytes=uint64_t(i.footprint.Footprint.RowPitch)*i.rows;f.bytes+=i.bytes;
        return f.bytes<=MaxBytes;
    }
    static void copy(ID3D12GraphicsCommandList *list,Frame &f,unsigned slot) {
        auto &i=f.image[slot];
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={i.source,0,i.state,D3D12_RESOURCE_STATE_COPY_SOURCE};
        if(i.state!=D3D12_RESOURCE_STATE_COPY_SOURCE)list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=i.source;
        from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=f.readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint=i.footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        if(i.state!=D3D12_RESOURCE_STATE_COPY_SOURCE){b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=i.state;list->ResourceBarrier(1,&b);}
    }
    Frame *prepare(ID3D12Device *device,ID3D12GraphicsCommandList *list,
                   const ChainFrame &native,float milliseconds,ID3D12Resource *depth,
                   ID3D12Resource *motion,D3D12_RESOURCE_STATES depthState,
                   D3D12_RESOURCE_STATES motionState,UINT width,UINT height) {
        if(!armed)return nullptr;
        if(count>=Capacity||!native.hasConstants||native.reset||!depth||!motion||
           depth==motion||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||
           !width||!height||width>16384||height>16384||milliseconds<=0||
           !(milliseconds<1000)|| (count&&native.frame!=nextId)){abort();return nullptr;}
        const auto dd=depth->GetDesc(),md=motion->GetDesc();
        if((!shape(dd,DXGI_FORMAT_R32_FLOAT)&&!shape(dd,DXGI_FORMAT_R32G8X24_TYPELESS))||!shape(md,DXGI_FORMAT_R16G16_FLOAT)||
           dd.Width!=md.Width||dd.Height!=md.Height){abort();return nullptr;}
        auto &f=frames[count];f.id=native.frame;f.native=native;f.milliseconds=milliseconds;
        D3D12_RESOURCE_DESC color=dd;color.Width=width;color.Height=height;
        color.Format=DXGI_FORMAT_R8G8B8A8_UNORM;color.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(!layout(device,f,0,dd)||!layout(device,f,1,md)||!layout(device,f,2,color)||
           !layout(device,f,3,color)||!layout(device,f,4,color)||f.image[0].rowBytes!=dd.Width*4||
           allocated+f.bytes>MaxBytes){release(f);abort();return nullptr;}
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width=f.bytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;
        buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&f.readback))||
           FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&f.fence))){release(f);abort();return nullptr;}
        f.image[0].source=depth;depth->AddRef();f.image[0].state=depthState;
        f.image[1].source=motion;motion->AddRef();f.image[1].state=motionState;
        f.stage=Prepared;allocated+=f.bytes;++count;nextId=native.frame+1;
        if(count==Capacity)armed=false;
        copy(list,f,0);copy(list,f,1);return &f;
    }
    bool recordBefore(ID3D12GraphicsCommandList *list,const ChainFrame &native,
                      ID3D12Resource *scene,D3D12_RESOURCE_STATES sceneState){
        Frame *f=nullptr;for(auto &candidate:frames)if(candidate.stage==Prepared&&candidate.id==native.frame){f=&candidate;break;}
        if(!f)return false;
        if(f->beforeRecorded||!scene||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||native.reset||
           !native.hasConstants||memcmp(&f->native.jitter,&native.jitter,offsetof(ChainFrame,tags)-offsetof(ChainFrame,jitter))){abort();return false;}
        const auto d=scene->GetDesc();const auto &expected=f->image[4].footprint.Footprint;
        if(!shape(d,DXGI_FORMAT_R8G8B8A8_UNORM)||d.Width!=expected.Width||d.Height!=expected.Height){abort();return false;}
        f->image[4].source=scene;scene->AddRef();f->image[4].state=sceneState;
        copy(list,*f,4);f->beforeRecorded=true;return true;
    }
    bool record(ID3D12GraphicsCommandList *list,const ChainFrame &native,
                ID3D12Resource *scene,ID3D12Resource *output,
                D3D12_RESOURCE_STATES sceneState,D3D12_RESOURCE_STATES outputState) {
        Frame *f=nullptr;for(auto &candidate:frames)if(candidate.stage==Prepared&&candidate.id==native.frame){f=&candidate;break;}
        if(!f)return false;
        if(!f->beforeRecorded||f->image[4].source!=scene||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||!scene||!output||scene==output||
           !native.hasConstants||native.reset){abort();return false;}
        const auto sd=scene->GetDesc(),od=output->GetDesc();
        const auto &expected=f->image[2].footprint.Footprint;
        if(!shape(sd,DXGI_FORMAT_R8G8B8A8_UNORM)||!shape(od,DXGI_FORMAT_R8G8B8A8_UNORM)||
           sd.Width!=expected.Width||sd.Height!=expected.Height||sd.Width!=od.Width||sd.Height!=od.Height){abort();return false;}
        // Matrices, motion units and jitter must describe the same Prepare;
        // later resource tags are allowed to arrive at the interpolation call.
        if(memcmp(&f->native.jitter,&native.jitter,
           offsetof(ChainFrame,tags)-offsetof(ChainFrame,jitter))){abort();return false;}
        f->native=native;f->image[2].source=scene;scene->AddRef();f->image[2].state=sceneState;
        f->image[3].source=output;output->AddRef();f->image[3].state=outputState;
        f->outputIdentity=output;copy(list,*f,2);copy(list,*f,3);f->stage=Recorded;return true;
    }
    // Call only once the SDK has queued the prior output-availability wait on
    // the verified direct interpolation queue. Never infer completion from a
    // CPU dispatch return or from an unrelated composition queue's fence.
    bool retire(ID3D12CommandQueue *queue,ID3D12Resource *reuse,uint64_t frame) {
        if(!queue||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
        for(auto &f:frames)if(f.stage==Recorded&&f.id<frame&&f.outputIdentity==reuse){
            if(FAILED(queue->Signal(f.fence,1))){abort();return false;}f.stage=Submitted;
        }return true;
    }
    // Caller has established all recorded Prepare/interpolate lists were
    // submitted on this queue. Also covers a declined/aborted partial sample.
    bool drainSubmitted(ID3D12CommandQueue *queue) {
        if(!queue||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
        for(auto &f:frames)if(f.stage==Prepared||f.stage==Recorded){
            if(FAILED(queue->Signal(f.fence,1))){abort();return false;}
            if(f.stage==Prepared)abort();f.stage=Submitted;
        }return true;
    }
    bool pollComplete() {
        bool all=count!=0;
        for(auto &f:frames)if(f.stage!=Empty){
            if(f.stage==Submitted){auto value=f.fence->GetCompletedValue();if(value==UINT64_MAX){f.stage=Failed;abort();}else if(value>=1)f.stage=Complete;}
            all=all&&f.stage==Complete;
        }return all;
    }
};
