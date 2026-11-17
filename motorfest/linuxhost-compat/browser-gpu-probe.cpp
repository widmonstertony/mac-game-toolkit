// SPDX-License-Identifier: AGPL-3.0-only
// Own offscreen D3D11 workload: no launcher UI, accounts or game code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <cwchar>
#include <string>
template<class T> struct Ref {T *p=nullptr;~Ref(){if(p)p->Release();}T *operator->()const{return p;}void **out(){return (void**)&p;}};
#define CHECK(x) do{HRESULT hr=(x);if(FAILED(hr)){printf("BROWSER_GPU_FAIL line=%d hr=%08lx\n",__LINE__,(unsigned long)hr);return 3;}}while(0)
int wmain(int argc,wchar_t **argv)
{
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=3 && argc!=4)return 2;
    bool child=argc==4;
    HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!dll){printf("BROWSER_DLL_FAIL error=%lu\n",GetLastError());return 3;}
    auto create=(PFN_D3D11_CREATE_DEVICE)GetProcAddress(dll,"D3D11CreateDevice");
    if(!create)return 3;
    Ref<ID3D11Device> dev;Ref<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
    CHECK(create(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                 nullptr,0,D3D11_SDK_VERSION,&dev.p,&level,&ctx.p));
    Ref<ID3D11Device1> dev1;CHECK(dev->QueryInterface(__uuidof(ID3D11Device1),dev1.out()));
    Ref<ID3D11Texture2D> tex,staging;Ref<IDXGIKeyedMutex> mutex;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=64;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if(child) {
        CHECK(dev1->OpenSharedResourceByName(argv[2],DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,
            __uuidof(ID3D11Texture2D),tex.out()));
    } else CHECK(dev->CreateTexture2D(&desc,nullptr,&tex.p));
    CHECK(tex->QueryInterface(__uuidof(IDXGIKeyedMutex),mutex.out()));
    HRESULT acquire=mutex->AcquireSync(child?1:0,10000);
    if(acquire!=S_OK){printf("BROWSER_MUTEX_FAIL hr=%08lx\n",(unsigned long)acquire);return 4;}
    if(child) {
        desc.MiscFlags=desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        CHECK(dev->CreateTexture2D(&desc,nullptr,&staging.p));ctx->CopyResource(staging.p,tex.p);
        D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(ctx->Map(staging.p,0,D3D11_MAP_READ,0,&mapped));
        const unsigned expected[]={191,128,64,255};unsigned bad=0;
        for(unsigned y=0;y<64;y++)for(unsigned x=0;x<256;x++){
            unsigned v=((unsigned char*)mapped.pData)[y*mapped.RowPitch+x];
            if(v+1<expected[x%4]||v>expected[x%4]+1)bad++;
        }
        ctx->Unmap(staging.p,0);CHECK(mutex->ReleaseSync(2));
        if(bad){printf("BROWSER_READBACK_FAIL bytes=%u\n",bad);return 5;}
        puts("BROWSER_CHILD_SHARED_READBACK_PASS pixels=4096");return 0;
    }
    Ref<ID3D11RenderTargetView> view;CHECK(dev->CreateRenderTargetView(tex.p,nullptr,&view.p));
    const float color[]={.25f,.5f,.75f,1.f};ctx->ClearRenderTargetView(view.p,color);
    Ref<IDXGIResource1> resource;CHECK(tex->QueryInterface(__uuidof(IDXGIResource1),resource.out()));
    HANDLE shared=nullptr;
    CHECK(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,argv[2],&shared));
    ctx->Flush();CHECK(mutex->ReleaseSync(1));
    wchar_t executable[32768];if(!GetModuleFileNameW(nullptr,executable,32768))return 6;
    std::wstring command=L"\""+std::wstring(executable)+L"\" \""+argv[1]+L"\" \""+argv[2]+L"\" --child";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi))return 6;
    DWORD wait=WaitForSingleObject(pi.hProcess,20000),code=999;
    if(wait==WAIT_TIMEOUT){TerminateProcess(pi.hProcess,124);WaitForSingleObject(pi.hProcess,2000);}
    GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(shared);
    if(wait!=WAIT_OBJECT_0||code){printf("BROWSER_CHILD_FAIL wait=%lu code=%lu\n",wait,code);return 7;}
    acquire=mutex->AcquireSync(2,10000);
    if(acquire!=S_OK)return 8;CHECK(mutex->ReleaseSync(0));
    puts("BROWSER_CROSS_PROCESS_GPU_PASS pixels=4096 keyed_mutex=true visiblePresentation=false");return 0;
}
