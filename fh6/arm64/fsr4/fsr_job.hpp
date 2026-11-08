#pragma once
#include <upscalers/include/ffx_upscale.h>

// Same-process, 64-bit Wine Unix-call ABI. The D3D12 caller owns these
// allocations and keeps them live until the Metal completion is observed.
struct FsrHeapTexture {
    uint64_t memory, offset;
    uint32_t width, height;
    uint32_t format; // Typed DXGI format. Zero selects the original probe format.
};

struct FsrJob {
    uint32_t size;
    uint32_t reserved;
    uint64_t device;
    uint64_t owner;
    ffxCreateContextDescUpscale context;
    ffxDispatchDescUpscale dispatch;
    FsrHeapTexture color, depth, motion, output, exposure;
    double gpuMs;
};
