#pragma once
// Bounded GPU-only Prepare snapshots. Frame-resource identity alone is not a
// lifetime guarantee: the game may overwrite depth/motion before interpolation.
// Retire only once the FFX scheduler reuses a previously submitted output on the
// same direct queue. A queue Signal then covers that slot's prior submission.
#include "apple-command-backend.h"
#include <initializer_list>
struct AppleFrameInputs {
    enum Stage { Free, Prepared, Recorded, Retiring };
    struct Slot {
        Stage stage{};ChainFrame frame{};
        ID3D12Resource *depth{},*motion{},*sourceDepth{},*sourceMotion{};
        ID3D12Resource *output{}; // Retained, not merely an address comparison.
        ID3D12Fence *fence{};UINT64 value{};
        ID3D12DescriptorHeap *visibilityHeap{};
        Slot *previousRead{};
        unsigned readers{};
        bool completed{};
    } slots[8];
    Slot *latest{};
    ID3D12Device *device{};ID3D12CommandQueue *queue{};
    HRESULT status{S_OK};const char *reason="unused";
    bool initialize(ID3D12Device *d,ID3D12CommandQueue *q){
        if(device)return device==d&&queue==q;
        if(!d||!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
        ID3D12Device *owner{};if(FAILED(q->GetDevice(IID_ID3D12Device,(void**)&owner)))return false;
        bool same=d==owner;owner->Release();if(!same)return false;
        device=d;d->AddRef();queue=q;q->AddRef();return true;
    }
    static void unpin(Slot &s){
        if(s.sourceDepth)s.sourceDepth->Release();if(s.sourceMotion)s.sourceMotion->Release();if(s.output)s.output->Release();
        s.sourceDepth=s.sourceMotion=s.output=nullptr;s.stage=Free;s.frame={};s.completed=false;
    }
    void poll(){
        for(auto &s:slots)if(s.stage==Retiring&&!s.completed){
            auto completed=s.fence->GetCompletedValue();
            if(completed==UINT64_MAX||completed<s.value)continue;
            s.completed=true;
            if(s.previousRead){--s.previousRead->readers;s.previousRead=nullptr;}
        }
        for(auto &s:slots)if (s.completed && !s.readers && &s != latest) unpin(s);
    }
    bool retireReusedOutput(ID3D12Resource *output,uint64_t nextFrame){
        if(!queue||!output)return false;
        for(auto &s:slots)if(s.stage==Recorded&&s.output==output&&s.frame.frame<nextFrame){
            if(!s.fence){status=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&s.fence);if(FAILED(status))return false;}
            status=queue->Signal(s.fence,++s.value);if(FAILED(status))return false;s.stage=Retiring;
        }poll();return true;
    }
    static bool sameShape(ID3D12Resource *a,const D3D12_RESOURCE_DESC &b){if(!a)return false;auto d=a->GetDesc();
        return d.Width==b.Width&&d.Height==b.Height&&d.Format==b.Format;}
    bool allocate(ID3D12Resource **out,D3D12_RESOURCE_DESC desc){
        if(sameShape(*out,desc))return true;
        if(*out){(*out)->Release();*out=nullptr;}
        desc.Flags=D3D12_RESOURCE_FLAGS(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        status=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_ID3D12Resource,(void**)out);
        return SUCCEEDED(status);
    }
    static void copy(ID3D12GraphicsCommandList *list,ID3D12Resource *source,ID3D12Resource *dest,D3D12_RESOURCE_STATES sourceState){
        D3D12_RESOURCE_BARRIER b[2]{};for(auto &v:b)v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        // Copy the depth plane only; never change the original stencil state.
        b[0].Transition={source,0,sourceState,D3D12_RESOURCE_STATE_COPY_SOURCE};
        b[1].Transition={dest,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};
        list->ResourceBarrier(2,b);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;to.pResource=dest;
        from.Type=to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b[0].Transition.StateAfter=sourceState;
        b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(2,b);
    }
    Slot *prepare(ID3D12GraphicsCommandList *list,const ChainFrame &frame,ID3D12Resource *depth,ID3D12Resource *motion,
                  UINT rw,UINT rh,D3D12_RESOURCE_STATES depthState,D3D12_RESOURCE_STATES motionState){
        if(!device||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||!AppleCommandFg::constants(frame)||
           (!AppleCommandFg::texture(depth,rw,rh,DXGI_FORMAT_R32_FLOAT)&&!AppleCommandFg::texture(depth,rw,rh,DXGI_FORMAT_R32G8X24_TYPELESS))||
           !AppleCommandFg::texture(motion,rw,rh,DXGI_FORMAT_R16G16_FLOAT)||
           !(depthState&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)||!(motionState&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)){reason="invalid Prepare";return nullptr;}
        ID3D12Device *owner{};if(FAILED(list->GetDevice(IID_ID3D12Device,(void**)&owner)))return nullptr;
        bool same=owner==device;owner->Release();if(!same)return nullptr;
        for(auto r:{depth,motion}){owner=nullptr;if(FAILED(r->GetDevice(IID_ID3D12Device,(void**)&owner)))return nullptr;
            same=owner==device;owner->Release();if(!same)return nullptr;}
        poll();Slot *free=nullptr;for(auto &s:slots){if(s.stage!=Free&&s.frame.frame==frame.frame){reason="duplicate Prepare";return nullptr;}if(s.stage==Free&&!free)free=&s;}
        if(!free){reason="GPU snapshots still in flight";return nullptr;}
        if(!allocate(&free->depth,depth->GetDesc())||!allocate(&free->motion,motion->GetDesc())){reason="snapshot allocation";return nullptr;}
        free->frame=frame;free->sourceDepth=depth;depth->AddRef();free->sourceMotion=motion;motion->AddRef();free->stage=Prepared;
        copy(list,depth,free->depth,depthState);copy(list,motion,free->motion,motionState);reason="Prepare copied on GPU";return free;
    }
    Slot *find(uint64_t frame){for(auto &s:slots)if(s.stage==Prepared&&s.frame.frame==frame)return &s;return nullptr;}
    bool recorded(Slot *s,ID3D12Resource *output){if(!s||s->stage!=Prepared||!output)return false;
        s->output=output;output->AddRef();s->stage=Recorded;latest=s;return true;}
    // Must be called after the context has stopped recording AND its owning
    // queue's completion fence retired. Includes orphaned Prepare-only slots.
    void releaseAfterGpuCompletion(){latest=nullptr;for(auto &s:slots){unpin(s);if(s.depth)s.depth->Release();if(s.motion)s.motion->Release();if(s.fence)s.fence->Release();if(s.visibilityHeap)s.visibilityHeap->Release();s={};}
        if(queue)queue->Release();if(device)device->Release();queue=nullptr;device=nullptr;}
};
