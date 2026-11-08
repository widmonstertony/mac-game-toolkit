#pragma once

#include <cmath>
#include <upscalers/include/ffx_upscale.h>

struct FgMotionSample {
    void *depth = nullptr, *motion = nullptr;
    FfxApiDimensions2D size{};
    FfxApiFloatCoords2D jitter{}, scale{};
    uint64_t time = 0;

    bool matches(void *inputDepth, void *inputMotion, FfxApiDimensions2D inputSize,
                 FfxApiFloatCoords2D inputJitter, uint64_t now) const
    {
        return depth && motion && depth == inputDepth && motion == inputMotion &&
               size.width == inputSize.width && size.height == inputSize.height &&
               std::abs(jitter.x - inputJitter.x) < 0.0001f &&
               std::abs(jitter.y - inputJitter.y) < 0.0001f &&
               now >= time && now - time <= 80;
    }
};

// Identical inputs should use the same signed pixel-space scale in FidelityFX
// upscaling and interpolation. Only correct a pure axis reversal.
inline bool fixFgMotionAxis(float &scale, float reference)
{
    if (!std::isfinite(scale) || !std::isfinite(reference) ||
        std::signbit(scale) == std::signbit(reference) || std::abs(reference) < 1.0f ||
        std::abs(scale + reference) > std::abs(reference) * 0.01f)
        return false;
    scale = reference;
    return true;
}

using FindFgMotion = int (WINAPI *)(void *, void *, FfxApiDimensions2D,
                                    FfxApiFloatCoords2D, FfxApiFloatCoords2D *);
