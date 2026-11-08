#import <Metal/Metal.h>
#include <dlfcn.h>
#include <cstdlib>
#include "wine_heap.h"
#include "fsr_job.hpp"
#include "ffx_frame.hpp"

extern "C" int32_t fh6_export_heap(void *);

static id<MTLTexture> texture(uint64_t device, FsrHeapTexture surface, MTLPixelFormat format)
{
    if (!surface.width || !surface.height)
        return nil;
    switch (surface.format) {
    case 0: break;
    case 10: format = MTLPixelFormatRGBA16Float; break;
    case 24: format = MTLPixelFormatRGB10A2Unorm; break;
    case 26: format = MTLPixelFormatRG11B10Float; break;
    case 28: format = MTLPixelFormatRGBA8Unorm; break;
    case 34: format = MTLPixelFormatRG16Float; break;
    case 41: format = MTLPixelFormatR32Float; break;
    case 54: format = MTLPixelFormatR16Float; break;
    case 87: format = MTLPixelFormatBGRA8Unorm; break;
    default: return nil;
    }
    metal_heap_request request = {device, surface.memory, 0};
    if (fh6_export_heap(&request)) return nil;
    id<MTLHeap> heap = (__bridge id<MTLHeap>)(void *)(uintptr_t)request.heap;
    auto *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:surface.width height:surface.height mipmapped:NO];
    desc.storageMode = heap.storageMode;
    desc.cpuCacheMode = heap.cpuCacheMode;
    // Matches the probe's staging resources. Arbitrary game textures can
    // use another layout and must be copied to a compatible resource.
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
    const MTLSizeAndAlign needed = [heap.device heapTextureSizeAndAlignWithDescriptor:desc];
    if (!needed.align || surface.offset % needed.align || surface.offset > heap.size ||
        needed.size > heap.size - surface.offset)
        return nil;
    return [heap newTextureWithDescriptor:desc offset:surface.offset];
}

static int32_t run_fsr(void *data, bool validateOnly)
{
    @autoreleasepool {
        if (!data) return (int32_t)0xc000000d;
        auto &job = *static_cast<FsrJob *>(data);
        if (job.size != sizeof(job) || job.reserved || !job.device || !job.owner)
            return (int32_t)0xc000000d;
        job.gpuMs = 0;
        const char *path = getenv("FH6_NATIVE_FSR4_LIBRARY");
        if (!path) return (int32_t)0xc00000bb;
        // Keep the loaded runtime for the lifetime of this process. It owns
        // the pipeline cache and temporal state across submissions.
        static void *library = dlopen(path, RTLD_LOCAL | RTLD_NOW);
        if (!library) return (int32_t)0xc00000bb;
        static auto encode = reinterpret_cast<decltype(&fsr4_encode_live_frame_v2)>(
            dlsym(library, "fsr4_encode_live_frame_v2"));
        if (!encode) return (int32_t)0xc00000bb;

        id<MTLTexture> color = texture(job.device, job.color, MTLPixelFormatRGBA16Float);
        id<MTLTexture> depth = texture(job.device, job.depth, MTLPixelFormatR32Float);
        id<MTLTexture> motion = texture(job.device, job.motion, MTLPixelFormatRG16Float);
        id<MTLTexture> output = texture(job.device, job.output, MTLPixelFormatRGBA16Float);
        id<MTLTexture> exposure = texture(job.device, job.exposure, MTLPixelFormatR32Float);
        id<MTLDevice> device = color.device;
        if (!device || depth.device != device || motion.device != device || output.device != device)
            return (int32_t)0xc00000bb;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        const auto input = [](id<MTLTexture> tex) -> fh6::MetalSurface {
            return {(__bridge void *)tex, uint32_t(tex.width), uint32_t(tex.height)};
        };
        fh6::MetalUpscaleInputs surfaces = {(__bridge void *)device, (__bridge void *)commands,
            (void *)(uintptr_t)job.owner, input(color), input(depth), input(motion), input(exposure), input(output)};
        Fsr4LiveFrameV2 frame = {};
        if (!commands || !fh6::prepareFsr4Frame(job.context, job.dispatch, surfaces, frame))
            return (int32_t)0xc00000bb;
        if (validateOnly) return 0;
        if (!encode(&frame)) return (int32_t)0xc00000bb;
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted)
            return (int32_t)0xc0000001;
        job.gpuMs = (commands.GPUEndTime - commands.GPUStartTime) * 1000;
        return 0;
    }
}

extern "C" int32_t fh6_encode_fsr(void *data) { return run_fsr(data, false); }
extern "C" int32_t fh6_validate_fsr(void *data) { return run_fsr(data, true); }
