#pragma once
#include <atomic>
#include <initializer_list>

// Trace only the FFX fences named through the public D3D12 ABI. Report a poll
// streak once it exceeds 80 ms and report which value finally moved it forward.
namespace Fh6FenceObserver {
using CreateFence = HRESULT (STDMETHODCALLTYPE *)(ID3D12Device *, UINT64, D3D12_FENCE_FLAGS, REFIID, void **);
using SetName = HRESULT (STDMETHODCALLTYPE *)(ID3D12Fence *, LPCWSTR);
using Completed = UINT64 (STDMETHODCALLTYPE *)(ID3D12Fence *);
using Release = ULONG (STDMETHODCALLTYPE *)(ID3D12Fence *);
using QueueOperation = HRESULT (STDMETHODCALLTYPE *)(ID3D12CommandQueue *, ID3D12Fence *, UINT64);
static CreateFence createFence;
static SetName setName;
static Completed completed;
static Release release;
static QueueOperation queueSignal, queueWait;
struct Fence {
    std::atomic<ID3D12Fence *> object{};
    std::atomic<unsigned> id{}, instance{};
};
static Fence fences[32];
static SRWLOCK registry = SRWLOCK_INIT;
static unsigned nextInstance;
static volatile LONG reports;
static std::atomic<int64_t> interval{};
static constexpr const wchar_t *names[] = {
    L"AMD FSR GameFence", L"AMD FSR InterpolationFence", L"AMD FSR PresentFence",
    L"AMD FSR ReplacementBufferFence", L"AMD FSR CompositionFence GPU", L"AMD FSR CompositionFence CPU",
};
static constexpr const char *labels[] = { "game", "interpolation", "present", "replacement", "composition-gpu", "composition-cpu" };
struct Operation {
    const char *kind;
    unsigned id, instance;
    UINT64 value;
    uintptr_t queue;
    DWORD thread;
    int64_t qpc;
};
static SRWLOCK operationLock = SRWLOCK_INIT;
static Operation operations[512]{};
static unsigned operationCursor;

static Fence *findFence(ID3D12Fence *fence)
{
    for (auto &f : fences)
        if (f.object.load(std::memory_order_acquire) == fence) return &f;
    return nullptr;
}

static void noteOperation(const char *kind, ID3D12Fence *fence, UINT64 value, ID3D12CommandQueue *queue = nullptr)
{
    auto entry = findFence(fence);
    if (!entry || !report) return;
    LARGE_INTEGER stamp;
    if (!QueryPerformanceCounter(&stamp)) return;
    const Operation op{kind, entry->id.load(std::memory_order_relaxed), entry->instance.load(std::memory_order_relaxed),
                       value, reinterpret_cast<uintptr_t>(queue), GetCurrentThreadId(), stamp.QuadPart};
    AcquireSRWLockExclusive(&operationLock);
    operations[operationCursor++ % ARRAYSIZE(operations)] = op;
    ReleaseSRWLockExclusive(&operationLock);
}

static Operation reportOperations(unsigned instance, UINT64 completedValue, int64_t observedAt)
{
    // All normal operations stay in memory. On a long wait, emit the first
    // pending value's producer and the latest queued dependency before it.
    Operation producer{}, dependency{};
    AcquireSRWLockShared(&operationLock);
    for (const auto &op : operations) {
        if (op.instance != instance || !op.kind || op.value <= completedValue ||
            op.qpc > observedAt || strcmp(op.kind, "signal")) continue;
        if (!producer.kind || op.value < producer.value || (op.value == producer.value && op.qpc < producer.qpc))
            producer = op;
    }
    if (producer.queue) for (const auto &op : operations) {
        if (op.queue != producer.queue || op.qpc > producer.qpc || !op.kind || strcmp(op.kind, "wait")) continue;
        if (!dependency.kind || op.qpc > dependency.qpc) dependency = op;
    }
    ReleaseSRWLockShared(&operationLock);
    for (const auto &op : {dependency, producer}) {
        if (!op.kind) continue;
        char line[295];
        snprintf(line, sizeof(line), "{\"event\":\"fg_fence_queue\",\"kind\":\"%s\",\"fence\":\"%s\","
            "\"instance\":%u,\"observedInstance\":%u,\"value\":%llu,\"queue\":\"%llx\",\"thread\":%lu,\"operationQpc\":%lld}",
            op.kind, labels[op.id], op.instance, instance, (unsigned long long)op.value,
            (unsigned long long)op.queue, (unsigned long)op.thread, (long long)op.qpc);
        report(line);
    }
    return dependency;
}

static void reportDependency(const Operation &dependency, unsigned observedInstance, const char *phase, int64_t observedAt)
{
    if (!dependency.kind) return;
    Operation signal{};
    AcquireSRWLockShared(&operationLock);
    for (const auto &op : operations) {
        if (op.instance != dependency.instance || !op.kind || strcmp(op.kind, "signal") ||
            op.value < dependency.value || op.qpc > observedAt) continue;
        if (!signal.kind || op.value < signal.value || (op.value == signal.value && op.qpc < signal.qpc))
            signal = op;
    }
    ReleaseSRWLockShared(&operationLock);

    // Hold the registry lock across the query to exclude final Release. This
    // queries only on a long stall and its recovery, outside the polling path.
    UINT64 done = UINT64_MAX;
    AcquireSRWLockShared(&registry);
    for (auto &f : fences) {
        if (f.instance.load(std::memory_order_relaxed) != dependency.instance) continue;
        if (auto object = f.object.load(std::memory_order_relaxed)) done = completed(object);
        break;
    }
    ReleaseSRWLockShared(&registry);
    const char *status = done == UINT64_MAX ? "unknown" : done >= dependency.value ? "reached" : "pending";
    char line[410];
    snprintf(line, sizeof(line), "{\"event\":\"fg_fence_dependency\",\"phase\":\"%s\",\"fence\":\"%s\","
        "\"instance\":%u,\"observedInstance\":%u,\"value\":%llu,\"completed\":%llu,\"status\":\"%s\","
        "\"producerValue\":%llu,\"producerQueue\":\"%llx\",\"producerThread\":%lu,\"producerQpc\":%lld}",
        phase, labels[dependency.id], dependency.instance, observedInstance, (unsigned long long)dependency.value,
        (unsigned long long)done, status, (unsigned long long)signal.value, (unsigned long long)signal.queue,
        (unsigned long)signal.thread, (long long)signal.qpc);
    report(line);
}

static HRESULT STDMETHODCALLTYPE nameFence(ID3D12Fence *fence, LPCWSTR name)
{
    const HRESULT result = setName(fence, name);
    if (FAILED(result) || !name) return result;
    unsigned i = 0;
    while (i < ARRAYSIZE(names) && wcscmp(name, names[i])) ++i;
    if (i == ARRAYSIZE(names)) return result;
    AcquireSRWLockExclusive(&registry);
    auto existing = findFence(fence);
    for (auto &f : fences) {
        auto object = f.object.load(std::memory_order_relaxed);
        if (existing ? &f != existing : object != nullptr) continue;
        if (!existing) f.instance.store(++nextInstance, std::memory_order_relaxed);
        f.id.store(i, std::memory_order_relaxed);
        f.object.store(fence, std::memory_order_release);
        break;
    }
    ReleaseSRWLockExclusive(&registry);
    return result;
}

static ULONG STDMETHODCALLTYPE releaseFence(ID3D12Fence *fence)
{
    if (!findFence(fence)) return release(fence);
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

static bool emit(const char *phase, unsigned id, unsigned instance, UINT64 value, int64_t duration, int64_t stamp)
{
    if (!report || InterlockedIncrement(&reports) > 96) return false;
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) return false;
    char line[300];
    snprintf(line, sizeof(line), "{\"event\":\"fg_fence_poll\",\"phase\":\"%s\",\"fence\":\"%s\","
        "\"instance\":%u,\"thread\":%lu,\"completed\":%llu,\"durationQpc\":%lld,\"qpcHz\":%lld,\"observedQpc\":%lld}",
        phase, labels[id], instance, (unsigned long)GetCurrentThreadId(), (unsigned long long)value, (long long)duration,
        (long long)frequency.QuadPart, (long long)stamp);
    report(line);
    return true;
}

static UINT64 STDMETHODCALLTYPE pollFence(ID3D12Fence *fence)
{
    const UINT64 value = completed(fence);
    const auto threshold = interval.load(std::memory_order_relaxed);
    if (!threshold || !report || value == UINT64_MAX) return value;
    struct Poll { ID3D12Fence *object; unsigned instance; UINT64 value; int64_t first, last; bool reported; Operation dependency; };
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
        const auto instance = f.instance.load(std::memory_order_relaxed);
        if (state.instance == instance && state.reported && state.value != value &&
            emit("advanced", id, instance, value, stamp - state.first, stamp))
            reportDependency(state.dependency, instance, "advanced", stamp);
        if (state.instance != instance || state.value != value || stamp - state.last > threshold / 4)
            state = {fence, instance, value, stamp, stamp, false, {}};
        if (!state.reported && stamp - state.first >= threshold) {
            if (emit("stalled", id, instance, value, stamp - state.first, stamp)) {
                state.dependency = reportOperations(instance, value, stamp);
                reportDependency(state.dependency, instance, "stalled", stamp);
            }
            state.reported = true;
        }
        state.last = stamp;
        break;
    }
    return value;
}

static HRESULT STDMETHODCALLTYPE observeQueueSignal(ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
    const HRESULT result = queueSignal(queue, fence, value);
    if (SUCCEEDED(result)) noteOperation("signal", fence, value, queue);
    return result;
}

static HRESULT STDMETHODCALLTYPE observeQueueWait(ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
    const HRESULT result = queueWait(queue, fence, value);
    if (SUCCEEDED(result)) noteOperation("wait", fence, value, queue);
    return result;
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
    table = *reinterpret_cast<void ***>(queue);
    attachLatencyHook(reinterpret_cast<QueueOperation *>(&table[14]), observeQueueSignal, queueSignal);
    attachLatencyHook(reinterpret_cast<QueueOperation *>(&table[15]), observeQueueWait, queueWait);
    if (selected && report) report("{\"event\":\"fg_fence_observer_ready\"}");
}
}
