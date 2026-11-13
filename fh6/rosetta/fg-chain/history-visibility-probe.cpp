// Owned standalone GPU visibility experiment. Never installed into a game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static const UINT W=128,H=96;
#include "probe-gpu.h"
#pragma pack(push,1)
struct Header {UINT64 magic;UINT version,size,w,h,rw,rh,fieldPitch,maskPitch;UINT64 maskBytes;UINT depthPitch[2],motionPitch[2];float matrix[16];};
#pragma pack(pop)
static_assert(sizeof(Header)==128,"visibility ABI");
static BYTE *readFile(const wchar_t *dir,const wchar_t *name,UINT64 &size){
 wchar_t path[32768];if(wcslen(dir)+wcslen(name)+2>=32768)return nullptr;swprintf(path,32768,L"%ls\\%ls",dir,name);
 HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return nullptr;LARGE_INTEGER n{};if(!GetFileSizeEx(f,&n)||n.QuadPart<=0||n.QuadPart>128ll*1024*1024){CloseHandle(f);return nullptr;}
 size=n.QuadPart;auto data=(BYTE*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)size);DWORD done;bool ok=data&&ReadFile(f,data,(DWORD)size,&done,nullptr)&&done==size;CloseHandle(f);return ok?data:nullptr;
}
static BYTE *tight(const wchar_t *dir,const wchar_t *name,UINT w,UINT h,UINT pitch,UINT stride){UINT64 bytes{};BYTE *raw=readFile(dir,name,bytes);if(!raw||pitch<w*stride||bytes<UINT64(h-1)*pitch+w*stride)return nullptr;auto out=(BYTE*)HeapAlloc(GetProcessHeap(),0,SIZE_T(w)*h*stride);if(out)for(UINT y=0;y<h;y++)memcpy(out+SIZE_T(y)*w*stride,raw+SIZE_T(y)*pitch,w*stride);HeapFree(GetProcessHeap(),0,raw);return out;}
int wmain(int argc,wchar_t **argv){
 setvbuf(stdout,nullptr,_IONBF,0);CHECK(argc==4);UINT64 bytes{};auto header=readFile(argv[1],L"visibility-input.bin",bytes);CHECK(header&&bytes==sizeof(Header));Header h;memcpy(&h,header,sizeof(h));CHECK(h.magic==0x3153495648364846ull&&h.version==1&&h.size==128&&h.w<=8192&&h.h<=8192&&h.rw>4&&h.rh>4&&h.rw<=h.w&&h.rh<=h.h);
 GPU g;CHECK(g.init());Texture t[7];
 const DXGI_FORMAT format[]={DXGI_FORMAT_R32_UINT,DXGI_FORMAT_R32_UINT,DXGI_FORMAT_R32G8X24_TYPELESS,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32G8X24_TYPELESS,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8G8_UNORM};
 const wchar_t *names[]={L"field-x.bin",L"field-y.bin",L"previous-depth.bin",L"previous-motion.bin",L"depth.bin",L"motion.bin",L"mask.bin"};
 UINT pitches[]={h.fieldPitch,h.fieldPitch,h.depthPitch[0],h.motionPitch[0],h.depthPitch[1],h.motionPitch[1],h.maskPitch};
 CHECK(g.begin());
 for(UINT i=0;i<7;i++){bool low=i>=2&&i<=5;UINT w=low?h.rw:h.w,hh=low?h.rh:h.h,stride=i==6?2:4;auto flags=i==6?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:(i==2||i==4)?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_NONE;CHECK(t[i].init(g.d,format[i],flags,w,hh));CHECK(t[i].rowBytes==w*stride);auto data=tight(argv[1],names[i],w,hh,pitches[i],stride);CHECK(data);CHECK(t[i].write(g.l,data,w*stride,i==6?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));HeapFree(GetProcessHeap(),0,data);}
 ID3DBlob *rootBlob=nullptr;D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,6,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,6}};D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,24};D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=2;rs.pParameters=params;CHECK(SUCCEEDED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&rootBlob,nullptr)));ID3D12RootSignature *root=nullptr;CHECK(SUCCEEDED(g.d->CreateRootSignature(0,rootBlob->GetBufferPointer(),rootBlob->GetBufferSize(),IID_ID3D12RootSignature,(void**)&root)));
 UINT64 shaderBytes{};auto shader=readFile(argv[2],L"history-visibility.cso",shaderBytes);CHECK(shader);D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root;ps.CS={shader,(SIZE_T)shaderBytes};ID3D12PipelineState *pipeline=nullptr;CHECK(SUCCEEDED(g.d->CreateComputePipelineState(&ps,IID_ID3D12PipelineState,(void**)&pipeline)));
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=7;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ID3D12DescriptorHeap *heap=nullptr;CHECK(SUCCEEDED(g.d->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)&heap)));auto handle=heap->GetCPUDescriptorHandleForHeapStart();UINT increment=g.d->GetDescriptorHandleIncrementSize(hd.Type);
 for(UINT i=0;i<6;i++){D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=(i==2||i==4)?DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:format[i];srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;g.d->CreateShaderResourceView(t[i].gpu,&srv,handle);handle.ptr+=increment;}
 D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=format[6];uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;g.d->CreateUnorderedAccessView(t[6].gpu,nullptr,&uav,handle);
 struct Constants{UINT rw,rh,w,h;float matrix[16],scale[2];UINT padding[2];} constants{h.rw,h.rh,h.w,h.h};static_assert(sizeof(Constants)==96,"HLSL layout");memcpy(constants.matrix,h.matrix,64);constants.scale[0]=.5f;constants.scale[1]=-.5f;
 g.l->SetDescriptorHeaps(1,&heap);g.l->SetComputeRootSignature(root);g.l->SetPipelineState(pipeline);g.l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());g.l->SetComputeRoot32BitConstants(1,24,&constants,0);g.l->Dispatch((h.rw+7)/8,(h.rh+7)/8,1);t[6].copyOut(g.l);CHECK(g.end());
 auto output=(BYTE*)HeapAlloc(GetProcessHeap(),0,SIZE_T(h.w)*h.h*2);CHECK(output&&t[6].read(output,h.w*2));UINT64 originalBytes{};auto original=readFile(argv[1],L"mask.bin",originalBytes);CHECK(original&&originalBytes==h.maskBytes);UINT changed=0,otherChanged=0;for(UINT y=0;y<h.h;y++)for(UINT x=0;x<h.w;x++){auto a=output+(SIZE_T(y)*h.w+x)*2,b=original+SIZE_T(y)*h.maskPitch+x*2;if(a[0]!=b[0]||a[1]!=b[1]){changed++;if(a[1]!=b[1]||b[0]!=255||a[0]!=0||x>=h.rw||y>=h.rh)otherChanged++;}memcpy(b,a,2);}
 CHECK(otherChanged==0);HANDLE out=CreateFileW(argv[3],GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);CHECK(out!=INVALID_HANDLE_VALUE);DWORD done;CHECK(WriteFile(out,original,(DWORD)originalBytes,&done,nullptr)&&done==originalBytes);CloseHandle(out);printf("GPU fence completed; visibility corrected pixels=%u invalid-or-outside changes=%u bytes=%llu\n",changed,otherChanged,(unsigned long long)originalBytes);return 0;
}
