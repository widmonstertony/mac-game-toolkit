// Real GPU quality diagnostic: stationary foreground over a translating background.
// No game resources, screen capture, or production configuration are accessed.
#include "bridge-native.mm"
#include "test-buffer.h"
#include <cstdio>
#include <string>
#include <vector>
#ifndef FX_TEST_WIDTH
#define FX_TEST_WIDTH 512
#define FX_TEST_HEIGHT 384
#define FX_QUALITY_WIDTH 342
#define FX_QUALITY_HEIGHT 256
#endif

int main(int argc,char **argv) {@autoreleasepool{
    constexpr unsigned w=FX_TEST_WIDTH,h=FX_TEST_HEIGHT,left=176*w/512,right=336*w/512,top=150*h/384,bottom=250*h/384;
    const bool zero=argc>1 && !strcmp(argv[1],"zero-motion-control");
    const bool lowres=argc>1 && !strncmp(argv[1],"perspective-lowres",18);
    const bool perspective=argc>1 && !strncmp(argv[1],"perspective",11);
    const bool cameraMatrices=perspective&&strstr(argv[1],"-camera");
    const bool detailed=perspective&&strstr(argv[1],"-detailed");
    const bool quality=lowres&&strstr(argv[1],"-quality");
    const unsigned rw=quality?FX_QUALITY_WIDTH:lowres?w/2:w,rh=quality?FX_QUALITY_HEIGHT:lowres?h/2:h;
    constexpr float nearZ=.1f,farZ=1000.f,cameraHeight=1.4f,travel=2.6f;
    const float focal=h/(2*std::tan(M_PI/6));
    const auto foreground=[&](float x,float y){
        if(!detailed)return x>=left&&x<right&&y>=top&&y<bottom;
        x*=512.f/w;y*=384.f/h;
        const float halfWidth=48+std::min(32.f,std::max(0.f,(y-150)*.8f));
        bool body=y>=150&&y<250&&std::abs(x-256)<halfWidth;
        bool mirrors=y>=165&&y<173&&((x>=164&&x<180)||(x>=332&&x<348));
        bool spoiler=y>=190&&y<194&&x>=160&&x<352;
        bool exhaust=y>=250&&y<258&&((x>=196&&x<206)||(x>=306&&x<316));
        return body||mirrors||spoiler||exhaust;
    };
    const auto red=[&](unsigned x,unsigned y){return detailed?unsigned(100+((((x*512/w)/5)^((y*384/h)/7))&3)*44):240u;};
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
    FxDispatchCamera cp{};auto &p=cp.frame;p.context=c.context;p.color=color.b;p.depth=depth.b;p.motion=motion.b;p.output=output.b;
    p.motionScaleX=p.motionScaleY=1;p.nearPlane=.1;p.farPlane=1000;p.fovDegrees=60;
    // Deliberately cancel the bridge correction for a failing negative control.
    const bool uncorrected=lowres&&strstr(argv[1],"-uncorrected");
    if(uncorrected){p.motionScaleX=float(w)/rw;p.motionScaleY=float(h)/rh;}
    p.aspectRatio=float(w)/h;p.deltaSeconds=1.f/60;p.depthReversed=1;
    if(cameraMatrices){
        cp.camera.flags=1;for(unsigned i=0;i<4;++i)cp.camera.worldToView[i*5]=1;
        const float f=1/std::tan(M_PI/6);
        cp.camera.viewToClip[0]=f/p.aspectRatio;cp.camera.viewToClip[5]=f;
        cp.camera.viewToClip[10]=nearZ/(nearZ-farZ);cp.camera.viewToClip[11]=1;
        cp.camera.viewToClip[14]=nearZ*farZ/(farZ-nearZ);
    }
    size_t totalGhost=0,maxGhost=0,badMidpoints=0,minInside=SIZE_MAX;
    for(unsigned frame=0;frame<16;++frame) {
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            bool front=foreground(x+.5f,y+.5f);
            const unsigned gray=perspective?roadGray(x,y,frame):48+(((x/32+(y+1024-frame*16)/32)&1)*80);
            auto rgba=color.row<uint8_t>(y)+x*4;
            rgba[0]=front?red(x,y):gray;rgba[1]=rgba[2]=front?8:gray;rgba[3]=255;
        }
        for(unsigned y=0;y<rh;++y)for(unsigned x=0;x<rw;++x) {
            const float px=(x+.5f)*w/rw,py=(y+.5f)*h/rh;
            const bool front=foreground(px,py);
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
        cp.camera.worldToView[13]=-cameraHeight;cp.camera.worldToView[14]=-float(frame)*travel;
        if(cameraMatrices?dispatchCamera(&cp):dispatch(&p))return 3;
        size_t ghost=0,inside=0,tail=0,changed=0,samples=0;
        double midpointError=0,currentError=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const auto rgba=output.row<uint8_t>(y)+x*4;
            if(rgba[0]>rgba[1]+48 && rgba[0]>rgba[2]+48) {
                if(foreground(x+.5f,y+.5f))++inside;
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
        if(p.generated){totalGhost+=ghost;maxGhost=std::max(maxGhost,ghost);minInside=std::min(minInside,inside);}
    }
    printf("mode=%s camera_matrices=%d motion_scale=%.3f total_outside=%zu max_outside=%zu min_inside=%zu bad_midpoints=%zu (quality observation, not live acceptance)\n",argc>1?argv[1]:"correct-layer-motion",cameraMatrices,p.motionScaleX,totalGhost,maxGhost,minInside,badMidpoints);
    if(argc>2) {
        FILE *f=fopen(argv[2],"wb");if(!f)return 4;
        fprintf(f,"P6\n%u %u\n255\n",w,h);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)if(fwrite(output.row<uint8_t>(y)+x*4,1,3,f)!=3){fclose(f);return 4;}
        if(fclose(f))return 4;
    }
    FxDestroy d{c.context};return destroy(&d)?5:(badMidpoints||maxGhost)?6:0;
}}
