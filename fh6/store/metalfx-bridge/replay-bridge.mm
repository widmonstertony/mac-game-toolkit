// Saved FH6 pair through the actual mapped-buffer bridge, with camera metadata.
// Two frozen initialization samples are not continuous live gameplay history.
#include "bridge-native.mm"
#include "test-buffer.h"
#include <cstdio>

static NSData *read(NSString *folder,NSString *name) {
    return [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:name]];
}
static NSDictionary *metadata(NSString *folder) {
    NSData *data=read(folder,@"capture.json");if(!data)return nil;
    id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if(![object isKindOfClass:NSDictionary.class])return nil;
    NSDictionary *m=object;uint64_t frame=[m[@"frame"] unsignedLongLongValue];
    if(!frame||![m[@"gpu_fence_completed"] boolValue]||![m[@"pixel_files_complete"] boolValue]||
       ![m[@"prepare"][@"known"] boolValue]||[m[@"prepare"][@"frame"] unsignedLongLongValue]!=frame)return nil;
    NSData *native=read(folder,@"native-frame.bin");if(native.length!=608)return nil;
    uint64_t nativeFrame=0;memcpy(&nativeFrame,native.bytes,8);if(nativeFrame!=frame)return nil;
    for(NSString *name in @[@"scene",@"prepare-current-depth",@"prepare-current-motion"]) {
        bool found=false;for(NSDictionary *image in m[@"images"])if([image[@"name"] isEqual:name]) {
            if([image[@"source_frame"] unsignedLongLongValue]!=frame)return nil;
            found=true;break;
        }
        if(!found)return nil;
    }
    return m;
}
static bool copy(OwnedBuffer &buffer,NSData *data,size_t sourcePitch,unsigned sourceW,unsigned sourceH) {
    if(data.length!=sourcePitch*sourceH||sourcePitch<sourceW*4)return false;
    for(unsigned y=0;y<buffer.b.height;++y){
        auto src=(const uint8_t*)data.bytes+(uint64_t(y)*2+1)*sourceH/(buffer.b.height*2)*sourcePitch;
        if(buffer.b.width==sourceW)memcpy(buffer.row<uint8_t>(y),src,buffer.b.width*4);
        else for(unsigned x=0;x<buffer.b.width;++x)memcpy(buffer.row<uint8_t>(y)+x*4,src+(uint64_t(x)*2+1)*sourceW/(buffer.b.width*2)*4,4);
    }
    return true;
}
static bool loadCamera(NSData *native,FxDispatchCamera &p) {
    if(native.length!=608)return false;
    float camera[8],basis[12];const auto b=(const uint8_t*)native.bytes;
    memcpy(camera,b+36,sizeof(camera));memcpy(basis,b+220,sizeof(basis));
    auto &f=p.frame;f.jitterX=camera[0];f.jitterY=camera[1];
    f.motionScaleX=camera[2]*f.color.width;f.motionScaleY=camera[3]*f.color.height;
    f.nearPlane=camera[4];f.farPlane=camera[5];f.fovDegrees=camera[6]*180/M_PI;f.aspectRatio=camera[7];
    simd_float3 position={basis[0],basis[1],basis[2]},up={basis[3],basis[4],basis[5]},
        right={basis[6],basis[7],basis[8]},forward={basis[9],basis[10],basis[11]};
    simd_float4x4 view;
    view.columns[0]={right.x,up.x,forward.x,0};view.columns[1]={right.y,up.y,forward.y,0};
    view.columns[2]={right.z,up.z,forward.z,0};
    view.columns[3]={-simd_dot(right,position),-simd_dot(up,position),-simd_dot(forward,position),1};
    memcpy(p.camera.worldToView,&view,sizeof(view));
    // Streamline row-vector rows become column-vector SIMD columns: transpose
    // mathematically, with the same contiguous storage values.
    memcpy(p.camera.viewToClip,b+156,sizeof(p.camera.viewToClip));p.camera.flags=1;
    return true;
}
int main(int argc,char **argv) {@autoreleasepool{
    if(argc<4||argc>5){fprintf(stderr,"usage: replay-bridge older-capture newer-capture output.rgba [jitter-zero-control|jitter-negate-control|jitter-output-grid-control|opaque-alpha-control|fullres-nearest-control]\n");return 1;}
    const char *control=argc==5?argv[4]:"recorded";
    if(strcmp(control,"recorded")&&strcmp(control,"jitter-zero-control")&&strcmp(control,"jitter-negate-control")&&strcmp(control,"jitter-output-grid-control")&&strcmp(control,"opaque-alpha-control")&&strcmp(control,"fullres-nearest-control"))return 1;
    NSString *older=[NSString stringWithUTF8String:argv[1]],*newer=[NSString stringWithUTF8String:argv[2]];
    NSDictionary *oldMeta=metadata(older),*newMeta=metadata(newer);
    if(!oldMeta||!newMeta||[oldMeta[@"frame"] unsignedLongLongValue]+1!=[newMeta[@"frame"] unsignedLongLongValue])return 2;
    constexpr unsigned w=3472,h=2266,rw=2314,rh=1510;
    const bool fullres=!strcmp(control,"fullres-nearest-control");
    const unsigned dw=fullres?w:rw,dh=fullres?h:rh;
    FxCreate c{1,dw,dh,w,h,0,0};if(create(&c))return 3;
    OwnedBuffer color(w,h,FX_RGBA8),depth(dw,dh,FX_R32F),motion(dw,dh,FX_RG16F),output(w,h,FX_RGBA8);
    FxDispatchCamera p{};p.frame.context=c.context;p.frame.color=color.b;p.frame.depth=depth.b;
    p.frame.motion=motion.b;p.frame.output=output.b;p.frame.depthReversed=1;
    p.frame.deltaSeconds=[newMeta[@"prepare"][@"milliseconds"] floatValue]*.001f;
    for(unsigned i=0;i<3;++i) {
        NSString *folder=i==2?newer:older;
        if(!copy(color,read(folder,@"scene.bin"),14080,w,h)||!copy(depth,read(folder,@"prepare-current-depth.bin"),9728,rw,rh)||
           !loadCamera(read(folder,@"native-frame.bin"),p))return 4;
        // Diagnostic controls only: do not change the production bridge ABI
        // or silently pick the apparently cleanest output as a validated fix.
        if(!strcmp(control,"jitter-zero-control"))p.frame.jitterX=p.frame.jitterY=0;
        if(!strcmp(control,"jitter-negate-control")){p.frame.jitterX=-p.frame.jitterX;p.frame.jitterY=-p.frame.jitterY;}
        if(!strcmp(control,"jitter-output-grid-control")){p.frame.jitterX*=float(w)/rw;p.frame.jitterY*=float(h)/rh;}
        if(!strcmp(control,"opaque-alpha-control"))for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)color.row<uint8_t>(y)[x*4+3]=255;
        if(i==2){if(!copy(motion,read(folder,@"prepare-current-motion.bin"),9472,rw,rh))return 4;}
        else memset((void*)motion.b.address,0,motion.b.bytes);
        p.frame.frameId=100+i;p.frame.reset=i==0;
        int result=dispatchCamera(&p);
        printf("control=%s step=%u result=%d generated=%u jitter=%.5f,%.5f gpu_ms=%.3f\n",control,i,result,p.frame.generated,p.frame.jitterX,p.frame.jitterY,p.frame.gpuMilliseconds);
        if(result||p.frame.generated!=(i==2))return 5;
    }
    FILE *file=fopen(argv[3],"wb");if(!file)return 6;
    bool ok=true;for(unsigned y=0;y<h;++y)ok=(fwrite(output.row<uint8_t>(y),1,w*4,file)==w*4)&&ok;
    ok=(fclose(file)==0)&&ok;FxDestroy d{c.context};if(destroy(&d))return 7;
    return ok?0:6;
}}
