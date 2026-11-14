#pragma once
#include <atomic>

// Trace only the FFX fences named through the public D3D12 ABI. Report a poll
// streak once it exceeds 80 ms and report which value finally moved it forward.
namespace Fh6FenceObserver {
using CreateFence = HRESULT (STDMETHODCALLTYPE *)(ID3D12Device *, UINT64, D3D12_FENCE_FLAGS, REFIID, void **);
using SetName = HRESULT (STDMETHODCALLTYPE *)(ID3D12Fence *, LPCWSTR);
using Completed = UINT64 (STDMETHODCALLTYPE *)(ID3D12Fence *);
using Release = ULONG (STDMETHODCALLTYPE *)(ID3D12Fence *);
static CreateFence createFence;
static SetName setName;
static Completed completed;
static Release release;
struct Fence { std::atomic<ID3D12Fence *> object{}; std::atomic<unsigned> id{}; };
static Fence fences[32];
static SRWLOCK registry = SRWLOCK_INIT;
static volatile LONG reports;
static std::atomic<int64_t> interval{};
static constexpr const wchar_t *names[] = {
    L"AMD FSR GameFence", L"AMD FSR InterpolationFence", L"AMD FSR PresentFence",
    L"AMD FSR ReplacementBufferFence", L"AMD FSR CompositionFence GPU", L"AMD FSR CompositionFence CPU",
};
static constexpr const char *labels[] = { "game", "interpolation", "present", "replacement", "composition-gpu", "composition-cpu" };

static HRESULT STDMETHODCALLTYPE nameFence(ID3D12Fence *fence, LPCWSTR name)
{
    const HRESULT result = setName(fence, name);
    if (FAILED(result) || !name) return result;
    unsigned i = 0;
    while (i < ARRAYSIZE(names) && wcscmp(name, names[i])) ++i;
    if (i == ARRAYSIZE(names)) return result;
    AcquireSRWLockExclusive(&registry);
    for (auto &f : fences) {
        auto object = f.object.load(std::memory_order_relaxed);
        if (object && object != fence) continue;
        f.id.store(i, std::memory_order_relaxed);
        f.object.store(fence, std::memory_order_release);
        break;
    }
    ReleaseSRWLockExclusive(&registry);
    return result;
}

static ULONG STDMETHODCALLTYPE releaseFence(ID3D12Fence *fence)
{
    // Keep registry writers serialized through the final Release, so a reused
    // object address cannot be mistaken for the old named fence.
    AcquireSRWLockExclusive(&registry);
    const ULONG count = release(fence);
    if (!count) for (auto &f : fences) {
        if (f.object.load(std::memory_order_relaxed) == fence) {
            f.object.store(nullptr, std::memory_order_release);
            break;
        }
    }
    ReleaseSRWLockExclusive(&registry);
    return count;
}

static void emit(const char *phase, unsigned id, UINT64 value, int64_t duration)
{
    if (!report || InterlockedIncrement(&reports) > 96) return;
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) return;
    char line[260];
    snprintf(line, sizeof(line), "{\"event\":\"fg_fence_poll\",\"phase\":\"%s\",\"fence\":\"%s\","
        "\"thread\":%lu,\"completed\":%llu,\"durationQpc\":%lld,\"qpcHz\":%lld}", phase, labels[id],
        (unsigned long)GetCurrentThreadId(), (unsigned long long)value, (long long)duration, (long long)frequency.QuadPart);
    report(line);
}

static UINT64 STDMETHODCALLTYPE pollFence(ID3D12Fence *fence)
{
    const UINT64 value = completed(fence);
    const auto threshold = interval.load(std::memory_order_relaxed);
    if (!threshold || !report) return value;
    struct Poll { ID3D12Fence *object; UINT64 value; int64_t first, last; bool reported; };
    static thread_local Poll state{};
    // Sample every 64 reads to keep an extra timer syscall out of the hot spin.
    static thread_local unsigned calls;
    if ((++calls & 63) && (!state.reported || state.object != fence || state.value == value)) return value;
    for (auto &f : fences) {
        if (f.object.load(std::memory_order_acquire) != fence) continue;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const auto stamp = now.QuadPart;
        const auto id = f.id.load(std::memory_order_relaxed);
        if (state.object == fence && state.reported && state.value != value)
            emit("advanced", id, value, stamp - state.first);
        if (state.object != fence || state.value != value || stamp - state.last > threshold / 4)
            state = {fence, value, stamp, stamp, false};
        if (!state.reported && stamp - state.first >= threshold) {
            emit("stalled", id, value, stamp - state.first);
            state.reported = true;
        }
        state.last = stamp;
        break;
    }
    return value;
}

static HRESULT STDMETHODCALLTYPE observeFenceCreation(ID3D12Device *device, UINT64 value, D3D12_FENCE_FLAGS flags,
                                                        REFIID iid, void **output)
{
    const HRESULT result = createFence(device, value, flags, iid, output);
    if (FAILED(result) || !output || !*output || iid != IID_ID3D12Fence) return result;
    auto table = *reinterpret_cast<void ***>(*output);
    // ID3D12Object::SetName, IUnknown::Release and ID3D12Fence::GetCompletedValue.
    attachLatencyHook(reinterpret_cast<SetName *>(&table[6]), nameFence, setName);
    attachLatencyHook(reinterpret_cast<Release *>(&table[2]), releaseFence, release);
    attachLatencyHook(reinterpret_cast<Completed *>(&table[8]), pollFence, completed);
    return result;
}

static void attach(ffxCreateContextDescHeader *desc)
{
    wchar_t enabled[2]{};
    if (!desc || desc->type != FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12 ||
        GetEnvironmentVariableW(L"FH6_D3DMETAL_FENCE_DIAGNOSTICS", enabled, 2) != 1 || enabled[0] != L'1') return;
    auto queue = reinterpret_cast<ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 *>(desc)->gameQueue;
    ID3D12Device *device = nullptr;
    if (!queue || FAILED(queue->GetDevice(IID_ID3D12Device, reinterpret_cast<void **>(&device)))) return;
    LARGE_INTEGER frequency;
    if (QueryPerformanceFrequency(&frequency)) interval = frequency.QuadPart * 8 / 100;
    auto table = *reinterpret_cast<void ***>(device);
    const bool selected = attachLatencyHook(reinterpret_cast<CreateFence *>(&table[36]), observeFenceCreation, createFence);
    device->Release();
    if (selected && report) report("{\"event\":\"fg_fence_observer_ready\"}");
}
}
