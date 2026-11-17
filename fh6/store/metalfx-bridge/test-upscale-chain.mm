// Real MetalFX temporal SR -> FG pixel oracle. No synthetic repeated warmup.
#include "bridge-native.mm"
#include "test-buffer.h"
#include <cstdio>
static constexpr unsigned rw=128,rh=96,w=192,h=144;
static bool measure(OwnedBuffer &b,float expected,const char *name,unsigned frame){
    double sum=0;unsigned count=0,outside=0;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        float r,g,blue;
        if(b.b.format==FX_RGBA16F){auto p=b.row<_Float16>(y)+x*4;r=p[0];g=p[1];blue=p[2];
            if(!std::isfinite(r)||!std::isfinite(g)||!std::isfinite(blue))return false;
        }else{auto p=b.row<uint8_t>(y)+x*4;r=p[0]/255.f;g=p[1]/255.f;blue=p[2]/255.f;}
        if(r>.55f&&g<.4f&&blue<.4f){sum+=x;++count;
            if(fabsf(x-expected)>25.5f||y<34||y>86)++outside;}
    }
    double center=count?sum/count:-1;
    printf("%s frame=%u pixels=%u centroid=%.3f expected=%.3f outside=%u\n",name,frame,count,center,expected,outside);
    return count>=2100&&count<=2500&&fabs(center-expected)<1.5&&outside==0;
}
int main(){@autoreleasepool{
    FxCreate sr{1,rw,rh,w,h,0,0},fg{1,rw,rh,w,h,0,0};
    if(upscaleCreate(&sr)||create(&fg))return 1;
    OwnedBuffer color(rw,rh,FX_RGBA16F),depth(rw,rh,FX_R32F),motion(rw,rh,FX_RG16F),
        scaled(w,h,FX_RGBA16F),generated(w,h,FX_RGBA8);
    FxUpscale u{};u.context=sr.context;u.color=color.b;u.depth=depth.b;u.motion=motion.b;u.output=scaled.b;
    u.motionScaleX=u.motionScaleY=1;u.preExposure=u.exposure=1;u.depthReversed=1;
    FxDispatch f{};f.context=fg.context;f.color=scaled.b;f.depth=depth.b;f.motion=motion.b;f.output=generated.b;
    f.motionScaleX=float(w)/rw;f.motionScaleY=float(h)/rh;f.nearPlane=.1f;f.farPlane=100;
    f.fovDegrees=60;f.aspectRatio=float(w)/h;f.deltaSeconds=1.f/30;f.depthReversed=1;
    unsigned failures=0,midpoints=0;
    for(unsigned n=0;n<12;++n){
        unsigned left=24+n*2;
        for(unsigned y=0;y<rh;++y)for(unsigned x=0;x<rw;++x){
            bool square=x>=left&&x<left+32&&y>=24&&y<56;
            auto c=color.row<_Float16>(y)+x*4;c[0]=square?.85f:.15f;c[1]=c[2]=square?.05f:.15f;c[3]=1;
            depth.row<float>(y)[x]=square?.9f:.1f;
            auto m=motion.row<_Float16>(y)+x*2;m[0]=square?-2:0;m[1]=0;
        }
        u.frameId=f.frameId=n+1;u.reset=f.reset=n==0;
        if(upscaleDispatch(&u))return 2;
        float center=left*1.5f+23.5f;
        failures+=!measure(scaled,center,"SR",n);
        if(dispatch(&f)||f.generated!=(n>=2))return 3;
        failures+=!measure(generated,center-(n>=2?1.5f:0),"SR+FG",n);
        midpoints+=f.generated;
        // A duplicate and invalid exposure must not advance history or alter output.
        if(n==4){if(upscaleDispatch(&u)!=-9)return 4;
            u.frameId=6;u.exposure=0;if(upscaleDispatch(&u)!=-9)return 4;u.frameId=5;u.exposure=1;}
    }
    // SR gaps reset accumulation, as an independent fresh-context comparison.
    FxCreate fresh{1,rw,rh,w,h,0,0};if(upscaleCreate(&fresh))return 5;
    OwnedBuffer reference(w,h,FX_RGBA16F);auto v=u;v.context=fresh.context;v.output=reference.b;v.frameId=30;v.reset=1;
    u.frameId=30;u.reset=0;
    if(upscaleDispatch(&v)||upscaleDispatch(&u))return 5;
    for(unsigned y=0;y<h;++y)if(memcmp(reference.row<uint8_t>(y),scaled.row<uint8_t>(y),w*8))return 6;
    FxDestroy ds{sr.context},df{fg.context},dr{fresh.context};
    if(upscaleDestroy(&ds)||destroy(&df)||upscaleDestroy(&dr))return 7;
    printf("%s 12 temporal SR outputs, %u generated midpoints, failures=%u; gap reset matched fresh context\n",failures?"FAIL":"PASS",midpoints,failures);
    return failures?8:0;
}}
