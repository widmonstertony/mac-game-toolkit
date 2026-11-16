#pragma once
#include <stdint.h>
// PE/x86_64 and Mach-O/x86_64 share these fixed-width POD structures. This
// prototype uses GPU staging copies; it does NOT expose private D3DMetal ABI.
enum { FX_CAPS=0, FX_CREATE=1, FX_DISPATCH=2, FX_DESTROY=3, FX_BUFFER_CONTROL=4, FX_DISPATCH_CAMERA=5 };
struct FxBuffer { uint64_t address, bytes; uint32_t width,height,rowPitch,format; };
enum { FX_RGBA8=1, FX_RGBA16F=2, FX_R32F=3, FX_RG16F=4 };
struct FxCreate {
    uint32_t abiVersion, renderWidth,renderHeight,displayWidth,displayHeight;
    uint32_t reserved; uint64_t context;
};
struct FxDispatch {
    uint64_t context,frameId;
    FxBuffer color,depth,motion,output;
    // Caller motion scale converts raw vectors to OUTPUT-color pixel offsets.
    // Native implementation performs the tested input-grid normalization.
    float jitterX,jitterY,motionScaleX,motionScaleY;
    float nearPlane,farPlane,fovDegrees,aspectRatio,deltaSeconds;
    uint32_t reset,depthReversed,generated;
    double gpuMilliseconds,wallMilliseconds;
};
struct FxDestroy { uint64_t context; };
// Column-major mathematical transforms, matching simd_float4x4. Flag 1 means
// both matrices are supplied. A row-vector engine must transpose its matrices.
struct FxCamera { uint32_t flags,reserved; float worldToView[16],viewToClip[16]; };
// New operation preserves the original dispatch ABI and old callers.
struct FxDispatchCamera { FxDispatch frame; FxCamera camera; };
static_assert(sizeof(FxBuffer)==32,"buffer ABI");
static_assert(sizeof(FxCreate)==32,"create ABI");
static_assert(sizeof(FxDispatch)==208,"dispatch ABI");
static_assert(sizeof(FxCamera)==136,"camera ABI");
static_assert(sizeof(FxDispatchCamera)==344,"camera dispatch ABI");
