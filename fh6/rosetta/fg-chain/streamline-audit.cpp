// FH6-only public-interface observer. No resource tags or constants are changed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <sl.h>
#include <sl_consts.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "chain.h"
#include "first-fault.h"

static HMODULE self;
static HANDLE logfile = INVALID_HANDLE_VALUE, exitfile = INVALID_HANDLE_VALUE;
static volatile LONG exitReports;
static void (__cdecl *originalExit)(int);
static void (__cdecl *originalQuickExit)(int);
static void (WINAPI *originalWinExit)(UINT);
static SRWLOCK frameLock = SRWLOCK_INIT, logLock = SRWLOCK_INIT;
static ChainFrame frames[128]{};
static volatile LONG64 sequence;
static volatile LONG started, reports;
static DWORD evaluateTls = TLS_OUT_OF_INDEXES;
static FARPROC (WINAPI *originalGetProcAddress)(HMODULE,LPCSTR) = GetProcAddress;
static PFun_slSetConstants *setConstants;
static PFun_slSetTag *setTag;
static PFun_slSetTagForFrame *setTagForFrame;
static PFun_slEvaluateFeature *evaluateFeature;

extern "C" __declspec(dllexport) void __cdecl FH6ChainReport(const char *line)
{
    if (!line || logfile == INVALID_HANDLE_VALUE || InterlockedIncrement(&reports) > 10000) return;
    AcquireSRWLockExclusive(&logLock);
    DWORD done;
    if(line[0]=='{') {
        char prefix[130];LARGE_INTEGER qpc;QueryPerformanceCounter(&qpc);
        const int length=snprintf(prefix,sizeof(prefix),"{\"traceSequence\":%llu,\"qpc\":%llu,",
            (unsigned long long)InterlockedIncrement64(&sequence),(unsigned long long)qpc.QuadPart);
        WriteFile(logfile,prefix,(DWORD)length,&done,nullptr);
        WriteFile(logfile,line+1,(DWORD)strlen(line+1),&done,nullptr);
    } else WriteFile(logfile,line,(DWORD)strlen(line),&done,nullptr);
    WriteFile(logfile,"\n",1,&done,nullptr);
    ReleaseSRWLockExclusive(&logLock);
}
static ChainFrame &slot(uint64_t id, uint32_t viewport)
{
    auto &f=frames[id%128];
    if (f.frame!=id || f.viewport!=viewport) { f={}; f.frame=id; f.viewport=viewport; }
    return f;
}
extern "C" __declspec(dllexport) int __cdecl FH6ChainGet(uint64_t id,ChainFrame *out)
{
    if (!out) return 0;
    AcquireSRWLockShared(&frameLock);
    const auto &f=frames[id%128];
    const bool found=f.frame==id && (f.hasConstants || f.tagsMask);
    if(found) *out=f;
    ReleaseSRWLockShared(&frameLock);
    return found;
}
extern "C" __declspec(dllexport) uint64_t __cdecl FH6ChainEvaluateFrame()
{
    const auto value=evaluateTls==TLS_OUT_OF_INDEXES?0:(uintptr_t)TlsGetValue(evaluateTls);
    return value?value-1:UINT64_MAX;
}

static void recordTags(uint64_t id,uint32_t viewport,const sl::ResourceTag *tags,uint32_t count)
{
    if(!tags || count>128 || id==UINT64_MAX) return;
    for(uint32_t i=0;i<count;i++) {
        const auto &t=tags[i]; int k=-1;
        if(t.type==sl::kBufferTypeDepth) k=0;
        else if(t.type==sl::kBufferTypeMotionVectors) k=1;
        else if(t.type==sl::kBufferTypeHUDLessColor) k=2;
        else if(t.type==sl::kBufferTypeUIColorAndAlpha) k=3;
        else if(t.type==sl::kBufferTypeScalingInputColor) k=4;
        else if(t.type==sl::kBufferTypeScalingOutputColor) k=5;
        if(k<0) continue;
        ChainResource r{};
        r.type=t.type; r.lifecycle=(uint32_t)t.lifecycle;
        r.sequence=(uint64_t)InterlockedIncrement64(&sequence);
        r.left=t.extent.left; r.top=t.extent.top;
        r.extentWidth=t.extent.width; r.extentHeight=t.extent.height;
        if(t.resource && t.resource->native) {
            r.native=(uint64_t)t.resource->native; r.state=t.resource->state;
            auto d=((ID3D12Resource*)t.resource->native)->GetDesc();
            r.width=(uint32_t)d.Width; r.height=d.Height; r.format=(uint32_t)d.Format;
        }
        AcquireSRWLockExclusive(&frameLock);
        auto &f=slot(id,viewport); f.tags[k]=r;
        if(r.native) f.tagsMask|=1u<<k; else f.tagsMask&=~(1u<<k);
        ReleaseSRWLockExclusive(&frameLock);
    }
}
static sl::Result traceConstants(const sl::Constants &v,const sl::FrameToken &token,const sl::ViewportHandle &view)
{
    // Only inspect the common v1 prefix. Forward the original object unchanged.
    if(v.structVersion<=sl::kStructVersion2) {
        AcquireSRWLockExclusive(&frameLock);
        auto &f=slot((uint32_t)token,(uint32_t)view);
        f.hasConstants=1; f.constantsSequence=(uint64_t)InterlockedIncrement64(&sequence);
        f.jitter[0]=v.jitterOffset.x; f.jitter[1]=v.jitterOffset.y;
        f.scale[0]=v.mvecScale.x; f.scale[1]=v.mvecScale.y;
        f.nearPlane=v.cameraNear; f.farPlane=v.cameraFar; f.fov=v.cameraFOV; f.aspect=v.cameraAspectRatio;
        f.inverted=(uint32_t)v.depthInverted; f.cameraMotionIncluded=(uint32_t)v.cameraMotionIncluded;
        f.motion3D=(uint32_t)v.motionVectors3D; f.dilated=(uint32_t)v.motionVectorsDilated;
        f.jittered=(uint32_t)v.motionVectorsJittered; f.reset=(uint32_t)v.reset;
        memcpy(f.clipToPrevious,&v.clipToPrevClip,sizeof(f.clipToPrevious));
        memcpy(f.projection,&v.cameraViewToClip,sizeof(f.projection));
        memcpy(f.cameraBasis,&v.cameraPos,3*sizeof(float));memcpy(f.cameraBasis+3,&v.cameraUp,3*sizeof(float));
        memcpy(f.cameraBasis+6,&v.cameraRight,3*sizeof(float));memcpy(f.cameraBasis+9,&v.cameraFwd,3*sizeof(float));
        ReleaseSRWLockExclusive(&frameLock);
    }
    return setConstants(v,token,view);
}
static sl::Result traceTagsForFrame(const sl::FrameToken &f,const sl::ViewportHandle &v,
        const sl::ResourceTag *t,uint32_t n,sl::CommandBuffer *cmd)
{
    recordTags((uint32_t)f,(uint32_t)v,t,n);
    return setTagForFrame(f,v,t,n,cmd);
}
static sl::Result traceTags(const sl::ViewportHandle &v,const sl::ResourceTag *t,uint32_t n,sl::CommandBuffer *cmd)
{
    // Legacy tags carry no frame token. Never guess their frame from a wall clock.
    static LONG once;
    if(!InterlockedExchange(&once,1)) FH6ChainReport("{\"event\":\"legacy_tags_without_frame\"}");
    return setTag(v,t,n,cmd);
}
static sl::Result traceEvaluate(sl::Feature feature,const sl::FrameToken &f,
        const sl::BaseStructure **inputs,uint32_t count,sl::CommandBuffer *cmd)
{
    const auto previous=TlsGetValue(evaluateTls);
    TlsSetValue(evaluateTls,(void*)((uintptr_t)(uint32_t)f+1));
    AcquireSRWLockExclusive(&frameLock);
    auto &s=frames[(uint32_t)f%128];
    if(s.frame!=(uint32_t)f) { s={}; s.frame=(uint32_t)f; }
    s.evaluateSequence=(uint64_t)InterlockedIncrement64(&sequence);
    ReleaseSRWLockExclusive(&frameLock);
    if(inputs && count<128) {
        uint32_t viewport=0;
        for(uint32_t i=0;i<count;i++)
            if(inputs[i] && inputs[i]->structType==sl::ViewportHandle::s_structType)
                viewport=(uint32_t)*(const sl::ViewportHandle*)inputs[i];
        for(uint32_t i=0;i<count;i++)
            if(inputs[i] && inputs[i]->structType==sl::ResourceTag::s_structType)
                recordTags((uint32_t)f,viewport,(const sl::ResourceTag*)inputs[i],1);
    }
    const auto result=evaluateFeature(feature,f,inputs,count,cmd);
    TlsSetValue(evaluateTls,previous);
    return result;
}
static FARPROC WINAPI traceGetProcAddress(HMODULE module,LPCSTR name)
{
    auto result=originalGetProcAddress(module,name);
    if(!result || (uintptr_t)name<=65535) return result;
    // Only public Streamline exports requested by the main game, not by OptiScaler.
    if(strncmp(name,"sl",2)) return result;
    wchar_t path[32768]; DWORD n=GetModuleFileNameW(module,path,32768);
    if(!n || n>=32768) return result;
    const wchar_t *base=wcsrchr(path,L'\\'); base=base?base+1:path;
    if(_wcsicmp(base,L"sl.interposer.dll")) return result;
#define CAPTURE(exportName,field,hook) \
    if(!strcmp(name,exportName)) { \
        if(result==(FARPROC)hook) return result; \
        field=reinterpret_cast<decltype(field)>(result); \
        FH6ChainReport("{\"event\":\"capture_export\",\"name\":\"" exportName "\"}"); \
        return (FARPROC)hook; }
    CAPTURE("slSetConstants",setConstants,traceConstants)
    CAPTURE("slSetTag",setTag,traceTags)
    CAPTURE("slSetTagForFrame",setTagForFrame,traceTagsForFrame)
    CAPTURE("slEvaluateFeature",evaluateFeature,traceEvaluate)
#undef CAPTURE
    return result;
}
// Observe public main-executable exit imports before CRT/module destructors.
// A separate handle and fixed one-write record avoid frame/log locks and the
// graphics trace budget. No stack unwinding, exit handling or status rewriting.
static void exitRequest(const char *api,int code,void *caller){
 if(exitfile==INVALID_HANDLE_VALUE||InterlockedIncrement(&exitReports)>8)return;
 char line[256];int n=snprintf(line,sizeof(line),"{\"event\":\"process_exit_request\",\"api\":\"%s\",\"code\":%d,\"pid\":%lu,\"tid\":%lu,\"tick\":%llu,\"caller\":\"%llx\"}\n",api,code,GetCurrentProcessId(),GetCurrentThreadId(),(unsigned long long)GetTickCount64(),(unsigned long long)caller);DWORD done;if(n>0&&n<(int)sizeof(line))WriteFile(exitfile,line,n,&done,nullptr);
}
__attribute__((noinline,noreturn)) static void __cdecl traceExit(int code){exitRequest("exit",code,__builtin_return_address(0));originalExit(code);__builtin_unreachable();}
__attribute__((noinline,noreturn)) static void __cdecl traceQuickExit(int code){exitRequest("_exit",code,__builtin_return_address(0));originalQuickExit(code);__builtin_unreachable();}
__attribute__((noinline,noreturn)) static void WINAPI traceWinExit(UINT code){exitRequest("ExitProcess",(int)code,__builtin_return_address(0));originalWinExit(code);__builtin_unreachable();}
static bool installImport(HMODULE module)
{
    auto base=(BYTE*)module; auto dos=(IMAGE_DOS_HEADER*)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return false;
    auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    const auto size=nt->OptionalHeader.SizeOfImage;
    const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress || dir.VirtualAddress>=size || dir.Size>size-dir.VirtualAddress) return false;
    auto imports=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);
    bool installed=false;
    for(size_t i=0;i<dir.Size/sizeof(*imports) && imports[i].Name;i++) {
        if(!imports[i].OriginalFirstThunk || !imports[i].FirstThunk) continue;
        auto names=(IMAGE_THUNK_DATA64*)(base+imports[i].OriginalFirstThunk);
        auto iat=(IMAGE_THUNK_DATA64*)(base+imports[i].FirstThunk);
        for(size_t j=0;names[j].u1.AddressOfData;j++) {
            if(IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal)) continue;
            auto name=(IMAGE_IMPORT_BY_NAME*)(base+names[j].u1.AddressOfData);
            const char *symbolName=(char*)name->Name;
            void *replacement=nullptr;
            if(!strcmp(symbolName,"GetProcAddress"))replacement=(void*)traceGetProcAddress;
            else if(exitfile!=INVALID_HANDLE_VALUE){
                const char *library=(char*)(base+imports[i].Name);
                bool crt=!_stricmp(library,"ucrtbase.dll")||!_stricmp(library,"msvcrt.dll")||!_stricmp(library,"api-ms-win-crt-runtime-l1-1-0.dll");
                if(crt&&!strcmp(symbolName,"exit"))replacement=(void*)traceExit;
                else if(crt&&!strcmp(symbolName,"_exit"))replacement=(void*)traceQuickExit;
                else if((!_stricmp(library,"kernel32.dll")||!_stricmp(library,"kernelbase.dll"))&&!strcmp(symbolName,"ExitProcess"))replacement=(void*)traceWinExit;
            }
            if(!replacement)continue;
            DWORD old,ignored;
            auto target=(void*volatile*)&iat[j].u1.Function;
            if(!VirtualProtect((void*)target,sizeof(*target),PAGE_READWRITE,&old)) continue;
            // Retain an existing public-import hook; do not bypass another tool.
            if(*target!=replacement){
                if(replacement==(void*)traceGetProcAddress)originalGetProcAddress=(decltype(originalGetProcAddress))*target;
                else if(replacement==(void*)traceExit)originalExit=(decltype(originalExit))*target;
                else if(replacement==(void*)traceQuickExit)originalQuickExit=(decltype(originalQuickExit))*target;
                else if(replacement==(void*)traceWinExit)originalWinExit=(decltype(originalWinExit))*target;
            }
            InterlockedExchangePointer(target,replacement);
            VirtualProtect((void*)target,sizeof(*target),old,&ignored);
            if(replacement==(void*)traceGetProcAddress)installed=true;
        }
    }
    return installed;
}
extern "C" __declspec(dllexport) void InitializeASI()
{
    if(InterlockedExchange(&started,1)) return;
    wchar_t path[32768]; DWORD n=GetModuleFileNameW(self,path,32768);
    if(!n || n>=32768) return;
    auto end=wcsrchr(path,L'\\'); if(!end) return;
    HMODULE pinned;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)&self,&pinned))return;
    wcscpy(end+1,L"fh6-process-exit.jsonl");
    exitfile=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    wcscpy(end+1,L"fh6-first-fault.jsonl");
    const bool faultObserver=FirstFault::initialize(path);
    wcscpy(end+1,L"fh6-fg-chain.jsonl");
    logfile=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    evaluateTls=TlsAlloc();
    if(evaluateTls==TLS_OUT_OF_INDEXES) { FH6ChainReport("{\"event\":\"tls_allocation_failed\"}");return; }
    const bool ok=installImport(GetModuleHandleW(nullptr));
    char line[160];
    snprintf(line,sizeof(line),"{\"event\":\"%s\",\"abi\":1,\"pid\":%lu,\"tick\":%llu}",
        ok?"observer_ready":"observer_import_missing",GetCurrentProcessId(),(unsigned long long)GetTickCount64());
    FH6ChainReport(line);
    FH6ChainReport(faultObserver?"{\"event\":\"first_fault_observer_ready\"}":"{\"event\":\"first_fault_observer_failed\"}");
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,void*)
{
    if(reason==DLL_PROCESS_ATTACH) self=module;
    return TRUE;
}
