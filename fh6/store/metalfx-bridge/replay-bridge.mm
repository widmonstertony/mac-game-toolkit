// Saved FH6 pair through the actual mapped-buffer bridge, with camera metadata.
// Two frozen initialization samples are not continuous live gameplay history.
#include "bridge-native.mm"
#include "test-buffer.h"
#include <cstdio>

#include "replay-common.h"
int main(int argc,char **argv) {@autoreleasepool{
    if(argc<4||argc>5){fprintf(stderr,"usage: replay-bridge older-capture newer-capture output.rgba [jitter-zero-control|jitter-negate-control|jitter-output-grid-control|opaque-alpha-control|fullres-nearest-control|tagged-input-control|tagged-camera-omitted-control]\n");return 1;}
    const char *control=argc==5?argv[4]:"recorded";
    bool omitCamera=!strcmp(control,"tagged-camera-omitted-control");
    bool tagged=!strcmp(control,"tagged-input-control")||omitCamera;
    if(tagged)fprintf(stderr,"CONTROL: depth/motion captured at interpolation, not verified Prepare snapshots; frozen two-sample initialization, not continuous history\n");
    if(!tagged&&strcmp(control,"recorded")&&strcmp(control,"jitter-zero-control")&&strcmp(control,"jitter-negate-control")&&strcmp(control,"jitter-output-grid-control")&&strcmp(control,"opaque-alpha-control")&&strcmp(control,"fullres-nearest-control"))return 1;
    NSString *older=[NSString stringWithUTF8String:argv[1]],*newer=[NSString stringWithUTF8String:argv[2]];
    NSDictionary *oldMeta=metadata(older,tagged),*newMeta=metadata(newer,tagged);
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
        NSArray *names=tagged?@[@"scene",@"depth",@"motion"]:replayInputNames(i==2?newMeta:oldMeta);
        if(!copy(color,read(folder,[names[0] stringByAppendingString:@".bin"]),14080,w,h)||!copy(depth,read(folder,[names[1] stringByAppendingString:@".bin"]),9728,rw,rh)||
           !loadCamera(read(folder,@"native-frame.bin"),p))return 4;
        // Diagnostic controls only: do not change the production bridge ABI
        // or silently pick the apparently cleanest output as a validated fix.
        if(!strcmp(control,"jitter-zero-control"))p.frame.jitterX=p.frame.jitterY=0;
        if(!strcmp(control,"jitter-negate-control")){p.frame.jitterX=-p.frame.jitterX;p.frame.jitterY=-p.frame.jitterY;}
        if(!strcmp(control,"jitter-output-grid-control")){p.frame.jitterX*=float(w)/rw;p.frame.jitterY*=float(h)/rh;}
        if(omitCamera)p.camera.flags=0;
        if(!strcmp(control,"opaque-alpha-control"))for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)color.row<uint8_t>(y)[x*4+3]=255;
        if(i==2){if(!copy(motion,read(folder,[names[2] stringByAppendingString:@".bin"]),9472,rw,rh))return 4;}
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
