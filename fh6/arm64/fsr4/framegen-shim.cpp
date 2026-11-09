#include <windows.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>

#include "fg-motion.hpp"
#include "gpu-label.hpp"

static HMODULE module;

BOOL WINAPI DllMain(HMODULE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}

static const std::wstring &directory()
{
    static const auto path = [] {
        wchar_t filename[32768];
        const auto length = GetModuleFileNameW(module, filename, std::size(filename));
        if (!length || length == std::size(filename)) return std::wstring{};
        std::wstring result(filename, length);
        result.resize(result.find_last_of(L'\\') + 1);
        return result;
    }();
    return path;
}

static FARPROC original(const char *name)
{
    static const auto library = LoadLibraryExW(
        (directory() + L"amd_fidelityfx_framegeneration_original_dx12.dll").c_str(),
        nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    return GetProcAddress(library, name);
}

static void report(const char *line, int length)
{
    if (length <= 0) return;
    const auto file = CreateFileW((directory() + L"fh6-framegen-motion.log").c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, DWORD(length), &written, nullptr);
    CloseHandle(file);
}

template<typename Prepare> static void checkMotion(Prepare &params)
{
    static std::atomic<FindFgMotion> find{nullptr};
    auto match = find.load(std::memory_order_acquire);
    if (!match) {
        HMODULE upscaler;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,
                (directory() + L"amd_fidelityfx_upscaler_dx12.dll").c_str(), &upscaler)) {
            match = reinterpret_cast<FindFgMotion>(GetProcAddress(upscaler, "FH6FindUpscaleMotion"));
            if (match) find.store(match, std::memory_order_release);
        }
    }

    const auto before = params.motionVectorScale;
    FfxApiFloatCoords2D reference{};
    const bool matched = match && match(params.depth.resource, params.motionVectors.resource,
                                       params.renderSize, params.jitterOffset, &reference);
    unsigned corrected = 0;
    if (matched) {
        if (fixFgMotionAxis(params.motionVectorScale.x, reference.x)) corrected |= 1;
        if (fixFgMotionAxis(params.motionVectorScale.y, reference.y)) corrected |= 2;
    }

    static std::atomic<unsigned> seen[3]{};
    const auto count = seen[corrected ? 2 : matched ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    if (count < 4 || (count < 4800 && count % 600 == 0)) {
        char line[320];
        const int length = std::snprintf(line, sizeof(line),
            "FH6 frame generation frame=%llu render=%ux%u motion=%ux%u depth=%ux%u "
            "scale=%g,%g sameInput=%u upscaleScale=%g,%g correctedAxes=%u\n",
            static_cast<unsigned long long>(params.frameID), params.renderSize.width, params.renderSize.height,
            params.motionVectors.description.width, params.motionVectors.description.height,
            params.depth.description.width, params.depth.description.height,
            before.x, before.y, unsigned(matched), reference.x, reference.y, corrected);
        if (length < int(sizeof(line))) report(line, length);
    }
}

extern "C" __declspec(dllexport) ffxReturnCode_t ffxCreateContext(
    ffxContext *context, ffxCreateContextDescHeader *desc, const ffxAllocationCallbacks *memory)
{
    static const auto create = reinterpret_cast<PfnFfxCreateContext>(original("ffxCreateContext"));
    return create ? create(context, desc, memory) : FFX_API_RETURN_NO_PROVIDER;
}

extern "C" __declspec(dllexport) ffxReturnCode_t ffxDestroyContext(
    ffxContext *context, const ffxAllocationCallbacks *memory)
{
    static const auto destroy = reinterpret_cast<PfnFfxDestroyContext>(original("ffxDestroyContext"));
    return destroy ? destroy(context, memory) : FFX_API_RETURN_NO_PROVIDER;
}

extern "C" __declspec(dllexport) ffxReturnCode_t ffxConfigure(ffxContext *context, const ffxConfigureDescHeader *desc)
{
    static const auto configure = reinterpret_cast<PfnFfxConfigure>(original("ffxConfigure"));
    return configure ? configure(context, desc) : FFX_API_RETURN_NO_PROVIDER;
}

extern "C" __declspec(dllexport) ffxReturnCode_t ffxQuery(ffxContext *context, ffxQueryDescHeader *desc)
{
    static const auto query = reinterpret_cast<PfnFfxQuery>(original("ffxQuery"));
    return query ? query(context, desc) : FFX_API_RETURN_NO_PROVIDER;
}

static bool repairEmptyRect(ffxDispatchDescFrameGeneration &params)
{
    auto &rect = params.generationRect;
    const auto &color = params.presentColor;
    const auto &output = params.outputs[0];
    const auto width = color.description.width;
    const auto height = color.description.height;
    constexpr auto maxDimension = std::numeric_limits<int32_t>::max();
    // OptiScaler may center a missing extent before Streamline supplies its
    // interpolation viewport. Leave every valid viewport, including bars, alone.
    if (!params.numGeneratedFrames || !color.resource || !output.resource ||
        !width || !height || width > maxDimension || height > maxDimension ||
        width != output.description.width || height != output.description.height ||
        rect.width || rect.height || rect.left != int32_t(width / 2) || rect.top != int32_t(height / 2))
        return false;
    rect = {0, 0, int32_t(width), int32_t(height)};
    return true;
}

extern "C" __declspec(dllexport) ffxReturnCode_t ffxDispatch(ffxContext *context, const ffxDispatchDescHeader *desc)
{
    static const auto dispatch = reinterpret_cast<PfnFfxDispatch>(original("ffxDispatch"));
    if (!dispatch) return FFX_API_RETURN_NO_PROVIDER;
    if (desc && desc->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE) {
        auto params = *reinterpret_cast<const ffxDispatchDescFrameGenerationPrepare *>(desc);
        checkMotion(params);
        const Fh6GpuLabel gpuLabel(params.commandList, "FH6 2x FG preparation");
        return dispatch(context, &params.header);
    }
    if (desc && desc->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2) {
        auto params = *reinterpret_cast<const ffxDispatchDescFrameGenerationPrepareV2 *>(desc);
        checkMotion(params);
        const Fh6GpuLabel gpuLabel(params.commandList, "FH6 2x FG preparation");
        return dispatch(context, &params.header);
    }
    if (desc && desc->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION) {
        auto params = *reinterpret_cast<const ffxDispatchDescFrameGeneration *>(desc);
        const bool repaired = repairEmptyRect(params);
        const auto result = [&] {
            const Fh6GpuLabel gpuLabel(params.commandList, "FH6 2x FG interpolation");
            return dispatch(context, repaired ? &params.header : desc);
        }();
        static std::atomic<unsigned> generated{0}, failures{0};
        if (params.numGeneratedFrames) {
            const auto count = (result ? failures : generated).fetch_add(1, std::memory_order_relaxed);
            if (count < 4 || (!result && count % 600 == 0)) {
                char line[256];
                const auto &rect = params.generationRect;
                const int length = std::snprintf(line, sizeof(line),
                    "FH6 frame generation dispatch frame=%llu frames=%u result=%u rect=%d,%d+%dx%d reset=%d repairedEmptyRect=%u\n",
                    static_cast<unsigned long long>(params.frameID), params.numGeneratedFrames, result,
                    rect.left, rect.top, rect.width, rect.height, params.reset, unsigned(repaired));
                if (length < int(sizeof(line))) report(line, length);
            }
        }
        return result;
    }
    return dispatch(context, desc);
}
