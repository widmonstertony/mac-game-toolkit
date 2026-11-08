#include <vulkan/vulkan_core.h>
#include <vkd3d_device_vkd3d_ext.h>
#include <winternl.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "fsr_job.hpp"

using DispatchUnix = NTSTATUS (WINAPI *)(UINT64, unsigned int, void *);
using RecordPass = HRESULT (WINAPI *)(ID3D12CommandList *, void (WINAPI *)(IUnknown *),
                                       HRESULT (WINAPI *)(IUnknown *), IUnknown *);
static HMODULE shim;

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) shim = module;
    return TRUE;
}

static std::wstring moduleDirectory()
{
    wchar_t path[32768];
    const auto len = GetModuleFileNameW(shim, path, std::size(path));
    if (!len || len == std::size(path)) return {};
    std::wstring dir(path, len);
    dir.resize(dir.find_last_of(L'\\') + 1);
    return dir;
}

[[gnu::format(printf, 1, 2)]]
static void report(const char *format, ...)
{
    char line[768];
    va_list args;
    va_start(args, format);
    const auto length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length <= 0 || length >= int(sizeof(line))) return;
    std::fputs(line, stderr);
    const auto path = moduleDirectory() + L"fh6-metal-fsr4.log";
    const auto file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, DWORD(length), &written, nullptr);
    CloseHandle(file);
}

struct Provider {
    PfnFfxCreateContext create;
    PfnFfxDestroyContext destroy;
    PfnFfxDispatch dispatch;
    PfnFfxConfigure configure;
    PfnFfxQuery query;

    explicit Provider(HMODULE module)
        : create(reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module, "ffxCreateContext"))),
          destroy(reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module, "ffxDestroyContext"))),
          dispatch(reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module, "ffxDispatch"))),
          configure(reinterpret_cast<PfnFfxConfigure>(GetProcAddress(module, "ffxConfigure"))),
          query(reinterpret_cast<PfnFfxQuery>(GetProcAddress(module, "ffxQuery"))) {}
};

static const Provider &provider()
{
    static const Provider value(LoadLibraryExW((moduleDirectory() +
        L"amd_fidelityfx_upscaler_original_dx12.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
    return value;
}

struct Bridge {
    UINT64 handle = 0;
    DispatchUnix call = nullptr;
    Bridge()
    {
        auto path = L"\\??\\" + moduleDirectory() + L"fh6-fsr-bridge.so";
        const auto ntdll = GetModuleHandleW(L"ntdll.dll");
        using Query = NTSTATUS (NTAPI *)(HANDLE, PVOID, int, PVOID, SIZE_T, PSIZE_T);
        const auto query = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryVirtualMemory"));
        const auto dispatcher = reinterpret_cast<DispatchUnix *>(GetProcAddress(ntdll, "__wine_unix_call_dispatcher"));
        if (!query || !dispatcher || !*dispatcher || path.size() > 32760) return;
        UNICODE_STRING nt = {USHORT(path.size() * 2), USHORT((path.size() + 1) * 2), path.data()};
        UINT64 handles[2] = {};
        if (!query(GetCurrentProcess(), &nt, 1002, handles, sizeof(handles), nullptr)) {
            handle = handles[1];
            call = *dispatcher;
        }
    }
};

struct Surface {
    ID3D12Heap *heap = nullptr;
    ID3D12Resource *texture = nullptr;
    FsrHeapTexture metal{};
    ~Surface() {
        if (texture) texture->Release();
        if (heap) heap->Release();
    }

    bool create(ID3D12Device *device, ID3D12DXVKInteropDevice3 *interop,
                const D3D12_RESOURCE_DESC &input, DXGI_FORMAT format)
    {
        const D3D12_HEAP_PROPERTIES properties = {D3D12_HEAP_TYPE_DEFAULT};
        const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, input.Width,
            input.Height, 1, 1, format, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
            static_cast<D3D12_RESOURCE_FLAGS>(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)};
        const auto size = device->GetResourceAllocationInfo(0, 1, &desc);
        const D3D12_HEAP_DESC allocation = {size.SizeInBytes, properties, size.Alignment, D3D12_HEAP_FLAG_NONE};
        UINT32 type;
        if (size.SizeInBytes == UINT64_MAX || FAILED(device->CreateHeap(&allocation, IID_PPV_ARGS(&heap))) ||
            FAILED(device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                nullptr, IID_PPV_ARGS(&texture))) ||
            FAILED(interop->GetVulkanHeapInfo(heap, &metal.memory, &metal.offset, &type)))
            return false;
        metal.width = uint32_t(input.Width);
        metal.height = input.Height;
        metal.format = format;
        return true;
    }

    bool matches(const D3D12_RESOURCE_DESC &desc, DXGI_FORMAT format) const
    {
        return metal.width == desc.Width && metal.height == desc.Height && metal.format == format;
    }
};

struct Staging {
    std::array<Surface, 5> surfaces;
};

struct Context {
    ffxCreateContextDescUpscale description{};
    uint64_t owner;
    std::mutex mutex;
    bool lastNative = false;
    bool reportedFallback = false;
    std::vector<std::shared_ptr<Staging>> staging;
};

static std::mutex contextsMutex;
static std::unordered_map<ffxContext, std::shared_ptr<Context>> contexts;
static std::atomic<uint64_t> nextOwner{1};

class Pass final : public IUnknown {
    std::atomic<ULONG> references{1};
    std::shared_ptr<Staging> staging;
    const Bridge &bridge;
    std::array<ID3D12Resource *, 5> resources{};
public:
    FsrJob job;
    Pass(std::shared_ptr<Staging> textures, const Bridge &bridge, const FsrJob &job,
         const std::array<ID3D12Resource *, 5> &resources)
        : staging(std::move(textures)), bridge(bridge), resources(resources), job(job) {
        for (auto *resource : resources) if (resource) resource->AddRef();
    }
    ~Pass() { for (auto *resource : resources) if (resource) resource->Release(); }
    static void WINAPI prepare(IUnknown *context);
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualGUID(iid, IID_IUnknown)) return E_NOINTERFACE;
        *out = static_cast<IUnknown *>(this);
        AddRef();
        return S_OK;
    }
    static HRESULT WINAPI run(IUnknown *context) {
        auto &pass = *static_cast<Pass *>(context);
        const auto status = pass.bridge.call(pass.bridge.handle, 0, &pass.job);
        static std::atomic<bool> reportedSuccess{false}, reportedFailure{false};
        auto &reported = status ? reportedFailure : reportedSuccess;
        if (!reported.exchange(true, std::memory_order_relaxed))
            report("FH6 Metal FSR4 dispatch status=%#lx GPU=%.3f ms\n",
                   static_cast<unsigned long>(status), pass.job.gpuMs);
        return status ? E_FAIL : S_OK;
    }
};

static DXGI_FORMAT typedFormat(DXGI_FORMAT format, unsigned slot)
{
    if (slot == 1 || slot == 4) {
        if (format == DXGI_FORMAT_R32_FLOAT || format == DXGI_FORMAT_D32_FLOAT ||
            format == DXGI_FORMAT_R32_TYPELESS) return DXGI_FORMAT_R32_FLOAT;
        if (format == DXGI_FORMAT_R16_FLOAT) return format;
    } else if (slot == 2) {
        if (format == DXGI_FORMAT_R16G16_FLOAT || format == DXGI_FORMAT_R16G16_TYPELESS)
            return DXGI_FORMAT_R16G16_FLOAT;
    } else {
        switch (format) {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM: return format;
        default: break;
        }
    }
    return DXGI_FORMAT_UNKNOWN;
}

static void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *texture,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {texture, 0, before, after};
    list->ResourceBarrier(1, &barrier);
}

static void copy(ID3D12GraphicsCommandList *list, ID3D12Resource *src, ID3D12Resource *dst,
                 D3D12_RESOURCE_STATES srcState, D3D12_RESOURCE_STATES dstState)
{
    transition(list, src, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(list, dst, dstState, D3D12_RESOURCE_STATE_COPY_DEST);
    const D3D12_TEXTURE_COPY_LOCATION source = {src, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    const D3D12_TEXTURE_COPY_LOCATION dest = {dst, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
    transition(list, dst, D3D12_RESOURCE_STATE_COPY_DEST, dstState);
    transition(list, src, D3D12_RESOURCE_STATE_COPY_SOURCE, srcState);
}

void WINAPI Pass::prepare(IUnknown *context)
{
    auto &pass = *static_cast<Pass *>(context);
    auto *list = static_cast<ID3D12GraphicsCommandList *>(pass.job.dispatch.commandList);
    for (unsigned i = 0; i < pass.resources.size(); ++i)
        if (i != 3 && pass.resources[i])
            copy(list, pass.resources[i], pass.staging->surfaces[i].texture,
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
}

template<typename T> static void release(T *p) { if (p) p->Release(); }

static const char *upscale(Context &context, const ffxDispatchDescUpscale &dispatch,
                           unsigned &detail)
{
    const auto record = reinterpret_cast<RecordPass>(
        GetProcAddress(GetModuleHandleW(L"d3d12core.dll"), "VKD3DRecordHostPass"));
    // Command allocators can outlive an FFX context or the caller's DLL reference.
    static const bool pinned = [] {
        HMODULE module = nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                  reinterpret_cast<LPCWSTR>(&shim), &module) != FALSE;
    }();
    static const Bridge bridge;
    if (!record) return "renderer host-pass export missing";
    if (!pinned) return "provider could not be pinned";
    if (!bridge.handle) return "Wine Unix bridge unavailable";
    if (!dispatch.commandList) return "missing D3D12 command list";
    if (dispatch.header.pNext || dispatch.flags || dispatch.enableSharpening ||
        dispatch.reactive.resource || dispatch.transparencyAndComposition.resource)
        return "dispatch has unsupported flags, sharpening, masks, or extensions";

    const bool exposure = dispatch.exposure.resource && !(context.description.flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE);
    const FfxApiResource inputs[] = {dispatch.color, dispatch.depth, dispatch.motionVectors, dispatch.output, dispatch.exposure};
    const unsigned count = exposure ? 5 : 4;
    std::array<ID3D12Resource *, 5> resources{};
    D3D12_RESOURCE_DESC descriptions[5]{};
    DXGI_FORMAT formats[5]{};
    for (unsigned i = 0; i < count; ++i) {
        resources[i] = static_cast<ID3D12Resource *>(inputs[i].resource);
        const auto expected = i == 3 ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ;
        if (!resources[i] || inputs[i].state != expected) {
            detail = i;
            return "resource missing or state incompatible";
        }
        descriptions[i] = resources[i]->GetDesc();
        const auto &desc = descriptions[i];
        formats[i] = typedFormat(desc.Format, i);
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 ||
            desc.SampleDesc.Count != 1 || desc.Width > UINT32_MAX || !formats[i] ||
            (i == 4 && (desc.Width != 1 || desc.Height != 1))) {
            detail = (i << 16) | unsigned(desc.Format);
            return "resource format or shape incompatible";
        }
    }

    auto *list = static_cast<ID3D12GraphicsCommandList *>(dispatch.commandList);
    ID3D12Device *rawDevice = nullptr;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&rawDevice)))) return "D3D12 command list device unavailable";
    const std::unique_ptr<ID3D12Device, decltype(&release<ID3D12Device>)> device(rawDevice, release<ID3D12Device>);
    ID3D12DXVKInteropDevice3 *rawInterop = nullptr;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&rawInterop)))) return "D3D12 Vulkan heap interop unavailable";
    const std::unique_ptr<ID3D12DXVKInteropDevice3, decltype(&release<ID3D12DXVKInteropDevice3>)>
        interop(rawInterop, release<ID3D12DXVKInteropDevice3>);
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice vulkan{};
    if (FAILED(interop->GetVulkanHandles(&instance, &physical, &vulkan)) || !vulkan) return "Vulkan device unavailable";

    std::shared_ptr<Staging> staging;
    for (auto it = context.staging.begin(); it != context.staging.end();) {
        const auto &candidate = *it;
        if (candidate.use_count() != 1) { ++it; continue; }
        bool matches = exposure == bool(candidate->surfaces[4].texture);
        for (unsigned i = 0; matches && i < count; ++i)
            matches = candidate->surfaces[i].matches(descriptions[i], formats[i]);
        if (matches) { staging = candidate; break; }
        it = context.staging.erase(it);
    }
    if (!staging) {
        if (context.staging.size() >= 8) return "all staging sets are in flight";
        staging = std::make_shared<Staging>();
        for (unsigned i = 0; i < count; ++i) {
            if (!staging->surfaces[i].create(device.get(), interop.get(), descriptions[i], formats[i])) {
                detail = (i << 16) | unsigned(formats[i]);
                return "could not allocate shareable staging texture";
            }
        }
        context.staging.push_back(staging);
    }

    FsrJob job{};
    job.size = sizeof(job);
    job.device = reinterpret_cast<UINT64>(vulkan);
    job.owner = context.owner;
    job.context = context.description;
    job.dispatch = dispatch;
    job.dispatch.reset = dispatch.reset || !context.lastNative;
    job.color = staging->surfaces[0].metal;
    job.depth = staging->surfaces[1].metal;
    job.motion = staging->surfaces[2].metal;
    job.output = staging->surfaces[3].metal;
    job.exposure = staging->surfaces[4].metal;
    if (const auto status = bridge.call(bridge.handle, 1, &job)) {
        detail = unsigned(status);
        return "Metal library or descriptor validation failed";
    }

    auto *pass = new Pass(staging, bridge, job, resources);
    const auto result = record(list, Pass::prepare, Pass::run, pass);
    pass->Release();
    if (FAILED(result)) {
        detail = unsigned(result);
        return "vkd3d could not split this command list";
    }
    copy(list, staging->surfaces[3].texture, resources[3],
         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return nullptr;
}

extern "C" __declspec(dllexport)
ffxReturnCode_t ffxCreateContext(ffxContext *context, ffxCreateContextDescHeader *desc,
                                 const ffxAllocationCallbacks *memory)
{
    const auto create = provider().create;
    if (!create) return FFX_API_RETURN_NO_PROVIDER;
    const auto result = create(context, desc, memory);
    if (!result && context && *context && desc && desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
        auto entry = std::make_shared<Context>();
        entry->description = *reinterpret_cast<const ffxCreateContextDescUpscale *>(desc);
        entry->description.header.pNext = nullptr;
        entry->description.fpMessage = nullptr;
        entry->owner = nextOwner.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock(contextsMutex);
        contexts[*context] = std::move(entry);
    }
    return result;
}

extern "C" __declspec(dllexport)
ffxReturnCode_t ffxDestroyContext(ffxContext *context, const ffxAllocationCallbacks *memory)
{
    if (context) {
        std::lock_guard lock(contextsMutex);
        contexts.erase(*context);
    }
    const auto destroy = provider().destroy;
    return destroy ? destroy(context, memory) : FFX_API_RETURN_NO_PROVIDER;
}

extern "C" __declspec(dllexport)
ffxReturnCode_t ffxConfigure(ffxContext *context, const ffxConfigureDescHeader *desc)
{
    const auto configure = provider().configure;
    return configure ? configure(context, desc) : FFX_API_RETURN_NO_PROVIDER;
}

extern "C" __declspec(dllexport)
ffxReturnCode_t ffxQuery(ffxContext *context, ffxQueryDescHeader *desc)
{
    const auto query = provider().query;
    return query ? query(context, desc) : FFX_API_RETURN_NO_PROVIDER;
}

extern "C" __declspec(dllexport)
ffxReturnCode_t ffxDispatch(ffxContext *context, const ffxDispatchDescHeader *desc)
{
    const auto dispatch = provider().dispatch;
    if (!dispatch) return FFX_API_RETURN_NO_PROVIDER;
    if (!context || !desc || desc->type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE)
        return dispatch(context, desc);
    std::shared_ptr<Context> entry;
    {
        std::lock_guard lock(contextsMutex);
        if (auto it = contexts.find(*context); it != contexts.end()) entry = it->second;
    }
    if (!entry) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true, std::memory_order_relaxed))
            report("FH6 Metal FSR4 fallback: upscale context was not registered\n");
        return dispatch(context, desc);
    }
    auto params = *reinterpret_cast<const ffxDispatchDescUpscale *>(desc);
    std::lock_guard lock(entry->mutex);
    unsigned detail = 0;
    const auto reason = upscale(*entry, params, detail);
    if (!reason) {
        entry->lastNative = true;
        return FFX_API_RETURN_OK;
    }
    if (!entry->reportedFallback) {
        const int listType = params.commandList ?
            static_cast<ID3D12GraphicsCommandList *>(params.commandList)->GetType() : -1;
        report("FH6 Metal FSR4 fallback: %s (detail=%#x list=%d context flags=%#x dispatch flags=%#x "
            "sharpness=%d reactive=%d transparency=%d extension=%d, formats=%u/%u/%u/%u/%u, "
            "states=%#x/%#x/%#x/%#x/%#x, size=%ux%u->%ux%u)\n",
            reason, detail, listType, entry->description.flags, params.flags, params.enableSharpening,
            params.reactive.resource != nullptr, params.transparencyAndComposition.resource != nullptr,
            params.header.pNext != nullptr, params.color.description.format, params.depth.description.format,
            params.motionVectors.description.format, params.output.description.format, params.exposure.description.format,
            params.color.state, params.depth.state, params.motionVectors.state, params.output.state, params.exposure.state,
            params.renderSize.width, params.renderSize.height, params.upscaleSize.width, params.upscaleSize.height);
        entry->reportedFallback = true;
    }
    params.reset |= entry->lastNative;
    entry->lastNative = false;
    return dispatch(context, &params.header);
}
