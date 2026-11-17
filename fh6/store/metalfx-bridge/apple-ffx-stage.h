#pragma once
#include "apple-frame-inputs.h"

// FH6-only adapter. The existing FFX scheduler still owns submission and
// presentation. This stage records into its list; it never submits or waits.
struct AppleFfxStage {
    AppleCommandFg backend;
    AppleFrameInputs inputs;
    uint64_t copied{}, middle{}, warmup{}, declined{};
    bool lastWasNative{};
    const char *reason="unused";

    template<typename Prepare>
    bool prepare(ID3D12Device *device,ID3D12CommandQueue *queue,
                 const ChainFrame &f,const Prepare &p,bool reset) {
        const auto rw=p.renderSize.width,rh=p.renderSize.height;
        if(p.header.pNext||f.frame!=p.frameID||!AppleCommandFg::constants(f)||
           f.tags[0].native!=(uint64_t)p.depth.resource||f.tags[1].native!=(uint64_t)p.motionVectors.resource||
           f.tags[0].lifecycle!=1||f.tags[1].lifecycle!=1||
           f.tags[0].left||f.tags[0].top||f.tags[1].left||f.tags[1].top||
           f.tags[0].width!=rw||f.tags[0].height!=rh||f.tags[1].width!=rw||f.tags[1].height!=rh||
           p.depth.state!=FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ||
           p.motionVectors.state!=FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ) {
            reason="unmatched Prepare contract";return false;
        }
        // FFX has converted normalized native vectors to render-pixel scales.
        if(fabsf(f.scale[0]*rw-p.motionVectorScale.x)>0.001f||
           fabsf(f.scale[1]*rh-p.motionVectorScale.y)>0.001f||
           f.jitter[0]!=p.jitterOffset.x||f.jitter[1]!=p.jitterOffset.y) {
            reason="Prepare constants differ";return false;
        }
        if(!inputs.initialize(device,queue)){reason="unknown producer queue";return false;}
        auto saved=f;saved.reset|=reset;
        auto slot=inputs.prepare((ID3D12GraphicsCommandList*)p.commandList,saved,
            (ID3D12Resource*)p.depth.resource,(ID3D12Resource*)p.motionVectors.resource,rw,rh,
            D3D12_RESOURCE_STATES(f.tags[0].state),D3D12_RESOURCE_STATES(f.tags[1].state));
        reason=inputs.reason;if(slot)++copied;return slot!=nullptr;
    }
    bool record(ID3D12Device *device,const ChainFrame &f,
                const ffxDispatchDescFrameGeneration &p,ID3D12Resource *scene) {
        auto slot=inputs.find(p.frameID);
        if(!slot||slot->frame.constantsSequence!=f.constantsSequence||
           slot->frame.tags[0].native!=f.tags[0].native||slot->frame.tags[1].native!=f.tags[1].native||
           p.header.pNext||p.numGeneratedFrames!=1||
           p.backbufferTransferFunction!=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB) {
            reason="missing same-frame inputs or unsupported output";++declined;return false;
        }
        const auto d=scene->GetDesc(),z=slot->depth->GetDesc();
        if(!backend.initialize(device,UINT(d.Width),d.Height,UINT(z.Width),z.Height)) {
            reason=backend.reason;++declined;return false;
        }
        auto saved=slot->frame;saved.reset|=p.reset;
        bool generated=false;
        bool ok=backend.record((ID3D12GraphicsCommandList*)p.commandList,saved,scene,
            slot->depth,slot->motion,(ID3D12Resource*)p.outputs[0].resource,generated);
        reason=backend.reason;
        if(ok&&generated){++middle;lastWasNative=true;return true;}
        if(ok)++warmup;else ++declined;
        // Startup copies are not counted/presented as native interpolation.
        // The caller runs original FSR FG after this work for those samples.
        return false;
    }
    void retire(ID3D12Resource *output,uint64_t frame){inputs.retireReusedOutput(output,frame);}
    void recorded(uint64_t frame,ID3D12Resource *output){if(auto s=inputs.find(frame))inputs.recorded(s,output);}
    bool releaseAfterGpuCompletion(){
        if(!backend.releaseAfterGpuCompletion())return false;
        inputs.releaseAfterGpuCompletion();backend=AppleCommandFg{};
        copied=middle=warmup=declined=0;lastWasNative=false;reason="unused";return true;
    }
};
