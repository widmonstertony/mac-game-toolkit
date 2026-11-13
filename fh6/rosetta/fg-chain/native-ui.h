#pragma once
// Owned, FH6-private repair. Native Streamline UI is premultiplied, not an
// image-difference mask. This compositor touches generated output ONLY.
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <framegeneration/include/ffx_framegeneration.h>
#include "chain.h"

struct NativeUiCompositor {
    struct Binding {
        ID3D12DescriptorHeap *heap{};
        ID3D12Resource *ui{}, *output{}, *scene{};
    };
    ID3D12Device *device{};
    ID3D12RootSignature *root{};
    ID3D12PipelineState *pipeline{};
    Binding bindings[32]{};
    HRESULT status{E_PENDING};
    const char *stage{"unused"};
    bool initialize(ID3D12Device *d) {
        if (device) return device==d && SUCCEEDED(status);
        device=d; device->AddRef();
        static const char shader[]=R"(
Texture2D<float4> nativeUI : register(t0);
RWTexture2D<float4> generated : register(u0);
cbuffer Size : register(b0) { uint width; uint height; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
    if(p.x>=width || p.y>=height) return;
    float4 ui=nativeUI.Load(int3(p.xy,0));
    float4 scene=generated[p.xy];
    generated[p.xy]=float4(ui.rgb+(1.0-ui.a)*scene.rgb,1.0);
})";
        ID3DBlob *code=nullptr,*errors=nullptr,*serialized=nullptr;
        stage="compile";status=D3DCompile(shader,sizeof(shader)-1,"fh6-native-ui",nullptr,nullptr,"main","cs_5_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors) {fprintf(stderr,"%s\n",(const char*)errors->GetBufferPointer());errors->Release();}
        if(FAILED(status)) return false;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
        ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1};
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable={2,ranges};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,2};
        D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=2;rs.pParameters=parameters;
        stage="serialize-root";status=D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,nullptr);
        if(SUCCEEDED(status)) {stage="create-root";status=device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_ID3D12RootSignature,(void**)&root);}
        if(serialized) serialized->Release();
        if(SUCCEEDED(status)) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root;
            ps.CS={code->GetBufferPointer(),code->GetBufferSize()};
            stage="create-pipeline";status=device->CreateComputePipelineState(&ps,IID_ID3D12PipelineState,(void**)&pipeline);
        }
        code->Release();return SUCCEEDED(status);
    }
    static bool full(const ChainResource &r,UINT w,UINT h) {
        return r.native && r.lifecycle==1 && r.format==DXGI_FORMAT_R8G8B8A8_UNORM && r.width==w && r.height==h &&
               (r.state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) &&
               r.left==0 && r.top==0 &&
               ((r.extentWidth==0 && r.extentHeight==0) || (r.extentWidth==w && r.extentHeight==h));
    }
    Binding *prepare(const ChainFrame &f,const FfxApiResource &hudless,const ffxDispatchDescFrameGeneration &p) {
        if(!pipeline || f.frame!=p.frameID || f.viewport!=0 || p.header.pNext || p.numGeneratedFrames!=1 ||
           !p.commandList || !p.outputs[0].resource || p.outputs[0].state!=FFX_API_RESOURCE_STATE_UNORDERED_ACCESS ||
           p.presentColor.description.format!=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM ||
           p.outputs[0].description.format!=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM) return nullptr;
        const auto w=p.presentColor.description.width,h=p.presentColor.description.height;
        const auto &scene=f.tags[2], &ui=f.tags[3];
        if(!full(scene,w,h) || !full(ui,w,h) || (void*)scene.native!=hudless.resource ||
           hudless.description.format!=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM ||
           hudless.state!=FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ ||
           p.outputs[0].description.width!=w || p.outputs[0].description.height!=h ||
           (!(p.generationRect.width==0 && p.generationRect.height==0) &&
            !(p.generationRect.left==0 && p.generationRect.top==0 && p.generationRect.width==(int)w && p.generationRect.height==(int)h))) return nullptr;
        auto u=(ID3D12Resource*)ui.native, o=(ID3D12Resource*)p.outputs[0].resource, s=(ID3D12Resource*)scene.native;
        if(u==o || s==o || u==s) return nullptr;
        const auto od=o->GetDesc(),ud=u->GetDesc(),sd=s->GetDesc();
        if(od.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || ud.Dimension!=od.Dimension || sd.Dimension!=od.Dimension ||
           od.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || ud.Format!=od.Format || sd.Format!=od.Format ||
           od.Width!=w || od.Height!=h || ud.Width!=w || ud.Height!=h || sd.Width!=w || sd.Height!=h ||
           od.DepthOrArraySize!=1 || ud.DepthOrArraySize!=1 || sd.DepthOrArraySize!=1 ||
           od.SampleDesc.Count!=1 || ud.SampleDesc.Count!=1 || sd.SampleDesc.Count!=1 ||
           !(od.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) || (ud.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) return nullptr;
        auto list=(ID3D12GraphicsCommandList*)p.commandList;
        if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT && list->GetType()!=D3D12_COMMAND_LIST_TYPE_COMPUTE) return nullptr;
        ID3D12Device *ld=nullptr; if(FAILED(list->GetDevice(IID_ID3D12Device,(void**)&ld)))return nullptr;
        const bool same=ld==device;ld->Release();if(!same)return nullptr;
        Binding *free=nullptr;
        for(auto &b:bindings) {
            if(b.heap && b.ui==u && b.output==o && b.scene==s) return &b;
            if(!b.heap && !free) free=&b;
        }
        if(!free) return nullptr; // bounded immutable cache; no in-flight overwrite
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)&free->heap)))return nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE handle=free->heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=ud.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(u,&srv,handle);
        handle.ptr+=device->GetDescriptorHandleIncrementSize(hd.Type);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=od.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(o,nullptr,&uav,handle);
        free->ui=u;u->AddRef();free->output=o;o->AddRef();free->scene=s;s->AddRef();return free;
    }
    void record(ID3D12GraphicsCommandList *list,Binding *b) {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=b->output;
        list->ResourceBarrier(1,&barrier);
        list->SetDescriptorHeaps(1,&b->heap);list->SetComputeRootSignature(root);list->SetPipelineState(pipeline);
        list->SetComputeRootDescriptorTable(0,b->heap->GetGPUDescriptorHandleForHeapStart());
        const auto d=b->output->GetDesc();const UINT dimensions[]={UINT(d.Width),d.Height};
        list->SetComputeRoot32BitConstants(1,2,dimensions,0);list->Dispatch((UINT(d.Width)+7)/8,(d.Height+7)/8,1);
        list->ResourceBarrier(1,&barrier);
    }
    // Call only after the owning FFX context and its queue have drained.
    void release() {
        for(auto &b:bindings) {if(b.heap)b.heap->Release();if(b.ui)b.ui->Release();if(b.output)b.output->Release();if(b.scene)b.scene->Release();b={};}
        if(pipeline)pipeline->Release();if(root)root->Release();if(device)device->Release();pipeline=nullptr;root=nullptr;device=nullptr;
    }
};
