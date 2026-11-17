#pragma once
// Command-list backend for the installed, ABI-checked Apple NGX provider.
// Records MetalFX work through D3DMetal, without closing/submitting the caller's
// command list or waiting/readback/uploading on the CPU. Not a game hook yet.
#include <windows.h>
#include <d3d12.h>
#include <cmath>
#include <cfloat>
#include "../../rosetta/fg-chain/chain.h"
struct AppleNgxParameters {
    void *raw{};
    void **table() const {return *(void***)raw;}
    void set(const char *k,void *v){((void(*)(void*,const char*,void*))table()[0])(raw,k,v);}
    void set(const char *k,ID3D12Resource *v){((void(*)(void*,const char*,ID3D12Resource*))table()[1])(raw,k,v);}
    void set(const char *k,int v){((void(*)(void*,const char*,int))table()[3])(raw,k,v);}
    void set(const char *k,unsigned v){((void(*)(void*,const char*,unsigned))table()[4])(raw,k,v);}
    void set(const char *k,float v){((void(*)(void*,const char*,float))table()[6])(raw,k,v);}
    bool validate(){
        if(!raw)return false;HMODULE owner{};auto vt=table();
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(const char*)vt[0],&owner);
        // Match every dispatch slot before invoking typed setters. These are
        // the pinned local D3DMetal 4.0b3 ABI, not generic NVSDK C++ slot indices.
        const size_t pe[]={0x1fd0,0x29f0,0x2a30,0x2a70,0x2ab0,0x2af0,0x2b30,0x2b70,0x28f0,0x2bb0,0x2bf0,0x2c30,0x2c70,0x2cb0,0x2cf0,0x2d30};
        const size_t native[]={0xb7394,0xb74ca,0xb7600,0xb7736,0xb786a,0xb799e,0xb7ad8,0xb7c14,0xb7d4a,0xb7eae,0xb8012,0xb8176,0xb82da,0xb843e,0xb85a4,0xb870a};
        for(unsigned n=0;n<16;++n)
            if(owner?uintptr_t(vt[n])-uintptr_t(owner)!=pe[n]:uintptr_t(vt[n])-uintptr_t(vt[0])!=native[n]-native[0])return false;
        set("FH6.ABI.U",192u);set("FH6.ABI.I",-3);set("FH6.ABI.F",.375f);
        unsigned u{};int i{};float f{};
        return ((unsigned(*)(void*,const char*,unsigned*))vt[12])(raw,"FH6.ABI.U",&u)==1&&
            ((unsigned(*)(void*,const char*,int*))vt[11])(raw,"FH6.ABI.I",&i)==1&&
            ((unsigned(*)(void*,const char*,float*))vt[14])(raw,"FH6.ABI.F",&f)==1&&u==192&&i==-3&&f==.375f;
    }
};
struct AppleCommandFg {
    using Init=unsigned(*)(unsigned long long,const wchar_t*,ID3D12Device*,unsigned,const void*);
    using Allocate=unsigned(*)(void**);
    using Create=unsigned(*)(ID3D12GraphicsCommandList*,unsigned,void*,void**);
    using Evaluate=unsigned(*)(ID3D12GraphicsCommandList*,const void*,const void*,void*);
    using Release=unsigned(*)(void*);
    HMODULE module{};AppleNgxParameters params;Create create{};Evaluate evaluate{};
    Release releaseFeature{},releaseParameters{};void *feature{};
    ID3D12Device *device{};ID3D12Resource *realOutput{};
    UINT w{},h{},rw{},rh{};uint64_t lastFrame{};unsigned samples{},readySamples{3};bool hasFrame{},failed{};
    unsigned status{};const char *reason="unused";
    // Calls must be serialized by the owner. Resources live until GPU completion.
    bool initialize(ID3D12Device *d,UINT width,UINT height,UINT renderWidth,UINT renderHeight){
        if(device)return device==d&&w==width&&h==height&&rw==renderWidth&&rh==renderHeight&&!failed;
        // A failed partial initialization must be explicitly disposed before retry.
        if(module||params.raw)return false;
        if(!d||!width||!height||!renderWidth||!renderHeight||renderWidth>width||renderHeight>height||width>8192||height>8192)return false;
        module=LoadLibraryW(L"nvngx.dll");if(!module){reason="native NGX unavailable";return false;}
        auto init=(Init)GetProcAddress(module,"NVSDK_NGX_D3D12_Init_Ext");
        auto allocate=(Allocate)GetProcAddress(module,"NVSDK_NGX_D3D12_AllocateParameters");
        create=(Create)GetProcAddress(module,"NVSDK_NGX_D3D12_CreateFeature");
        evaluate=(Evaluate)GetProcAddress(module,"NVSDK_NGX_D3D12_EvaluateFeature");
        releaseFeature=(Release)GetProcAddress(module,"NVSDK_NGX_D3D12_ReleaseFeature");
        releaseParameters=(Release)GetProcAddress(module,"NVSDK_NGX_D3D12_DestroyParameters");
        if(!init||!allocate||!create||!evaluate||!releaseFeature||!releaseParameters){reason="missing exports";return false;}
        status=init(0,L".",d,0x15,nullptr);if(status!=1){reason="NGX initialization";return false;}
        status=allocate(&params.raw);if(status!=1||!params.validate()){reason="parameter ABI";return false;}
        device=d;d->AddRef();w=width;h=height;rw=renderWidth;rh=renderHeight;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;
        desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        status=d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,(void**)&realOutput);
        if(FAILED(HRESULT(status))){failed=true;reason="real-output allocation";return false;}
        params.set("Width",w);params.set("Height",h);params.set("OutWidth",w);params.set("OutHeight",h);
        params.set("DLSSG.BackbufferWidth",w);params.set("DLSSG.BackbufferHeight",h);params.set("DLSSG.DynamicResolution",0u);
        params.set("DLSSG.MVecsSubrectBaseX",0u);params.set("DLSSG.MVecsSubrectBaseY",0u);
        params.set("DLSSG.DepthSubrectBaseX",0u);params.set("DLSSG.DepthSubrectBaseY",0u);
        params.set("DLSSG.MVecsSubrectWidth",rw);params.set("DLSSG.MVecsSubrectHeight",rh);
        params.set("DLSSG.DepthSubrectWidth",rw);params.set("DLSSG.DepthSubrectHeight",rh);
        params.set("DLSSG.run_lowres_mvec_pass",0);reason="ready";return true;
    }
    static bool texture(ID3D12Resource *r,UINT width,UINT height,DXGI_FORMAT format,bool uav=false){
        if(!r)return false;auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&
            d.Width==width&&d.Height==height&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&d.Format==format&&
            (!uav||(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
    }
    static bool constants(const ChainFrame &f){
        if(!f.hasConstants||f.viewport||f.inverted>1||f.cameraMotionIncluded!=1||f.motion3D||f.dilated>1||f.jittered||f.reset>1)return false;
        for(float v:f.jitter)if(!std::isfinite(v)||fabsf(v)==FLT_MAX)return false;
        for(float v:f.scale)if(!std::isfinite(v)||fabsf(v)==FLT_MAX)return false;
        for(float v:f.cameraBasis)if(!std::isfinite(v)||fabsf(v)==FLT_MAX)return false;
        for(float v:f.projection)if(!std::isfinite(v)||fabsf(v)==FLT_MAX)return false;
        return std::isfinite(f.nearPlane)&&std::isfinite(f.farPlane)&&std::isfinite(f.fov)&&std::isfinite(f.aspect)&&
            f.nearPlane>0&&f.farPlane>f.nearPlane&&f.fov>0&&f.fov<3.141593f&&f.aspect>0;
    }
    // Caller supplies fenced/same-queue frame-matched Prepare snapshots and
    // full RGBA8 HUDless scene. Input states: shader-read. Output state: UAV.
    bool record(ID3D12GraphicsCommandList *list,const ChainFrame &f,ID3D12Resource *color,
                ID3D12Resource *depth,ID3D12Resource *motion,ID3D12Resource *out,bool &generated){
        generated=false;
        if(!device||failed||!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||!constants(f)||
           (hasFrame&&f.frame<=lastFrame)||!texture(color,w,h,DXGI_FORMAT_R8G8B8A8_UNORM)||
           (!texture(depth,rw,rh,DXGI_FORMAT_R32_FLOAT)&&!texture(depth,rw,rh,DXGI_FORMAT_R32G8X24_TYPELESS))||
           !texture(motion,rw,rh,DXGI_FORMAT_R16G16_FLOAT)||!texture(out,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true)||
           color==out||depth==out||motion==out){reason="invalid frame/resources";return false;}
        ID3D12Device *owner{};if(FAILED(list->GetDevice(IID_ID3D12Device,(void**)&owner)))return false;
        bool same=owner==device;owner->Release();if(!same)return false;
        ID3D12Resource *resources[]={color,depth,motion,out};
        for(auto r:resources){owner=nullptr;if(FAILED(r->GetDevice(IID_ID3D12Device,(void**)&owner)))return false;
            same=owner==device;owner->Release();if(!same){reason="foreign resource device";return false;}}
        if(!feature){status=create(list,11,params.raw,&feature);if(status!=1||!feature){failed=true;reason="native feature creation";return false;}}
        const bool reset=!hasFrame||f.reset||f.frame!=lastFrame+1;
        params.set("DLSSG.Backbuffer",color);params.set("DLSSG.Depth",depth);params.set("DLSSG.MVecs",motion);
        params.set("DLSSG.OutputInterpolated",out);params.set("DLSSG.OutputReal",realOutput);
        params.set("DLSSG.CameraNear",f.nearPlane);params.set("DLSSG.CameraFar",f.farPlane);
        params.set("DLSSG.CameraFOV",f.fov);params.set("DLSSG.CameraAspectRatio",f.aspect);
        params.set("DLSSG.MvecScaleX",f.scale[0]*rw);params.set("DLSSG.MvecScaleY",f.scale[1]*rh);
        params.set("DLSSG.DepthInverted",int(f.inverted));params.set("DLSSG.Reset",reset?1:0);
        params.set("DLSSG.JitterOffsetX",f.jitter[0]);params.set("DLSSG.JitterOffsetY",f.jitter[1]);
        params.set("DLSSG.CameraViewToClip",(void*)f.projection);
        const char *keys[]={"DLSSG.CameraPosX","DLSSG.CameraPosY","DLSSG.CameraPosZ","DLSSG.CameraUpX","DLSSG.CameraUpY","DLSSG.CameraUpZ","DLSSG.CameraRightX","DLSSG.CameraRightY","DLSSG.CameraRightZ","DLSSG.CameraFwdX","DLSSG.CameraFwdY","DLSSG.CameraFwdZ"};
        for(unsigned n=0;n<12;++n)params.set(keys[n],f.cameraBasis[n]);
        D3D12_RESOURCE_BARRIER barriers[2]{};for(auto &b:barriers)b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[0].UAV.pResource=out;barriers[1].UAV.pResource=realOutput;
        list->ResourceBarrier(2,barriers);status=evaluate(list,feature,params.raw,nullptr);
        if(status!=1){failed=true;reason="native evaluation";return false;}
        list->ResourceBarrier(2,barriers);
        // Measured native behavior differs for creation and a live reset:
        // creation copies two samples; resetting an existing feature copies
        // three. Do not label the third post-reset copy as interpolation.
        if(reset)readySamples=hasFrame?4:3;
        lastFrame=f.frame;hasFrame=true;samples=reset?1:(samples<4?samples+1:4);generated=samples>=readySamples;
        reason=generated?"native midpoint recorded":"native startup copy recorded";return true;
    }
    // Never call this while any command list referencing the backend is pending.
    bool releaseAfterGpuCompletion(){
        if(feature&&releaseFeature(feature)!=1)return false;feature=nullptr;
        if(params.raw&&releaseParameters(params.raw)!=1)return false;params.raw=nullptr;
        if(realOutput)realOutput->Release();if(device)device->Release();realOutput=nullptr;device=nullptr;
        if(module)FreeLibrary(module);module=nullptr;return true;
    }
};
