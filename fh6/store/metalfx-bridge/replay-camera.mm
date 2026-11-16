// Offline fixture only. Frozen history + one chronological pair, not a live-game repair.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#import <simd/simd.h>

static constexpr NSUInteger W = 3472, H = 2266, RW = 2314, RH = 1510;
static constexpr NSUInteger OutputRow = (W * 8 + 255) & ~255UL;

static NSData *read(NSString *directory, NSString *name) {
    NSData *data = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:name]];
    if (!data) std::fprintf(stderr, "Missing input %s\n", name.UTF8String);
    return data;
}

static id<MTLTexture> texture(id<MTLDevice> device, MTLPixelFormat format, NSUInteger width, NSUInteger height, MTLTextureUsage usage) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = usage;
    return [device newTextureWithDescriptor:d];
}

static bool upload(id<MTLDevice> device, id<MTLBlitCommandEncoder> blit, id<MTLTexture> dst, NSData *src, NSUInteger pitch, bool rgba8) {
    if (!src || !dst || src.length != dst.height * pitch) return false;
    const NSUInteger bpp = rgba8 ? 8 : 4;
    const NSUInteger row = (dst.width * bpp + 255) & ~255UL;
    id<MTLBuffer> input = [device newBufferWithLength:row * dst.height options:MTLResourceStorageModeShared];
    if (!input) return false;
    for (NSUInteger y = 0; y < dst.height; ++y) {
        const auto *raw = static_cast<const uint8_t *>(src.bytes) + y * pitch;
        auto *out = static_cast<uint8_t *>(input.contents) + y * row;
        if (rgba8) {
            auto *half = reinterpret_cast<_Float16 *>(out);
            for (NSUInteger x = 0; x < dst.width * 4; ++x) half[x] = raw[x] / 255.0f;
        } else {
            std::memcpy(out, raw, dst.width * bpp);
        }
    }
    [blit copyFromBuffer:input sourceOffset:0 sourceBytesPerRow:row sourceBytesPerImage:row * dst.height
                 sourceSize:MTLSizeMake(dst.width,dst.height,1) toTexture:dst destinationSlice:0
              destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    return true;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 4) return 1;
        const auto path = [](const char *p) { return [NSString stringWithUTF8String:p]; };
        NSString *older = path(argv[1]), *current = path(argv[2]), *out = path(argv[3]);
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        std::printf("GPU %s MetalFX=%d Metal4FX=%d\n", device.name.UTF8String,
                    [MTLFXFrameInterpolatorDescriptor supportsDevice:device],
                    [MTLFXFrameInterpolatorDescriptor supportsMetal4FX:device]);
        if (!device || ![MTLFXFrameInterpolatorDescriptor supportsDevice:device]) return 2;
        MTLFXFrameInterpolatorDescriptor *d = [MTLFXFrameInterpolatorDescriptor new];
        d.colorTextureFormat = MTLPixelFormatRGBA16Float;
        d.outputTextureFormat = MTLPixelFormatRGBA16Float;
        d.depthTextureFormat = MTLPixelFormatDepth32Float;
        d.motionTextureFormat = MTLPixelFormatRG16Float;
        d.inputWidth = RW;
        d.inputHeight = RH;
        d.outputWidth = W;
        d.outputHeight = H;
        id<MTLFXFrameInterpolator> interpolation = [d newFrameInterpolatorWithDevice:device];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!interpolation || !queue) return 3;
        auto prev = texture(device,d.colorTextureFormat,W,H,interpolation.colorTextureUsage);
        auto curr = texture(device,d.colorTextureFormat,W,H,interpolation.colorTextureUsage);
        auto depth = texture(device,d.depthTextureFormat,RW,RH,interpolation.depthTextureUsage);
        auto olderDepth = texture(device,d.depthTextureFormat,RW,RH,interpolation.depthTextureUsage);
        auto zeroMotion = texture(device,d.motionTextureFormat,RW,RH,interpolation.motionTextureUsage);
        auto motion = texture(device,d.motionTextureFormat,RW,RH,interpolation.motionTextureUsage);
        auto generated = texture(device,d.outputTextureFormat,W,H,interpolation.outputTextureUsage);
        auto readback = [device newBufferWithLength:OutputRow * H options:MTLResourceStorageModeShared];
        if (!prev || !curr || !depth || !olderDepth || !zeroMotion || !motion || !generated || !readback) return 4;
        id<MTLCommandBuffer> inputs = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [inputs blitCommandEncoder];
        if (!upload(device, blit, prev, read(older,@"scene.bin"), 14080, true) ||
            !upload(device, blit, curr, read(current,@"scene.bin"), 14080, true) ||
            !upload(device, blit, olderDepth, read(older,@"prepare-current-depth.bin"), 9728, false) ||
            !upload(device, blit, zeroMotion, [NSMutableData dataWithLength:9472*RH], 9472, false) ||
            !upload(device, blit, depth, read(current,@"prepare-current-depth.bin"), 9728, false) ||
            !upload(device, blit, motion, read(current,@"prepare-current-motion.bin"), 9472, false)) return 5;
        [blit endEncoding];
        [inputs commit];
        [inputs waitUntilCompleted];
        if (inputs.error) { NSLog(@"%@", inputs.error); return 6; }
        NSData *oldNative = read(older,@"native-frame.bin"), *newNative = read(current,@"native-frame.bin");
        if (oldNative.length != 608 || newNative.length != 608) return 10;
        float oldCamera[8], newCamera[8];
        std::memcpy(oldCamera, static_cast<const uint8_t *>(oldNative.bytes)+36, sizeof(oldCamera));
        std::memcpy(newCamera, static_cast<const uint8_t *>(newNative.bytes)+36, sizeof(newCamera));
        const bool cameraMatrices = argc > 6 && !std::strcmp(argv[6], "camera-matrices");
        const auto setCameraMatrices = [&](NSData *native) {
            float basis[12]; simd_float4x4 projection;
            std::memcpy(basis, static_cast<const uint8_t *>(native.bytes)+220, sizeof(basis));
            // Streamline stores row-vector matrices; its rows become SIMD columns.
            std::memcpy(&projection, static_cast<const uint8_t *>(native.bytes)+156, sizeof(projection));
            simd_float3 position={basis[0],basis[1],basis[2]}, up={basis[3],basis[4],basis[5]},
                right={basis[6],basis[7],basis[8]}, forward={basis[9],basis[10],basis[11]};
            simd_float4x4 view;
            view.columns[0]={right.x,up.x,forward.x,0};
            view.columns[1]={right.y,up.y,forward.y,0};
            view.columns[2]={right.z,up.z,forward.z,0};
            view.columns[3]={-simd_dot(right,position),-simd_dot(up,position),-simd_dot(forward,position),1};
            interpolation.worldToViewMatrix=view;
            interpolation.viewToClipMatrix=projection;
        };
        const bool renderPixelControl = argc > 4 && !std::strcmp(argv[4], "render-pixel-control");
        const int warmup = argc > 5 ? std::atoi(argv[5]) : 2;
        if (warmup < 2 || warmup > 24) { std::fprintf(stderr, "Need at least two initialization samples; the second dispatch is not an interpolated frame.\n"); return 11; }
        NSDictionary *metadata = [NSJSONSerialization JSONObjectWithData:read(current,@"capture.json") options:0 error:nil];
        const float delta = [metadata[@"prepare"][@"milliseconds"] floatValue]*.001f;
        if (!(delta > 0 && delta < 1)) return 12;
        interpolation.outputTexture = generated;
        interpolation.depthReversed = YES;
        for (int i = 0; i <= warmup; ++i) {
            const bool next = i == warmup;
            const float *camera = next ? newCamera : oldCamera;
            id<MTLCommandBuffer> command = [queue commandBuffer];
            command.label = next ? @"captured frame 58245 - interpolate" : @"frozen frame 58244 - initialize history";
            interpolation.motionVectorScaleX = camera[2] * (renderPixelControl ? RW : W);
            interpolation.motionVectorScaleY = camera[3] * (renderPixelControl ? RH : H);
            interpolation.nearPlane = camera[4];
            interpolation.farPlane = camera[5];
            interpolation.fieldOfView = camera[6]*180.0/M_PI;
            interpolation.aspectRatio = camera[7];
            if (cameraMatrices) setCameraMatrices(next ? newNative : oldNative);
            interpolation.depthTexture = next ? depth : olderDepth;
            interpolation.motionTexture = next ? motion : zeroMotion;
            interpolation.jitterOffsetX = camera[0];
            interpolation.jitterOffsetY = camera[1];
            interpolation.deltaTime = delta;
            interpolation.shouldResetHistory = i == 0;
            interpolation.colorTexture = next ? curr : prev;
            interpolation.prevColorTexture = prev;
            [interpolation encodeToCommandBuffer:command];
            [command commit];
            [command waitUntilCompleted];
            if (command.error) { NSLog(@"%@", command.error); return 7; }
            std::printf("step %d %s GPU %.3f ms scale %.1f %.1f\n", i, next ? "actual-pair" : "frozen-history", 1000*(command.GPUEndTime-command.GPUStartTime), interpolation.motionVectorScaleX, interpolation.motionVectorScaleY);
        }
        id<MTLCommandBuffer> results = [queue commandBuffer];
        blit = [results blitCommandEncoder];
        [blit copyFromTexture:generated sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                   sourceSize:MTLSizeMake(W,H,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:OutputRow destinationBytesPerImage:OutputRow * H];
        [blit endEncoding];
        [results commit];
        [results waitUntilCompleted];
        if (results.error) { NSLog(@"%@", results.error); return 8; }
        NSMutableData *packed = [NSMutableData dataWithLength:W*H*4];
        auto *write = static_cast<uint8_t *>(packed.mutableBytes);
        for (NSUInteger y = 0; y < H; ++y) {
            const auto *read = reinterpret_cast<const _Float16 *>(static_cast<const uint8_t *>(readback.contents) + y*OutputRow);
            for (NSUInteger x = 0; x < W*4; ++x)
                write[y*W*4+x] = static_cast<uint8_t>(std::round(std::fmin(1.,std::fmax(0.,read[x]))*255.0));
        }
        if (![packed writeToFile:out atomically:YES]) return 9;
    }
}
