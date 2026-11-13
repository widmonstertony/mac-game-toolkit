#pragma once
// Bounded, explicitly armed GPU readbacks. All copies stay in the actual FFX
// interpolation command list, before/after UI composition. Never guess that a
// CPU return value means GPU completion.
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <framegeneration/include/ffx_framegeneration.h>
#include "chain.h"

struct CapturePrepare {
    uint64_t frame{};bool known{};float milliseconds{},scale[2]{},jitter[2]{};
    UINT render[2]{};
};
struct FgGpuCapture {
    enum {ImageCount=7,JobCount=2};
    struct Image {
        const char *name{};ID3D12Resource *source{};D3D12_RESOURCE_STATES state{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT width{},height{},format{};UINT64 bytes{},rowBytes{};
    };
    struct Job {
        Image image[ImageCount]{};ID3D12Resource *readback{};ID3D12Fence *fence{};
        ID3D12Device *device{};volatile LONG *active{};
        HMODULE module{},reporterModule{};ChainReport report{};wchar_t folder[32768]{};
        uint64_t frame{};UINT64 total{};ChainFrame native{};CapturePrepare prepare{};
        UINT transfer{};bool recorded{};
    };
    Job *pending[JobCount]{};unsigned remaining{},poll{};HMODULE module{};ChainReport report{};
    volatile LONG activeJobs{};
    wchar_t root[32768]{},request[32768]{};
    void setup(HMODULE m,ChainReport r) {
        if(module)return;module=m;report=r;
        DWORD n=GetModuleFileNameW(m,root,32768);if(!n || n>=32768){root[0]=0;return;}
        auto slash=wcsrchr(root,L'\\');if(!slash){root[0]=0;return;}slash[1]=0;
        if(wcslen(root)>32600){root[0]=0;return;}
        wcscpy(request,root);wcscat(request,L"capture-request");wcscat(root,L"captures");
    }
    static void cleanup(Job *j) {
        for(auto &i:j->image)if(i.source)i.source->Release();
        if(j->readback)j->readback->Release();if(j->fence)j->fence->Release();
        if(j->device)j->device->Release();if(j->reporterModule)FreeLibrary(j->reporterModule);
        if(j->active)InterlockedDecrement(j->active);
        HeapFree(GetProcessHeap(),0,j);
    }
    static D3D12_RESOURCE_STATES apiState(UINT value) {
        UINT state=0;
        if(value&FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)state|=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if(value&FFX_API_RESOURCE_STATE_COMPUTE_READ)state|=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if(value&FFX_API_RESOURCE_STATE_PIXEL_READ)state|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if(value&FFX_API_RESOURCE_STATE_COPY_SRC)state|=D3D12_RESOURCE_STATE_COPY_SOURCE;
        if(value&FFX_API_RESOURCE_STATE_COPY_DEST)state|=D3D12_RESOURCE_STATE_COPY_DEST;
        if(value&FFX_API_RESOURCE_STATE_RENDER_TARGET)state|=D3D12_RESOURCE_STATE_RENDER_TARGET;
        return D3D12_RESOURCE_STATES(state);
    }
    static bool save(const wchar_t *path,const void *data,UINT64 bytes) {
        HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
        auto ptr=(const BYTE*)data;bool ok=true;
        while(bytes){DWORD chunk=bytes>1048576?1048576:(DWORD)bytes,done=0;
            if(!WriteFile(f,ptr,chunk,&done,nullptr)||done!=chunk){ok=false;break;}ptr+=done;bytes-=done;}
        CloseHandle(f);return ok;
    }
    static DWORD WINAPI worker(void *arg) {
        auto j=(Job*)arg;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);bool complete=false;
        if(event && SUCCEEDED(j->fence->SetEventOnCompletion(1,event)))complete=WaitForSingleObject(event,30000)==WAIT_OBJECT_0;
        const auto fenceValue=j->fence->GetCompletedValue();complete=complete && fenceValue!=UINT64_MAX && fenceValue>=1;
        if(!complete){if(j->report)j->report("{\"event\":\"gpu_capture_completion_failed_retained\"}");
            // Retain the bounded job/module reference on a GPU timeout instead
            // of destroying resources which may still be referenced by GPU.
            return 1;
        }
        if(event)CloseHandle(event);
        BYTE *mapped=nullptr;D3D12_RANGE range{0,(SIZE_T)j->total};bool ok=SUCCEEDED(j->readback->Map(0,&range,(void**)&mapped));
        wchar_t path[32768];
        if(ok)for(auto &i:j->image){wcscpy(path,j->folder);wcscat(path,L"\\");
            wchar_t name[64];MultiByteToWideChar(CP_UTF8,0,i.name,-1,name,64);wcscat(path,name);wcscat(path,L".bin");
            if(!save(path,mapped+i.footprint.Offset,i.bytes))ok=false;}
        if(mapped){D3D12_RANGE noWrite{};j->readback->Unmap(0,&noWrite);}
        // A single metadata file, written last, is the completed-capture marker.
        char meta[6000];int n=snprintf(meta,sizeof(meta),"{\"frame\":%llu,\"gpu_fence_completed\":%s,\"pixel_files_complete\":%s,\"transfer\":%u,\"native_frame_size\":%u,\"prepare\":{\"known\":%s,\"frame\":%llu,\"milliseconds\":%.9g,\"render\":[%u,%u],\"scale\":[%.9g,%.9g],\"jitter\":[%.9g,%.9g]},\"images\":[",
            (unsigned long long)j->frame,complete?"true":"false",ok?"true":"false",j->transfer,(unsigned)sizeof(j->native),
            j->prepare.known?"true":"false",(unsigned long long)j->prepare.frame,j->prepare.milliseconds,j->prepare.render[0],j->prepare.render[1],j->prepare.scale[0],j->prepare.scale[1],j->prepare.jitter[0],j->prepare.jitter[1]);
        for(unsigned k=0;k<ImageCount && n>0 && n<(int)sizeof(meta);k++){const auto &i=j->image[k];n+=snprintf(meta+n,sizeof(meta)-n,"%s{\"name\":\"%s\",\"ptr\":\"%llx\",\"source_format\":%u,\"copy_format\":%u,\"width\":%u,\"height\":%u,\"row_pitch\":%u,\"row_bytes\":%llu,\"bytes\":%llu,\"state_restored\":%u}",
            k?",":"",i.name,(unsigned long long)i.source,i.format,(unsigned)i.footprint.Footprint.Format,i.width,i.height,i.footprint.Footprint.RowPitch,(unsigned long long)i.rowBytes,(unsigned long long)i.bytes,(unsigned)i.state);}
        if(n>0 && n+3<(int)sizeof(meta)){memcpy(meta+n,"]}\n",3);n+=3;
            wcscpy(path,j->folder);wcscat(path,L"\\native-frame.bin");ok=save(path,&j->native,sizeof(j->native))&&ok;
            wcscpy(path,j->folder);wcscat(path,L"\\capture.json");ok=save(path,meta,n)&&ok;}
        else ok=false;
        if(j->report){char line[200];snprintf(line,sizeof(line),"{\"event\":\"gpu_capture_saved\",\"frame\":%llu,\"success\":%s,\"bytes\":%llu}",(unsigned long long)j->frame,ok?"true":"false",(unsigned long long)j->total);j->report(line);}
        HMODULE keep=j->module;cleanup(j);FreeLibraryAndExitThread(keep,ok?0:1);return 0;
    }
    static bool schedule(Job *j,ID3D12CommandQueue *queue) {
        // Called only when SDK is about to REUSE this exact output. The pinned
        // SDK has already queued its output-availability composition-fence wait
        // before calling the FFX dispatch callback. With async=false this signal
        // therefore follows the captured list and its previous presentation.
        if(FAILED(queue->Signal(j->fence,1)))return false;
        HMODULE keep=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)&worker,&keep))return false;
        j->module=keep;
        if(j->report && !j->reporterModule)
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)j->report,&j->reporterModule);
        HANDLE thread=CreateThread(nullptr,0,worker,j,0,nullptr);
        if(!thread){FreeLibrary(keep);return false;}CloseHandle(thread);return true;
    }
    void retire(ID3D12Resource *output,uint64_t currentFrame,ID3D12CommandQueue *queue) {
        for(auto &j:pending)if(j && j->recorded && j->frame<currentFrame && j->image[5].source==output)
            if(schedule(j,queue))j=nullptr;
    }
    void drained(ID3D12CommandQueue *queue) {
        // Context teardown already drained the original SDK's queue here.
        for(auto &j:pending)if(j && j->recorded)if(schedule(j,queue))j=nullptr;
    }
    Job *begin(ID3D12Device *device,const ChainFrame &f,const CapturePrepare &prep,const ffxDispatchDescFrameGeneration &p) {
        if(!module || !root[0] || p.reset || InterlockedCompareExchange(&activeJobs,0,0)>=JobCount)return nullptr;
        if(!remaining && (++poll%120==1) && !pending[0] && !pending[1] && GetFileAttributesW(request)!=INVALID_FILE_ATTRIBUTES) {
            if(DeleteFileW(request)){remaining=JobCount;CreateDirectoryW(root,nullptr);if(report)report("{\"event\":\"gpu_capture_armed\",\"frames\":2}");}
        }
        if(!remaining)return nullptr;
        unsigned slot=JobCount;for(unsigned i=0;i<JobCount;i++)if(!pending[i]){slot=i;break;}if(slot==JobCount)return nullptr;
        auto j=(Job*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Job));if(!j)return nullptr;
        j->active=&activeJobs;InterlockedIncrement(&activeJobs);j->device=device;device->AddRef();
        j->frame=p.frameID;j->native=f;j->prepare=prep;j->prepare.known=prep.known && prep.frame==p.frameID;j->transfer=p.backbufferTransferFunction;j->report=report;
        const char *names[]={"depth","motion","scene","ui","present","generated-before-ui","generated-after-ui"};
        const uint64_t ptrs[]={f.tags[0].native,f.tags[1].native,f.tags[2].native,f.tags[3].native,(uint64_t)p.presentColor.resource,(uint64_t)p.outputs[0].resource,(uint64_t)p.outputs[0].resource};
        const D3D12_RESOURCE_STATES states[]={D3D12_RESOURCE_STATES(f.tags[0].state),D3D12_RESOURCE_STATES(f.tags[1].state),D3D12_RESOURCE_STATES(f.tags[2].state),D3D12_RESOURCE_STATES(f.tags[3].state),apiState(p.presentColor.state),apiState(p.outputs[0].state),apiState(p.outputs[0].state)};
        bool valid=true;
        for(unsigned k=0;k<ImageCount;k++) {
            auto &i=j->image[k];i.name=names[k];i.source=(ID3D12Resource*)ptrs[k];i.state=states[k];
            if(!i.source){valid=false;break;}i.source->AddRef();const auto d=i.source->GetDesc();
            if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.SampleDesc.Count!=1 || d.DepthOrArraySize!=1){valid=false;break;}
            i.width=(UINT)d.Width;i.height=d.Height;i.format=(UINT)d.Format;
            UINT rows;UINT64 total;device->GetCopyableFootprints(&d,0,1,0,&i.footprint,&rows,&i.rowBytes,&total);
            if(!i.footprint.Footprint.RowPitch || i.footprint.Footprint.Depth!=1 || i.footprint.Footprint.Width!=i.width || i.footprint.Footprint.Height!=i.height){valid=false;break;}
            j->total=(j->total+511)&~UINT64(511);i.footprint.Offset=j->total;
            i.bytes=UINT64(i.footprint.Footprint.RowPitch)*rows;j->total+=i.bytes;
            if(j->total>256ull*1024*1024){valid=false;break;}
        }
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=j->total;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(valid)valid=SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&j->readback));
        if(valid)valid=SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&j->fence));
        if(!valid){cleanup(j);remaining=0;if(report)report("{\"event\":\"gpu_capture_allocation_declined\"}");return nullptr;}
        swprintf(j->folder,32768,L"%ls\\pid-%lu-frame-%llu-%llu",root,GetCurrentProcessId(),(unsigned long long)j->frame,(unsigned long long)GetTickCount64());
        if(!CreateDirectoryW(j->folder,nullptr)){cleanup(j);remaining=0;return nullptr;}
        pending[slot]=j;remaining--;return j;
    }
    static void copies(ID3D12GraphicsCommandList *list,Job *j,unsigned first,unsigned end) {
        for(unsigned k=first;k<end;k++) {
            auto &i=j->image[k];D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={i.source,0,i.state,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=i.source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=0;to.pResource=j->readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=i.footprint;
            list->CopyTextureRegion(&to,0,0,0,&from,nullptr);b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=i.state;list->ResourceBarrier(1,&b);
        }
        if(end==ImageCount)j->recorded=true;
    }
};
