#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <simd/simd.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <map>
#include <unistd.h>
#include "bridge-abi.h"

@interface FH6MetalContext : NSObject
@property id<MTLDevice> device;
@property id<MTLCommandQueue> queue;
@property id<MTLFXFrameInterpolator> fg;
@property id<MTLComputePipelineState> conversion;
@property id<MTLTexture> previous,current,depth,motion,output;
@property uint64_t lastFrame;
@property uint32_t historySamples;
@property BOOL history,poisoned,cameraKnown;
@property uint32_t rw,rh,w,h;
// On timeout these references intentionally remain alive until process exit.
@property NSArray *pending;
@end
@implementation FH6MetalContext
@end
static std::mutex guard;
static std::map<uint64_t,FH6MetalContext*> contexts;
static uint64_t nextContext=1;
static id<MTLTexture> privateTexture(id<MTLDevice> d,MTLPixelFormat f,NSUInteger w,NSUInteger h,MTLTextureUsage usage) {
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];
    td.storageMode=MTLStorageModePrivate;td.usage=usage|MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
    return [d newTextureWithDescriptor:td];
}
static MTLPixelFormat format(uint32_t f) {
    switch(f){case FX_RGBA8:return MTLPixelFormatRGBA8Unorm;case FX_RGBA16F:return MTLPixelFormatRGBA16Float;case FX_R32F:return MTLPixelFormatR32Float;case FX_RG16F:return MTLPixelFormatRG16Float;default:return MTLPixelFormatInvalid;}
}
static id<MTLTexture> mappedTexture(id<MTLDevice> d,const FxBuffer &b) {
    const uint64_t page=getpagesize(),pixel=b.format==FX_RGBA16F?8:4;
    if(!b.address||b.address%page||!b.bytes||b.bytes%page||b.bytes>256*1024*1024||!b.width||!b.height||b.width>8192||b.height>8192||b.rowPitch<b.width*pixel||b.rowPitch%256||uint64_t(b.rowPitch)*b.height>b.bytes||format(b.format)==MTLPixelFormatInvalid)return nil;
    // Caller owns the D3D12 allocation and holds it mapped through completion.
    auto buffer=[d newBufferWithBytesNoCopy:(void*)b.address length:b.bytes options:MTLResourceStorageModeShared deallocator:nil];
    if(!buffer)return nil;
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format(b.format) width:b.width height:b.height mipmapped:NO];
    td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
    return [buffer newTextureWithDescriptor:td offset:0 bytesPerRow:b.rowPitch];
}
static bool finish(FH6MetalContext *c,id<MTLCommandBuffer> cb,NSArray *retained) {
    c.pending=retained;
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    [cb addCompletedHandler:^(id<MTLCommandBuffer>){dispatch_semaphore_signal(done);}];
    [cb commit];
    if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))){c.poisoned=YES;return false;}
    c.pending=nil;
    if(cb.status!=MTLCommandBufferStatusCompleted){c.poisoned=YES;NSLog(@"FH6 MetalFX command failed: %@",cb.error);return false;}
    return true;
}
static void convert(FH6MetalContext *c,id<MTLCommandBuffer> cb,id<MTLTexture> src,id<MTLTexture> dst) {
    auto e=[cb computeCommandEncoder];[e setComputePipelineState:c.conversion];
    [e setTexture:src atIndex:0];[e setTexture:dst atIndex:1];
    [e dispatchThreads:MTLSizeMake(dst.width,dst.height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];[e endEncoding];
}
static int32_t caps(void *p){@autoreleasepool{auto d=MTLCreateSystemDefaultDevice();if(!p||!d)return -1;*(uint64_t*)p=([MTLFXTemporalScalerDescriptor supportsDevice:d]?1:0)|([MTLFXFrameInterpolatorDescriptor supportsDevice:d]?2:0);if(@available(macOS 27.0,*))*(uint64_t*)p|=4;return 0;}}
static int32_t create(void *raw) {@autoreleasepool{
    auto *p=(FxCreate*)raw;if(!p||p->abiVersion!=1||p->reserved||!p->renderWidth||!p->renderHeight||p->renderWidth>p->displayWidth||p->renderHeight>p->displayHeight||p->displayWidth>8192||p->displayHeight>8192)return -2;
    std::lock_guard<std::mutex> lock(guard);if(contexts.size()>=4)return -3;
    auto c=[FH6MetalContext new];c.device=MTLCreateSystemDefaultDevice();if(!c.device||![MTLFXFrameInterpolatorDescriptor supportsDevice:c.device])return -4;
    c.rw=p->renderWidth;c.rh=p->renderHeight;c.w=p->displayWidth;c.h=p->displayHeight;c.queue=[c.device newCommandQueue];
    auto desc=[MTLFXFrameInterpolatorDescriptor new];desc.colorTextureFormat=MTLPixelFormatRGBA16Float;desc.outputTextureFormat=MTLPixelFormatRGBA16Float;desc.depthTextureFormat=MTLPixelFormatR32Float;desc.motionTextureFormat=MTLPixelFormatRG16Float;desc.inputWidth=c.rw;desc.inputHeight=c.rh;desc.outputWidth=c.w;desc.outputHeight=c.h;
    c.fg=[desc newFrameInterpolatorWithDevice:c.device];if(!c.fg||!c.queue)return -5;
    c.previous=privateTexture(c.device,desc.colorTextureFormat,c.w,c.h,c.fg.colorTextureUsage);
    c.current=privateTexture(c.device,desc.colorTextureFormat,c.w,c.h,c.fg.colorTextureUsage);
    c.depth=privateTexture(c.device,desc.depthTextureFormat,c.rw,c.rh,c.fg.depthTextureUsage);
    c.motion=privateTexture(c.device,desc.motionTextureFormat,c.rw,c.rh,c.fg.motionTextureUsage);
    c.output=privateTexture(c.device,desc.outputTextureFormat,c.w,c.h,c.fg.outputTextureUsage);
    NSError *error=nil;
    NSString *source=@"#include <metal_stdlib>\nusing namespace metal;kernel void convert(texture2d<float,access::read> a[[texture(0)]],texture2d<float,access::write> b[[texture(1)]],uint2 xy[[thread_position_in_grid]]){if(xy.x<b.get_width()&&xy.y<b.get_height())b.write(a.read(xy),xy);}";
    auto library=[c.device newLibraryWithSource:source options:nil error:&error];
    c.conversion=[c.device newComputePipelineStateWithFunction:[library newFunctionWithName:@"convert"] error:&error];
    if(!c.previous||!c.current||!c.depth||!c.motion||!c.output||!c.conversion){NSLog(@"MetalFX create failure %@",error);return -6;}
    p->context=nextContext++;contexts.emplace(p->context,c);return 0;
}}
static int32_t dispatchNative(FxDispatch *p,const FxCamera *camera) {@autoreleasepool{
    if(!p)return -2;p->generated=0;p->gpuMilliseconds=p->wallMilliseconds=0;
    const bool hasCamera=camera&&(camera->flags&1);
    if(camera&&(camera->reserved||(camera->flags&~1u)))return -13;
    simd_float4x4 view=matrix_identity_float4x4,projection=matrix_identity_float4x4;
    if(hasCamera){
        if(@available(macOS 27.0,*)){}else return -13;
        for(float x:camera->worldToView)if(!std::isfinite(x))return -13;
        for(float x:camera->viewToClip)if(!std::isfinite(x))return -13;
        memcpy(&view,camera->worldToView,sizeof(view));memcpy(&projection,camera->viewToClip,sizeof(projection));
        if(std::abs(simd_determinant(view))<1e-10f||std::abs(simd_determinant(projection))<1e-10f)return -13;
    }
    std::lock_guard<std::mutex> lock(guard);auto found=contexts.find(p->context);if(found==contexts.end())return -7;auto c=found->second;if(c.poisoned)return -8;
    if((c.history&&p->frameId<=c.lastFrame)||!std::isfinite(p->deltaSeconds)||p->deltaSeconds<=0||p->deltaSeconds>1||!std::isfinite(p->fovDegrees)||p->fovDegrees<=0||p->fovDegrees>=180||!std::isfinite(p->aspectRatio)||p->aspectRatio<=0||!std::isfinite(p->nearPlane)||!std::isfinite(p->farPlane)||p->nearPlane<=0||p->farPlane<=p->nearPlane||!std::isfinite(p->motionScaleX)||!std::isfinite(p->motionScaleY)||!std::isfinite(p->jitterX)||!std::isfinite(p->jitterY))return -9;
    if(p->color.width!=c.w||p->color.height!=c.h||p->output.width!=c.w||p->output.height!=c.h||p->depth.width!=c.rw||p->depth.height!=c.rh||p->motion.width!=c.rw||p->motion.height!=c.rh||p->depth.format!=FX_R32F||p->motion.format!=FX_RG16F||(p->color.format!=FX_RGBA8&&p->color.format!=FX_RGBA16F)||(p->output.format!=FX_RGBA8&&p->output.format!=FX_RGBA16F))return -10;
    double start=NSProcessInfo.processInfo.systemUptime;
    auto color=mappedTexture(c.device,p->color),depth=mappedTexture(c.device,p->depth),motion=mappedTexture(c.device,p->motion),out=mappedTexture(c.device,p->output);if(!color||!depth||!motion||!out)return -11;
    auto cb=[c.queue commandBuffer];cb.label=@"FH6 MetalFX staged interpolation";
    convert(c,cb,color,c.current);convert(c,cb,depth,c.depth);convert(c,cb,motion,c.motion);
    const bool reset=!c.history||p->reset||p->frameId!=c.lastFrame+1||c.cameraKnown!=hasCamera;
    if(reset){auto e=[cb blitCommandEncoder];[e copyFromTexture:c.current toTexture:c.previous];[e endEncoding];}
    c.fg.colorTexture=c.current;c.fg.prevColorTexture=c.previous;c.fg.depthTexture=c.depth;c.fg.motionTexture=c.motion;c.fg.outputTexture=c.output;
    c.fg.jitterOffsetX=p->jitterX;c.fg.jitterOffsetY=p->jitterY;
    // This Mac's native interpolator consumes offsets in the depth/motion
    // input grid. Direct output-pixel offsets duplicate occluded foreground
    // with 1/2 and 2/3 input sizes. The real-GPU layered perspective fixture
    // verifies the normalization and retains an uncorrected negative control.
    // Keep our external ABI in output-pixel coordinates, independent of SR.
    c.fg.motionVectorScaleX=p->motionScaleX*float(c.rw)/c.w;
    c.fg.motionVectorScaleY=p->motionScaleY*float(c.rh)/c.h;
    c.fg.nearPlane=p->nearPlane;c.fg.farPlane=p->farPlane;c.fg.fieldOfView=p->fovDegrees;c.fg.aspectRatio=p->aspectRatio;c.fg.deltaTime=p->deltaSeconds;c.fg.depthReversed=p->depthReversed;c.fg.shouldResetHistory=reset;
    if(@available(macOS 27.0,*)){c.fg.worldToViewMatrix=view;c.fg.viewToClipMatrix=projection;}
    [c.fg encodeToCommandBuffer:cb];convert(c,cb,c.output,out);
    if(!finish(c,cb,@[color,depth,motion,out]))return -12;
    p->gpuMilliseconds=1000*(cb.GPUEndTime-cb.GPUStartTime);p->wallMilliseconds=1000*(NSProcessInfo.processInfo.systemUptime-start);
    auto temp=c.previous;c.previous=c.current;c.current=temp;c.lastFrame=p->frameId;c.history=YES;c.cameraKnown=hasCamera;
    // On this MetalFX implementation the reset submission AND the following
    // submission produce the current frame. Only the third sample has an
    // interpolated output (verified with moving-square GPU readback). Never
    // enqueue a duplicated startup frame as a generated midpoint.
    c.historySamples=reset?1:std::min(c.historySamples+1,3u);
    p->generated=c.historySamples>=3?1:0;return 0;
}}
static int32_t dispatch(void *raw){return dispatchNative((FxDispatch*)raw,nullptr);}
static int32_t dispatchCamera(void *raw){auto p=(FxDispatchCamera*)raw;return p?dispatchNative(&p->frame,&p->camera):-2;}
static int32_t destroy(void *raw){auto *p=(FxDestroy*)raw;if(!p)return -2;std::lock_guard<std::mutex> lock(guard);auto found=contexts.find(p->context);if(found==contexts.end())return -7;if(found->second.poisoned)return -8;contexts.erase(found);p->context=0;return 0;}
static int32_t bufferControl(void *raw){@autoreleasepool{
    auto p=(FxBuffer*)raw;if(!p)return -2;auto d=MTLCreateSystemDefaultDevice();auto t=mappedTexture(d,*p);if(!t)return -11;
    auto c=[FH6MetalContext new];c.queue=[d newCommandQueue];auto cb=[c.queue commandBuffer];auto e=[cb blitCommandEncoder];[e fillBuffer:t.buffer range:NSMakeRange(0,p->bytes) value:0x5a];[e endEncoding];return finish(c,cb,@[t])?0:-12;
}}
extern "C" __attribute__((visibility("default"))) int32_t (*__wine_unix_call_funcs[])(void*)={caps,create,dispatch,destroy,bufferControl,dispatchCamera};
