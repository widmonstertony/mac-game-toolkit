// Real GPU quality diagnostic: stationary foreground over a translating background.
// No game resources, screen capture, or production configuration are accessed.
#include "bridge-native.mm"
#include "test-buffer.h"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc,char **argv) {@autoreleasepool{
    constexpr unsigned w=512,h=384,left=176,right=336,top=150,bottom=250;
    const bool zero=argc>1 && !strcmp(argv[1],"zero-motion-control");
    const bool lowres=argc>1 && !strncmp(argv[1],"perspective-lowres",18);
    const bool perspective=lowres||(argc>1 && !strcmp(argv[1],"perspective"));
    const bool quality=lowres&&strstr(argv[1],"-quality");
    const unsigned rw=quality?342:lowres?w/2:w,rh=quality?256:lowres?h/2:h;
    constexpr float nearZ=.1f,farZ=1000.f,cameraHeight=1.4f,travel=2.6f;
    const float focal=h/(2*std::tan(M_PI/6));
    const auto roadGray=[&](unsigned x,unsigned y,float frame) {
        if(y<=h/2)return 64u;
        const float z=cameraHeight*focal/(float(y)+.5f-h*.5f);
        const float worldX=(float(x)+.5f-w*.5f)*z/focal;
        const float worldZ=z+frame*travel;
        const int tile=int(std::floor(worldX/.8f))+int(std::floor(worldZ/.8f));
        return 48u+unsigned((tile&1)*80);
    };
    FxCreate c{1,rw,rh,w,h,0,0};if(create(&c))return 2;
    OwnedBuffer color(w,h,FX_RGBA8),depth(rw,rh,FX_R32F),motion(rw,rh,FX_RG16F),output(w,h,FX_RGBA8);
    FxDispatch p{};p.context=c.context;p.color=color.b;p.depth=depth.b;p.motion=motion.b;p.output=output.b;
    p.motionScaleX=p.motionScaleY=1;p.nearPlane=.1;p.farPlane=1000;p.fovDegrees=60;
    // Deliberately cancel the bridge correction for a failing negative control.
    const bool uncorrected=lowres&&strstr(argv[1],"-uncorrected");
    if(uncorrected){p.motionScaleX=float(w)/rw;p.motionScaleY=float(h)/rh;}
    p.aspectRatio=float(w)/h;p.deltaSeconds=1.f/60;p.depthReversed=1;
    size_t totalGhost=0,maxGhost=0,badMidpoints=0;
    for(unsigned frame=0;frame<16;++frame) {
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            bool front=x>=left&&x<right&&y>=top&&y<bottom;
            const unsigned gray=perspective?roadGray(x,y,frame):48+(((x/32+(y+1024-frame*16)/32)&1)*80);
            auto rgba=color.row<uint8_t>(y)+x*4;
            rgba[0]=front?240:gray;rgba[1]=rgba[2]=front?8:gray;rgba[3]=255;
        }
        for(unsigned y=0;y<rh;++y)for(unsigned x=0;x<rw;++x) {
            const float px=(x+.5f)*w/rw,py=(y+.5f)*h/rh;
            const bool front=px>=left&&px<right&&py>=top&&py<bottom;
            depth.row<float>(y)[x]=front?.04f:.02f;
            auto mv=motion.row<_Float16>(y)+x*2;mv[0]=0;mv[1]=front||zero?0:-16;
            if(perspective&&!front) {
                if(py<=h/2){depth.row<float>(y)[x]=0;mv[1]=0;}
                else {
                    const float z=cameraHeight*focal/(py-h*.5f);
                    // Exact finite reversed-Z projection and backward screen
                    // motion for a camera advancing along its view direction.
                    depth.row<float>(y)[x]=(nearZ*farZ/z-nearZ)/(farZ-nearZ);
                    mv[0]=-(px-w*.5f)*travel/(z+travel);
                    mv[1]=-(py-h*.5f)*travel/(z+travel);
                }
            }
        }
        p.frameId=frame+1;p.reset=frame==0;
        if(dispatch(&p))return 3;
        size_t ghost=0,inside=0,tail=0,changed=0,samples=0;
        double midpointError=0,currentError=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const auto rgba=output.row<uint8_t>(y)+x*4;
            if(rgba[0]>rgba[1]+48 && rgba[0]>rgba[2]+48) {
                if(x>=left&&x<right&&y>=top&&y<bottom)++inside;
                else {++ghost;if(y>=bottom&&y<bottom+40)++tail;}
            }
            // Exclude occluded areas and viewport boundaries for this separate
            // midpoint check. A duplicate current frame must not pass as FG.
            if(y>24&&y<h-24&&(x<left-24||x>=right+24)) {
                const unsigned midpoint=perspective?roadGray(x,y,float(frame)-.5f):48+(((x/32+(y+1024-frame*16+8)/32)&1)*80);
                const unsigned now=color.row<uint8_t>(y)[x*4];
                midpointError+=std::abs(int(rgba[0])-int(midpoint));
                currentError+=std::abs(int(rgba[0])-int(now));
                changed+=rgba[0]!=now;++samples;
            }
        }
        printf("frame=%u generated=%u foreground=%zu outside_foreground=%zu rear_band=%zu changed=%zu midpoint_mae=%.4f current_mae=%.4f\n",frame,p.generated,inside,ghost,tail,changed,midpointError/samples,currentError/samples);
        if(!zero&&p.generated&&(!changed||midpointError>=currentError))++badMidpoints;
        if(p.generated){totalGhost+=ghost;maxGhost=std::max(maxGhost,ghost);}
    }
    printf("mode=%s motion_scale=%.3f total_outside=%zu max_outside=%zu bad_midpoints=%zu (quality observation, not live acceptance)\n",lowres?"perspective-lowres":perspective?"perspective":zero?"zero-motion-control":"correct-layer-motion",p.motionScaleX,totalGhost,maxGhost,badMidpoints);
    if(argc>2) {
        FILE *f=fopen(argv[2],"wb");if(!f)return 4;
        fprintf(f,"P6\n%u %u\n255\n",w,h);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)if(fwrite(output.row<uint8_t>(y)+x*4,1,3,f)!=3){fclose(f);return 4;}
        if(fclose(f))return 4;
    }
    FxDestroy d{c.context};return destroy(&d)?5:(badMidpoints||maxGhost)?6:0;
}}
