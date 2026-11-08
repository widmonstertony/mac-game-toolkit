#include <vulkan/vulkan_core.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <winternl.h>
#include "fsr_job.hpp"
#include "amd-framegen.hpp"
#include <vkd3d_device_vkd3d_ext.h>
#define CHECK(call) do { const auto hr = (call); if (FAILED(hr)) { printf("%s failed: %#lx\n", #call, hr); return 1; } } while (0)
extern "C" HRESULT WINAPI D3D12CreateDevice(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
int wmain(int argc, wchar_t **argv)
{
    if (argc != 3) return 2;
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
    // FH6 supplies a display-sized color allocation with a smaller active region.
    constexpr uint32_t rw = 1736, rh = 1133, uw = 3472, uh = 2266;
    const uint32_t widths[] = {uw, rw, rw, uw}, heights[] = {uh, rh, rh, uh};
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
    const D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    ID3D12CommandQueue *queue = nullptr;
    ID3D12Fence *fence = nullptr;
    CHECK(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return 1;
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
    HMODULE sr = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!sr) { printf("Upscale DLL load failed: %lu\n", GetLastError()); return 1; }
    auto query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(sr, "ffxQuery"));
    auto create = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(sr, "ffxCreateContext"));
    auto dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(sr, "ffxDispatch"));
    auto destroy = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(sr, "ffxDestroyContext"));
    if (!query || !create || !dispatch || !destroy) return 1;
    constexpr auto effect = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    uint64_t ids[8] = {}, count = 8;
    const char *names[8] = {};
    ffxQueryDescGetVersions versions{{FFX_API_QUERY_DESC_TYPE_GET_VERSIONS}, effect, device, &count, ids, names};
    if (query(nullptr, &versions.header) || !count || count > 8) return 1;
    ffxOverrideVersion selected{{FFX_API_DESC_TYPE_OVERRIDE_VERSION}, ids[0]};
    struct { ffxCreateContextDescHeader header; ID3D12Device *device; } backend{
        {FFX_API_MAKE_BACKEND_SUB_ID(FFX_API_BACKEND_ID_DX12, 0x02), &selected.header}, device};
    ffxCreateContextDescUpscale context{{effect, &backend.header},
        FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
        FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION, {uw,uh}, {uw,uh}, nullptr};
    ffxContext srContext{};
    const auto createResult = create(&srContext, &context.header, nullptr);
    printf("Upscale context via the OptiScaler DLL interface: %u provider %s\n", createResult, names[0]);
    if (createResult) return 1;
    ffxDispatchDescUpscale request{};
    request.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    request.commandList = list;
    request.color.resource = surfaces[0].texture;
    request.depth.resource = surfaces[1].texture;
    request.motionVectors.resource = surfaces[2].texture;
    request.output.resource = surfaces[3].texture;
    request.color.state = request.depth.state = request.motionVectors.state = FFX_API_RESOURCE_STATE_COMPUTE_READ;
    request.output.state = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
    request.renderSize = {rw,rh};
    request.upscaleSize = {uw,uh};
    request.motionVectorScale = {1.f,1.f};
    request.preExposure = 1.f;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 bufferSize = 0;
    device->GetCopyableFootprints(&surfaces[3].desc, 0, 1, 0, &footprint, nullptr, nullptr, &bufferSize);
    D3D12_HEAP_PROPERTIES cpu = {D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bufferDesc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, bufferSize, 1, 1, 1,
        DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *readback = nullptr;
    CHECK(device->CreateCommittedResource(&cpu, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    auto *consumer = list;
    auto *fg = createFrameGen(argv[2], device, rw, rh, uw, uh);
    if (!fg) return 1;
    ID3D12Resource *generated = nullptr;
    CHECK(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &surfaces[3].desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&generated)));
    ID3D12Resource *generatedReadback = nullptr;
    CHECK(device->CreateCommittedResource(&cpu, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&generatedReadback)));
    const auto transition = [](ID3D12GraphicsCommandList *commands, ID3D12Resource *texture,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        commands->ResourceBarrier(1, &b);
    };
    const auto copy = [&](ID3D12Resource *texture, ID3D12Resource *buffer, D3D12_RESOURCE_STATES state) {
        transition(consumer, texture, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
        src.pResource = texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = buffer; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
        consumer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transition(consumer, texture, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
    };
    struct Edge { unsigned left, right; };
    const auto edges = [&](ID3D12Resource *buffer) -> Edge {
        void *mapped = nullptr;
        if (FAILED(buffer->Map(0, nullptr, &mapped))) return {};
        const auto *base = static_cast<const unsigned char *>(mapped);
        Edge edge{uw, 0};
        for (unsigned x = 0; x < uw; ++x) {
            const auto *pixel = reinterpret_cast<const uint16_t *>(base + footprint.Offset +
                (uh / 2) * footprint.Footprint.RowPitch + x * 8);
            if (pixel[0] > 0x38cd && pixel[0] < 0x7c00) {
                if (edge.left == uw) edge.left = x;
                edge.right = x;
            }
        }
        const D3D12_RANGE noWrite{};
        buffer->Unmap(0, &noWrite);
        return edge;
    };
    // Allow a few pixels of variance from the temporal filters and sample phase.
    const auto midpoint = [](unsigned before, unsigned after, unsigned generated) {
        return before < generated && generated < after &&
            std::abs(int(2 * generated) - int(before + after)) <= 6;
    };
    Edge previous{};
    unsigned verified = 0;
    for (UINT64 frame = 0; frame < 3; ++frame) {
        CHECK(allocator->Reset());
        CHECK(list->Reset(allocator, nullptr));
        const float values[][4] = {{0.10f,0.25f,0.4f,1.f},{0.5f,0,0,0},
            {frame ? -36.f : 0.f,0,0,0},{0,0,0,0}};
        auto handle = rtvHandle;
        for (unsigned i = 0; i < 4; ++i) {
            auto *texture = surfaces[i].texture;
            device->CreateRenderTargetView(texture, nullptr, handle);
            transition(list, texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->ClearRenderTargetView(handle, values[i], 0, nullptr);
            if (i == 0) {
                const auto x = LONG(900 + frame * 36);
                const D3D12_RECT rect = {x, rh / 2 - 100, x + 300, rh / 2 + 100};
                const float red[] = {0.85f,0.1f,0.15f,1.f};
                list->ClearRenderTargetView(handle, red, 1, &rect);
            }
            transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
            handle.ptr += stride;
        }
        constexpr auto computeRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        for (unsigned i = 0; i < 3; ++i)
            transition(list, surfaces[i].texture, D3D12_RESOURCE_STATE_COMMON, computeRead);
        transition(list, surfaces[3].texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        request.reset = frame == 0;
        request.frameTimeDelta = 16.667f;
        const auto result = dispatch(&srContext, &request.header);
        if (result) { printf("Upscale dispatch failed: %u\n", result); return 1; }
        transition(list, surfaces[3].texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, computeRead);
        if (!recordFrameGen(fg, consumer, surfaces[1].texture, surfaces[2].texture, surfaces[3].texture,
                            generated, frame)) return 1;
        copy(surfaces[3].texture, readback, computeRead);
        copy(generated, generatedReadback, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        for (unsigned i = 0; i < 4; ++i)
            transition(consumer, surfaces[i].texture, computeRead, D3D12_RESOURCE_STATE_COMMON);
        CHECK(consumer->Close());

        CHECK(enqueue(consumer, frame + 1));
        CHECK(fence->SetEventOnCompletion(frame + 1, event));
        if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) return 1;
        CHECK(device->GetDeviceRemovedReason());

        const auto sr = edges(readback);
        const auto gen = edges(generatedReadback);
        printf("Frame %llu upscale edge=%u..%u, generated edge=%u..%u, output=%ux%u\n",
            (unsigned long long)frame, sr.left, sr.right, gen.left, gen.right, uw, uh);
        if (frame && midpoint(previous.left, sr.left, gen.left) &&
                     midpoint(previous.right, sr.right, gen.right)) ++verified;
        previous = sr;
    }
    printf("Verified generated intermediate frames: %u/2\n", verified);
    destroyFrameGen(fg);
    destroy(&srContext, nullptr);
    FreeLibrary(sr);
    generatedReadback->Release();
    generated->Release();
    readback->Release();
    rtv->Release();
    CloseHandle(event);
    fence->Release();
    queue->Release();
    for (auto &surface : surfaces) { surface.texture->Release(); surface.heap->Release(); }
    heapInterop->Release();
    list->Release();
    allocator->Release();
    interop->Release();
    device->Release();
    return verified == 2 ? 0 : 1;
}
