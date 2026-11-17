// Headless real D3DMetal GPU test; reuses the same scene/depth/motion resources
// over eight consecutive frames and checks every stored pixel's frame identity.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <cstdio>
#include <cstring>
#include "capture.h"
static constexpr UINT W=192,H=128,RW=96,RH=64;
#include "../../../rosetta/fg-chain/probe-gpu.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d condition=%s\n",__LINE__,#x);return 1;}}while(0)
static constexpr auto Read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);GPU g;CHECK(g.init());
 HMODULE module{};auto method=(*(void***)g.d)[0];
 CHECK(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(char*)method,&module));
 CHECK(module==GetModuleHandleW(L"apd12.dll"));puts("PASS actual D3DMetal device apd12.dll");
 Texture depth,motion,scene,output[3];CHECK(depth.init(g.d,DXGI_FORMAT_R32G8X24_TYPELESS,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,RW,RH));
 CHECK(depth.rowBytes==RW*4);CHECK(motion.init(g.d,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,RW,RH));
 CHECK(scene.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM));
 for(auto &o:output)CHECK(o.init(g.d,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
 SequenceCapture recorder;CHECK(recorder.arm());CHECK(!recorder.arm());
 BYTE c[W*H*4],m[RW*RH*4];float z[RW*RH];
 for(unsigned n=0;n<11;++n){
  const uint64_t id=100+n;auto &out=output[n%3];
  CHECK(recorder.retire(g.q,out.gpu,id));
  for(unsigned i=0;i<W*H;++i){c[i*4]=BYTE(n+10);c[i*4+1]=BYTE(i%251);c[i*4+2]=BYTE(n*3);c[i*4+3]=255;}
  for(unsigned i=0;i<RW*RH;++i){z[i]=float(n+1)/16;uint16_t half[2]={uint16_t(0x3c00+n),uint16_t(0xbc00+n)};memcpy(m+i*4,half,4);}
  CHECK(g.begin());CHECK(depth.write(g.l,(BYTE*)z,RW*4,Read));CHECK(motion.write(g.l,m,RW*4,Read));
  ChainFrame f{};f.frame=id;f.hasConstants=1;f.inverted=f.cameraMotionIncluded=1;f.scale[0]=.5f;f.scale[1]=-.5f;f.fov=1.05f;
  auto captured=recorder.prepare(g.d,g.l,f,16.667f,depth.gpu,motion.gpu,Read,Read,W,H);
  CHECK(bool(captured)==(n<8));
  CHECK(scene.write(g.l,c,W*4,Read));CHECK(out.write(g.l,c,W*4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
  CHECK(recorder.record(g.l,f,scene.gpu,out.gpu,Read,D3D12_RESOURCE_STATE_UNORDERED_ACCESS)==(n<8));
  if(n==0){CHECK(!recorder.pollComplete());CHECK(!recorder.clearCompleted());}
  CHECK(g.end());
 }
 CHECK(recorder.count==8&&!recorder.armed&&!recorder.aborted);
 CHECK(recorder.pollComplete());uint64_t checked=0;
 for(unsigned n=0;n<8;++n){
  auto &f=recorder.frames[n];CHECK(f.id==100+n&&f.stage==SequenceCapture::Complete);
  BYTE *data=nullptr;D3D12_RANGE range{0,(SIZE_T)f.bytes};CHECK(SUCCEEDED(f.readback->Map(0,&range,(void**)&data)));
  for(unsigned k=0;k<4;++k){auto &im=f.image[k];
   for(unsigned y=0;y<im.rows;++y)for(unsigned x=0;x<im.footprint.Footprint.Width;++x){
    auto pixel=data+im.footprint.Offset+y*im.footprint.Footprint.RowPitch+x*4;
    if(k==0){float d;memcpy(&d,pixel,4);CHECK(d==float(n+1)/16);}
    else if(k==1){uint16_t v[2];memcpy(v,pixel,4);CHECK(v[0]==0x3c00+n&&v[1]==0xbc00+n);}
    else {unsigned i=y*W+x;CHECK(pixel[0]==n+10&&pixel[1]==i%251&&pixel[2]==n*3&&pixel[3]==255);}
    ++checked;
   }
  }
  D3D12_RANGE empty{};f.readback->Unmap(0,&empty);
 }
 printf("PASS eight consecutive Prepare/color/output snapshots pixels=%llu bytes=%llu reused_outputs=3\n",checked,recorder.allocated);
 CHECK(recorder.clearCompleted());CHECK(recorder.arm());
 // A frame gap aborts the sequence instead of pretending it is continuous.
 CHECK(g.begin());ChainFrame f{};f.frame=200;f.hasConstants=1;
 CHECK(recorder.prepare(g.d,g.l,f,16,depth.gpu,motion.gpu,Read,Read,W,H));
 f.frame=202;CHECK(!recorder.prepare(g.d,g.l,f,16,depth.gpu,motion.gpu,Read,Read,W,H));
 CHECK(recorder.aborted&&!recorder.armed);CHECK(!recorder.clearCompleted());
 CHECK(g.end());CHECK(recorder.drainSubmitted(g.q));
 auto until=GetTickCount64()+3000;while(!recorder.pollComplete()&&GetTickCount64()<until)SwitchToThread();
 CHECK(recorder.pollComplete());CHECK(recorder.clearCompleted());
 puts("PASS gap abort and partial-copy lifetime retained until exact producer fence completed");
 return 0;
}
