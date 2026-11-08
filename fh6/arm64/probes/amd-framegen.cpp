#include "amd-framegen.hpp"
#include <cstdio>
#include <windows.h>
#include <api/include/dx12/ffx_api_dx12.h>
#include <framegeneration/include/ffx_framegeneration.h>

struct FrameGen {
    HMODULE module;
    PfnFfxConfigure configure;
    PfnFfxDispatch dispatch;
    PfnFfxDestroyContext destroy;
    ffxContext context;
    FfxApiDimensions2D render, display;
};

FrameGen *createFrameGen(const wchar_t *library, ID3D12Device *device, uint32_t rw, uint32_t rh, uint32_t uw, uint32_t uh,
                        ID3D12Resource *presentColor)
{
    HMODULE module = LoadLibraryExW(library, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { printf("Frame generation DLL load failed: %lu\n", GetLastError()); return nullptr; }
    const auto create = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module, "ffxCreateContext"));
    const auto query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(module, "ffxQuery"));
    auto *fg = new FrameGen{module,
        reinterpret_cast<PfnFfxConfigure>(GetProcAddress(module, "ffxConfigure")),
        reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module, "ffxDispatch")),
        reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module, "ffxDestroyContext")),
        nullptr, {rw, rh}, {uw, uh}};
    if (!create || !query || !fg->dispatch || !fg->configure || !fg->destroy) {
        destroyFrameGen(fg);
        return nullptr;
    }
    constexpr auto type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    uint64_t ids[8] = {}, count = 8;
    const char *names[8] = {};
    ffxQueryDescGetVersions versions{{FFX_API_QUERY_DESC_TYPE_GET_VERSIONS}, type, device, &count, ids, names};
    if (query(nullptr, &versions.header) || count < 1 || count > 8) {
        destroyFrameGen(fg);
        return nullptr;
    }
    ffxOverrideVersion selected{{FFX_API_DESC_TYPE_OVERRIDE_VERSION}, ids[0]};
    ffxCreateBackendDX12Desc backend{{FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12, &selected.header}, device};
    ffxCreateContextDescFrameGeneration desc{{type, &backend.header}, 0, fg->display, fg->render,
        ffxApiGetSurfaceFormatDX12(presentColor->GetDesc().Format)};
    const auto result = create(&fg->context, &desc.header, nullptr);
    printf("Frame generation provider %s, create status=%u\n", names[0] ? names[0] : "unknown", result);
    if (result) {
        destroyFrameGen(fg);
        return nullptr;
    }
    return fg;
}

bool recordFrameGen(FrameGen *fg, ID3D12GraphicsCommandList *list, ID3D12Resource *depth, ID3D12Resource *motion,
                    ID3D12Resource *present, ID3D12Resource *output, uint64_t frame)
{
    constexpr uint32_t flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    const FfxApiRect2D rect = {0, 0, int32_t(fg->display.width), int32_t(fg->display.height)};
    ffxConfigureDescFrameGeneration configure = {};
    configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configure.frameGenerationEnabled = true;
    configure.flags = flags;
    configure.generationRect = rect;
    configure.frameID = frame;
    auto result = fg->configure(&fg->context, &configure.header);
    if (result) { printf("Frame generation configure failed: %u\n", result); return false; }

    ffxDispatchDescFrameGenerationPrepareV2 prepare = {};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.frameID = frame;
    prepare.flags = flags;
    prepare.commandList = list;
    prepare.renderSize = fg->render;
    prepare.motionVectorScale = {1, 1};
    prepare.frameTimeDelta = 16.667f;
    prepare.reset = frame == 0;
    prepare.cameraNear = 0.1f;
    prepare.cameraFar = 1000.f;
    prepare.cameraFovAngleVertical = 1.f;
    prepare.viewSpaceToMetersFactor = 1.f;
    prepare.depth = ffxApiGetResourceDX12(depth);
    prepare.motionVectors = ffxApiGetResourceDX12(motion);
    prepare.cameraUp[1] = prepare.cameraRight[0] = prepare.cameraForward[2] = 1.f;
    result = fg->dispatch(&fg->context, &prepare.header);
    if (result) { printf("Frame generation prepare failed: %u\n", result); return false; }

    ffxDispatchDescFrameGeneration dispatch = {};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    dispatch.commandList = list;
    dispatch.presentColor = ffxApiGetResourceDX12(present);
    dispatch.outputs[0] = ffxApiGetResourceDX12(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.numGeneratedFrames = 1;
    dispatch.reset = frame == 0;
    dispatch.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;
    dispatch.minMaxLuminance[1] = 100.f;
    dispatch.generationRect = rect;
    dispatch.frameID = frame;
    result = fg->dispatch(&fg->context, &dispatch.header);
    printf("Frame generation dispatch %llu status=%u\n", (unsigned long long)frame, result);
    return !result;
}

void destroyFrameGen(FrameGen *fg)
{
    if (fg->context) fg->destroy(&fg->context, nullptr);
    FreeLibrary(fg->module);
    delete fg;
}
