// Included by bridge-native.mm. Private-texture temporal SR using the same
// bounded mapped-buffer contract as FG. This is not a live-game hook.
@interface FH6UpscaleContext : FH6MetalContext
@property id<MTLFXTemporalScaler> scaler;
@property id<MTLTexture> exposure;
@end
@implementation FH6UpscaleContext
@end
static std::map<uint64_t,FH6UpscaleContext*> upscaleContexts;

static int32_t upscaleCreate(void *raw) {@autoreleasepool{
    auto p=(FxCreate*)raw;
    if(!p||p->abiVersion!=1||p->reserved||!p->renderWidth||!p->renderHeight||
       p->renderWidth>p->displayWidth||p->renderHeight>p->displayHeight||
       p->displayWidth>8192||p->displayHeight>8192)return -2;
    std::lock_guard<std::mutex> lock(guard);if(upscaleContexts.size()>=4)return -3;
    auto c=[FH6UpscaleContext new];c.device=MTLCreateSystemDefaultDevice();
    if(!c.device||![MTLFXTemporalScalerDescriptor supportsDevice:c.device])return -4;
    c.rw=p->renderWidth;c.rh=p->renderHeight;c.w=p->displayWidth;c.h=p->displayHeight;
    c.queue=[c.device newCommandQueue];
    auto d=[MTLFXTemporalScalerDescriptor new];
    d.inputWidth=c.rw;d.inputHeight=c.rh;d.outputWidth=c.w;d.outputHeight=c.h;
    d.colorTextureFormat=MTLPixelFormatRGBA16Float;d.outputTextureFormat=MTLPixelFormatRGBA16Float;
    d.depthTextureFormat=MTLPixelFormatR32Float;d.motionTextureFormat=MTLPixelFormatRG16Float;
    d.autoExposureEnabled=NO;d.requiresSynchronousInitialization=YES;
    c.scaler=[d newTemporalScalerWithDevice:c.device];if(!c.scaler||!c.queue)return -5;
    c.current=privateTexture(c.device,d.colorTextureFormat,c.rw,c.rh,c.scaler.colorTextureUsage);
    c.depth=privateTexture(c.device,d.depthTextureFormat,c.rw,c.rh,c.scaler.depthTextureUsage);
    c.motion=privateTexture(c.device,d.motionTextureFormat,c.rw,c.rh,c.scaler.motionTextureUsage);
    c.output=privateTexture(c.device,d.outputTextureFormat,c.w,c.h,c.scaler.outputTextureUsage);
    auto ed=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Float width:1 height:1 mipmapped:NO];
    ed.storageMode=MTLStorageModeShared;ed.usage=MTLTextureUsageShaderRead;
    c.exposure=[c.device newTextureWithDescriptor:ed];
    NSError *error=nil;
    NSString *source=@"#include <metal_stdlib>\nusing namespace metal;kernel void convert(texture2d<float,access::read> a[[texture(0)]],texture2d<float,access::write> b[[texture(1)]],uint2 xy[[thread_position_in_grid]]){if(xy.x<b.get_width()&&xy.y<b.get_height())b.write(a.read(xy),xy);}";
    auto library=[c.device newLibraryWithSource:source options:nil error:&error];
    c.conversion=[c.device newComputePipelineStateWithFunction:[library newFunctionWithName:@"convert"] error:&error];
    if(!c.current||!c.depth||!c.motion||!c.output||!c.exposure||!c.conversion)return -6;
    p->context=nextContext++;upscaleContexts.emplace(p->context,c);return 0;
}}

static int32_t upscaleDispatch(void *raw) {@autoreleasepool{
    auto p=(FxUpscale*)raw;if(!p)return -2;p->gpuMilliseconds=p->wallMilliseconds=0;
    if(p->reset>1||p->depthReversed>1||!std::isfinite(p->jitterX)||!std::isfinite(p->jitterY)||
       !std::isfinite(p->motionScaleX)||!std::isfinite(p->motionScaleY)||
       !std::isfinite(p->preExposure)||p->preExposure<=0||
       !std::isfinite(p->exposure)||p->exposure<=0||p->exposure>65504)return -9;
    std::lock_guard<std::mutex> lock(guard);auto i=upscaleContexts.find(p->context);
    if(i==upscaleContexts.end())return -7;auto c=i->second;if(c.poisoned)return -8;
    if(c.history&&p->frameId<=c.lastFrame)return -9;
    for(const FxBuffer *b:{&p->color,&p->depth,&p->motion})if(b->width!=c.rw||b->height!=c.rh)return -10;
    if(p->output.width!=c.w||p->output.height!=c.h||p->depth.format!=FX_R32F||p->motion.format!=FX_RG16F||
       (p->color.format!=FX_RGBA8&&p->color.format!=FX_RGBA16F)||
       (p->output.format!=FX_RGBA8&&p->output.format!=FX_RGBA16F))return -10;
    double start=NSProcessInfo.processInfo.systemUptime;
    auto color=mappedTexture(c.device,p->color),depth=mappedTexture(c.device,p->depth),
         motion=mappedTexture(c.device,p->motion),out=mappedTexture(c.device,p->output);
    if(!color||!depth||!motion||!out)return -11;
    // All previous work for this context has completed before replacing exposure.
    _Float16 exposure=(_Float16)p->exposure;
    [c.exposure replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&exposure bytesPerRow:2];
    auto cb=[c.queue commandBuffer];cb.label=@"FH6 MetalFX staged temporal upscale";
    convert(c,cb,color,c.current);convert(c,cb,depth,c.depth);convert(c,cb,motion,c.motion);
    c.scaler.colorTexture=c.current;c.scaler.depthTexture=c.depth;c.scaler.motionTexture=c.motion;
    c.scaler.outputTexture=c.output;c.scaler.exposureTexture=c.exposure;
    c.scaler.inputContentWidth=c.rw;c.scaler.inputContentHeight=c.rh;
    c.scaler.jitterOffsetX=p->jitterX;c.scaler.jitterOffsetY=p->jitterY;
    c.scaler.motionVectorScaleX=p->motionScaleX;c.scaler.motionVectorScaleY=p->motionScaleY;
    c.scaler.preExposure=p->preExposure;c.scaler.depthReversed=p->depthReversed;
    c.scaler.reset=!c.history||p->reset||p->frameId!=c.lastFrame+1;
    [c.scaler encodeToCommandBuffer:cb];convert(c,cb,c.output,out);
    if(!finish(c,cb,@[color,depth,motion,out]))return -12;
    c.lastFrame=p->frameId;c.history=YES;
    p->gpuMilliseconds=1000*(cb.GPUEndTime-cb.GPUStartTime);
    p->wallMilliseconds=1000*(NSProcessInfo.processInfo.systemUptime-start);return 0;
}}
static int32_t upscaleDestroy(void *raw){
    auto p=(FxDestroy*)raw;if(!p)return -2;std::lock_guard<std::mutex> lock(guard);
    auto i=upscaleContexts.find(p->context);if(i==upscaleContexts.end())return -7;
    if(i->second.poisoned)return -8;upscaleContexts.erase(i);p->context=0;return 0;
}
