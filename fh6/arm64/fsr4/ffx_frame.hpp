#pragma once

#include <upscalers/include/ffx_upscale.h>
#include "fsr4_live.h"

namespace fh6 {

struct MetalSurface {
    void *texture;
    uint32_t width, height;
};

struct MetalUpscaleInputs {
    void *device;
    void *commandBuffer;
    void *owner;
    MetalSurface color, depth, motion, exposure, output;
};

// The caller resolves each FidelityFX resource to its corresponding Metal
// texture and keeps it alive through the submission. Failure leaves the
// output descriptor untouched so the existing FidelityFX dispatch can run.
inline bool prepareFsr4Frame(const ffxCreateContextDescUpscale &context,
                             const ffxDispatchDescUpscale &dispatch,
                             const MetalUpscaleInputs &inputs,
                             Fsr4LiveFrameV2 &frame)
{
    constexpr uint32_t supportedFlags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE |
        FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS |
        FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION |
        FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
        FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
    if (context.header.type != FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE ||
        dispatch.header.type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE ||
        (context.flags & ~supportedFlags) || dispatch.header.pNext || dispatch.flags || dispatch.enableSharpening ||
        dispatch.reactive.resource || dispatch.transparencyAndComposition.resource ||
        !dispatch.commandList || !inputs.device || !inputs.commandBuffer || !inputs.owner)
        return false;

    if (!dispatch.color.resource || !dispatch.depth.resource || !dispatch.motionVectors.resource ||
        !dispatch.output.resource || !inputs.color.texture || !inputs.depth.texture ||
        !inputs.motion.texture || !inputs.output.texture ||
        inputs.output.texture == inputs.color.texture || inputs.output.texture == inputs.depth.texture ||
        inputs.output.texture == inputs.motion.texture)
        return false;

    const bool autoExposure = (context.flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE) != 0;
    if (!autoExposure && dispatch.exposure.resource &&
        (!inputs.exposure.texture || inputs.exposure.width != 1 || inputs.exposure.height != 1 ||
         inputs.output.texture == inputs.exposure.texture))
        return false;

    const uint32_t outputWidth = dispatch.upscaleSize.width;
    const uint32_t outputHeight = dispatch.upscaleSize.height;
    if (((outputWidth == 0) != (outputHeight == 0)) ||
        !context.maxRenderSize.width || !context.maxRenderSize.height ||
        !context.maxUpscaleSize.width || !context.maxUpscaleSize.height ||
        outputWidth > context.maxUpscaleSize.width || outputHeight > context.maxUpscaleSize.height)
        return false;

    Fsr4LiveFrameV2 next = {};
    next.abiVersion = FSR4_LIVE_FRAME_ABI_V2;
    next.structSize = sizeof(next);
    const auto geometry = [](MetalSurface surface) -> Fsr4InputSurface {
        return {surface.width, surface.height, 0, 0};
    };
    next.geometry = {geometry(inputs.color), geometry(inputs.depth), geometry(inputs.motion),
        geometry(inputs.output), context.maxRenderSize.width, context.maxRenderSize.height};
    next.inputContentProperties = (context.flags & FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION) != 0;
    auto &out = next.frame;
    out.device = inputs.device;
    out.commandBuffer = inputs.commandBuffer;
    out.owner = inputs.owner;
    out.color = inputs.color.texture;
    out.depth = inputs.depth.texture;
    out.motion = inputs.motion.texture;
    out.output = inputs.output.texture;
    out.exposure = !autoExposure && dispatch.exposure.resource ? inputs.exposure.texture : nullptr;
    out.rw = dispatch.renderSize.width;
    out.rh = dispatch.renderSize.height;
    out.uw = outputWidth ? outputWidth : context.maxUpscaleSize.width;
    out.uh = outputHeight ? outputHeight : context.maxUpscaleSize.height;
    out.jitterX = dispatch.jitterOffset.x;
    out.jitterY = dispatch.jitterOffset.y;
    out.mvScaleX = dispatch.motionVectorScale.x;
    out.mvScaleY = dispatch.motionVectorScale.y;
    out.preExposure = dispatch.preExposure;
    out.reset = dispatch.reset;
    out.depthReversed = (context.flags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED) != 0;
    out.jitteredMV = (context.flags & FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) != 0;
    out.outputResMV = (context.flags & FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) != 0;
    out.autoExposure = autoExposure;

    const auto input = fsr4_live_input_desc(&out, &next.geometry, next.inputContentProperties);
    Fsr4InputPrepared prepared = {};
    if (!fsr4_input_status_ok(fsr4_input_prepare(&input, nullptr, 0, &prepared)))
        return false;
    frame = next;
    return true;
}

}  // namespace fh6
