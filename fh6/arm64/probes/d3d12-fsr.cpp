#include <vulkan/vulkan_core.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <winternl.h>
#include "fsr_job.hpp"
#include <vkd3d_device_vkd3d_ext.h>
#define CHECK(call) do { const auto hr = (call); if (FAILED(hr)) { printf("%s failed: %#lx\n", #call, hr); return 1; } } while (0)
extern "C" HRESULT WINAPI D3D12CreateDevice(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
int wmain(int argc, wchar_t **argv)
{
    if (argc != 2) return 2;
    ID3D12Device *device;
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)));
    ID3D12DXVKInteropDevice1 *interop;
    CHECK(device->QueryInterface(IID_PPV_ARGS(&interop)));
    VkInstance instance = nullptr;
    VkPhysicalDevice physical = nullptr;
    VkDevice vulkan = nullptr;
    CHECK(interop->GetVulkanHandles(&instance, &physical, &vulkan));
    if (!instance || !physical || !vulkan) return 1;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)));
    const D3D12_HEAP_PROPERTIES properties = {D3D12_HEAP_TYPE_DEFAULT};
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_FLOAT};
    for (const auto format : formats) {
        const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 128, 64, 1, 1, format, {1,0},
            D3D12_TEXTURE_LAYOUT_UNKNOWN, format == DXGI_FORMAT_D32_FLOAT ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
        ID3D12Resource *resource;
        CHECK(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)));
        UINT64 image = 0, offset = 0;
        VkFormat vkFormat = static_cast<VkFormat>(0);
        CHECK(interop->GetVulkanResourceInfo1(resource, &image, &offset, &vkFormat));
        if (!image || !vkFormat) return 1;
        printf("D3D12 format=%u Vulkan image=OK format=%u offset=%llu\n", format, vkFormat, offset);
        resource->Release();
    }
    VkCommandBuffer buffer = nullptr;
    CHECK(interop->BeginVkCommandBufferInterop(list, &buffer));
    CHECK(interop->EndVkCommandBufferInterop(list));
    CHECK(list->Close());
    if (!buffer) return 1;
    ID3D12DXVKInteropDevice3 *heapInterop = nullptr;
    const auto hr = device->QueryInterface(IID_PPV_ARGS(&heapInterop));
    printf("Vulkan command buffer interop=OK, heap interop v3=%#lx\n", hr);
    if (!heapInterop) return 1;
    constexpr uint32_t rw = 2314, rh = 1510, uw = 3472, uh = 2266;
    const uint32_t widths[] = {rw, rw, rw, uw}, heights[] = {rh, rh, rh, uh};
    const DXGI_FORMAT stagingFormats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
    struct Surface {
        ID3D12Heap *heap;
        ID3D12Resource *texture;
        D3D12_RESOURCE_DESC desc;
        FsrHeapTexture metal;
    };
    Surface surfaces[4] = {};
    for (unsigned i = 0; i < 4; ++i) {
        auto &surface = surfaces[i];
        surface.desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, widths[i], heights[i], 1, 1,
            stagingFormats[i], {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, static_cast<D3D12_RESOURCE_FLAGS>(
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)};
        const auto size = device->GetResourceAllocationInfo(0, 1, &surface.desc);
        D3D12_HEAP_DESC desc = {size.SizeInBytes, properties, size.Alignment, D3D12_HEAP_FLAG_NONE};
        CHECK(device->CreateHeap(&desc, IID_PPV_ARGS(&surface.heap)));
        CHECK(device->CreatePlacedResource(surface.heap, 0, &surface.desc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&surface.texture)));
        UINT32 type = 0;
        CHECK(heapInterop->GetVulkanHeapInfo(surface.heap, &surface.metal.memory, &surface.metal.offset, &type));
        surface.metal.width = widths[i]; surface.metal.height = heights[i];
    }
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    using Query = NTSTATUS (NTAPI *)(HANDLE, PVOID, int, PVOID, SIZE_T, PSIZE_T);
    using Dispatch = NTSTATUS (WINAPI *)(UINT64, unsigned int, void *);
    auto query = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryVirtualMemory"));
    auto call = reinterpret_cast<Dispatch *>(GetProcAddress(ntdll, "__wine_unix_call_dispatcher"));
    if (!query || !call || !*call) { puts("Wine Unix library API unavailable"); return 1; }
    UNICODE_STRING path = {};
    path.Length = static_cast<USHORT>(wcslen(argv[1]) * sizeof(wchar_t));
    path.MaximumLength = path.Length + sizeof(wchar_t);
    path.Buffer = argv[1];
    UINT64 handles[2] = {};
    auto status = query(GetCurrentProcess(), &path, 1002, handles, sizeof(handles), nullptr);
    printf("Load native Metal bridge: %#lx\n", status);
    if (status || !handles[1]) return 1;
    const D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    ID3D12CommandQueue *queue = nullptr;
    ID3D12Fence *fence = nullptr;
    CHECK(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return 1;
    ID3D12Fence *metalDone = nullptr;
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&metalDone)));
    HANDLE producerEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!producerEvent) return 1;
    const auto enqueue = [&](ID3D12GraphicsCommandList *commands, UINT64 value) {
        ID3D12CommandList *lists[] = {commands};
        queue->ExecuteCommandLists(1, lists);
        return queue->Signal(fence, value);
    };
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4};
    ID3D12DescriptorHeap *rtv = nullptr;
    CHECK(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtv)));
    auto rtvHandle = rtv->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CHECK(list->Reset(allocator, nullptr));
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    const float values[][4] = {{0.25f,0.5f,0.75f,1.f},{0.5f,0,0,0},{0,0,0,0},{0,0,0,0}};
    for (unsigned i = 0; i < 4; ++i) {
        auto *texture = surfaces[i].texture;
        device->CreateRenderTargetView(texture, nullptr, rtvHandle);
        barrier.Transition = {texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET};
        list->ResourceBarrier(1, &barrier);
        list->ClearRenderTargetView(rtvHandle, values[i], 0, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1, &barrier);
        rtvHandle.ptr += stride;
    }
    CHECK(list->Close());
    CHECK(enqueue(list, 1));
    CHECK(fence->SetEventOnCompletion(1, producerEvent));
    FsrJob request = {};
    request.size = sizeof(request); request.device = reinterpret_cast<UINT64>(vulkan);
    request.owner = reinterpret_cast<UINT64>(&request);
    request.context = {{FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE}, FFX_UPSCALE_ENABLE_AUTO_EXPOSURE,
        {rw, rh}, {uw, uh}, nullptr};
    request.dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    request.dispatch.commandList = list;
    request.dispatch.color.resource = surfaces[0].texture;
    request.dispatch.depth.resource = surfaces[1].texture;
    request.dispatch.motionVectors.resource = surfaces[2].texture;
    request.dispatch.output.resource = surfaces[3].texture;
    request.dispatch.renderSize = {rw, rh};
    request.dispatch.motionVectorScale = {1.f, 1.f};
    request.dispatch.preExposure = 1.f;
    request.dispatch.reset = true;
    request.color = surfaces[0].metal; request.depth = surfaces[1].metal;
    request.motion = surfaces[2].metal; request.output = surfaces[3].metal;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 bufferSize = 0;
    device->GetCopyableFootprints(&surfaces[3].desc, 0, 1, 0, &footprint, nullptr, nullptr, &bufferSize);
    D3D12_HEAP_PROPERTIES cpu = {D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bufferDesc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, bufferSize, 1, 1, 1,
        DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *readback = nullptr;
    CHECK(device->CreateCommittedResource(&cpu, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    ID3D12CommandAllocator *consumerAllocator = nullptr;
    ID3D12GraphicsCommandList *consumer = nullptr;
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&consumerAllocator)));
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, consumerAllocator, nullptr, IID_PPV_ARGS(&consumer)));
    barrier.Transition.pResource = surfaces[3].texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    consumer->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
    src.pResource = surfaces[3].texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
    consumer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    CHECK(consumer->Close());

    // Enqueue the consumer before starting the Metal worker. The queue must
    // observe the real completion signal before touching the shared texture.
    CHECK(queue->Wait(metalDone, 1));
    CHECK(enqueue(consumer, 2));
    CHECK(fence->SetEventOnCompletion(2, event));
    HRESULT metalStatus = E_FAIL;
    std::thread worker([&] {
        if (WaitForSingleObject(producerEvent, 10000) != WAIT_OBJECT_0) return;
        const auto result = (*call)(handles[1], 0, &request);
        printf("Native Metal FSR status=%#lx GPU=%.3f ms\n", result, request.gpuMs);
        if (result) return;
        metalStatus = metalDone->Signal(1);
    });
    const bool completed = WaitForSingleObject(event, 15000) == WAIT_OBJECT_0;
    worker.join();
    printf("D3D12 fence handoff=%s\n", completed && SUCCEEDED(metalStatus) ? "OK" : "failed");
    if (!completed || FAILED(metalStatus)) return 1;
    void *mapped = nullptr;
    CHECK(readback->Map(0, nullptr, &mapped));
    const auto *base = static_cast<const unsigned char *>(mapped);
    unsigned correct = 0, total = 0;
    for (unsigned y = 8; y < uh - 8; y += 32)
        for (unsigned x = 8; x < uw - 8; x += 32) {
            auto p = reinterpret_cast<const uint16_t *>(base + footprint.Offset + y * footprint.Footprint.RowPitch + x * 8);
            correct += p[0] > 0 && p[0] < p[1] && p[1] < p[2] && p[2] < 0x7c00;
            ++total;
        }
    printf("D3D12 -> Metal FSR -> D3D12: %u/%u ordered RGB pixels at %ux%u\n", correct, total, uw, uh);
    readback->Unmap(0, nullptr);
    readback->Release();
    rtv->Release();
    consumer->Release();
    consumerAllocator->Release();
    CloseHandle(producerEvent);
    CloseHandle(event);
    metalDone->Release();
    fence->Release();
    queue->Release();
    if (correct != total) return 1;
    for (auto &surface : surfaces) { surface.texture->Release(); surface.heap->Release(); }
    heapInterop->Release();
    list->Release();
    allocator->Release();
    interop->Release();
    device->Release();
}
