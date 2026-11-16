// Standalone real-GPU contract test. Does not load or modify a game.
#include "bridge-native.mm"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)

#include "test-buffer.h"

int main(){@autoreleasepool{
    constexpr uint32_t w=192,h=128;
    uint64_t supported=0;REQUIRE(caps(&supported)==0 && (supported&2));
    FxCreate c{1,w,h,w,h,0,0};REQUIRE(create(&c)==0 && c.context);
    OwnedBuffer color(w,h,FX_RGBA8),depth(w,h,FX_R32F),motion(w,h,FX_RG16F),output(w,h,FX_RGBA8);
    FxDispatch p{};p.context=c.context;p.color=color.b;p.depth=depth.b;p.motion=motion.b;p.output=output.b;
    p.motionScaleX=p.motionScaleY=1;p.nearPlane=.1f;p.farPlane=1000;p.fovDegrees=60;
    p.aspectRatio=float(w)/h;p.deltaSeconds=1.f/60;p.depthReversed=1;
    for(unsigned frame=0;frame<12;++frame){
        const unsigned left=30+frame*4;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            bool foreground=x>=left&&x<left+32&&y>=48&&y<80;
            auto rgba=color.row<uint8_t>(y)+x*4;
            rgba[0]=rgba[1]=rgba[2]=foreground?255:8;rgba[3]=255;
            depth.row<float>(y)[x]=foreground?.9f:.1f;
            auto mv=motion.row<_Float16>(y)+x*2;mv[0]=foreground?-4.f:0;mv[1]=0;
        }
        p.frameId=frame+100;p.reset=frame==0;REQUIRE(dispatch(&p)==0);
        REQUIRE(p.generated==(frame>=2));
        size_t count=0,sum=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
            if(output.row<uint8_t>(y)[x*4]>128){++count;sum+=x;}
        const double centroid=count?double(sum)/count:-1;
        printf("frame=%u generated=%u foreground=%zu centroid=%.3f expected=%.3f gpu_ms=%.3f\n",
            frame,p.generated,count,centroid,left+(frame>=2?13.5:15.5),p.gpuMilliseconds);
        REQUIRE(count==1024);
        REQUIRE(std::abs(centroid-(left+(frame>=2?13.5:15.5)))<.1);
    }
    REQUIRE(dispatch(&p)==-9 && !p.generated); // Duplicate ID must not consume history.
    p.frameId+=2;p.reset=0;REQUIRE(dispatch(&p)==0 && !p.generated); // Gap resets.
    ++p.frameId;p.reset=1;REQUIRE(dispatch(&p)==0 && !p.generated);
    ++p.frameId;p.reset=0;p.depth.width=w-1;REQUIRE(dispatch(&p)==-10 && !p.generated);
    p.depth.width=w;REQUIRE(dispatch(&p)==0 && !p.generated); // Rejected input didn't advance.
    ++p.frameId;REQUIRE(dispatch(&p)==0 && p.generated);
    if(supported&4) {
        FxDispatchCamera cp{};cp.frame=p;++cp.frame.frameId;cp.camera.flags=1;
        for(unsigned i=0;i<4;++i)cp.camera.worldToView[i*5]=1;
        const float f=1/std::tan(float(M_PI)/6);
        cp.camera.viewToClip[0]=f/p.aspectRatio;cp.camera.viewToClip[5]=f;
        cp.camera.viewToClip[10]=p.nearPlane/(p.nearPlane-p.farPlane);
        cp.camera.viewToClip[11]=1;
        cp.camera.viewToClip[14]=p.nearPlane*p.farPlane/(p.farPlane-p.nearPlane);
        const float saved=cp.camera.viewToClip[0];cp.camera.viewToClip[0]=NAN;
        REQUIRE(dispatchCamera(&cp)==-13&&!cp.frame.generated);
        cp.camera.viewToClip[0]=saved;cp.camera.reserved=1;
        REQUIRE(dispatchCamera(&cp)==-13&&!cp.frame.generated);
        cp.camera.reserved=0;
        // Camera-presence changes reset history. Rejected metadata did not
        // consume the frame ID; the same ID must still be accepted here.
        REQUIRE(dispatchCamera(&cp)==0&&!cp.frame.generated);
        ++cp.frame.frameId;REQUIRE(dispatchCamera(&cp)==0&&!cp.frame.generated);
        ++cp.frame.frameId;REQUIRE(dispatchCamera(&cp)==0&&cp.frame.generated);
        p=cp.frame;++p.frameId;REQUIRE(dispatch(&p)==0&&!p.generated);
        puts("PASS camera ABI validation, rejected metadata, presence reset and legacy dispatch");
    }
    FxDestroy d{c.context};REQUIRE(destroy(&d)==0 && !d.context);
    REQUIRE(dispatch(&p)==-7 && !p.generated);
    puts("PASS real GPU midpoint, mapped buffers, frame gaps, explicit reset, rejected input and destroy");
    return 0;
}}
