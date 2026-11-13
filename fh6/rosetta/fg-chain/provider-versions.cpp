// Real provider identity check; no inference about displayed frames or ghosting.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <framegeneration/include/ffx_framegeneration.h>
#include <upscalers/include/ffx_upscale.h>
#include <cstdio>
#include <cstring>

int wmain(int argc,wchar_t **argv)
{
    if(argc!=4)return 2;
    ID3D12Device *device=nullptr;
    if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)&device)))return 3;
    uint64_t previousIds[8]{},previousCount=0;
    const uint64_t type=wcscmp(argv[3],L"sr")==0?FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE:FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    for(int i=1;i<=2;i++){
        HMODULE m=LoadLibraryExW(argv[i],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!m){printf("load failed %d %lu\n",i,GetLastError());return 4;}
        auto query=(PfnFfxQuery)GetProcAddress(m,"ffxQuery");if(!query)return 5;
        uint64_t ids[8]{},count=8;const char *names[8]{};
        ffxQueryDescGetVersions q{};q.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        q.createDescType=type;q.device=device;q.outputCount=&count;q.versionIds=ids;q.versionNames=names;
        const auto result=query(nullptr,&q.header);
        printf("provider=%d result=%u count=%llu\n",i,result,(unsigned long long)count);
        if(result || !count || count>8)return 6;
        for(uint64_t j=0;j<count;j++)printf("  %llu %s\n",(unsigned long long)ids[j],names[j]?names[j]:"?");
        if(i==1){previousCount=count;memcpy(previousIds,ids,sizeof(ids));}
        else if(count!=previousCount||memcmp(previousIds,ids,count*sizeof(uint64_t)))return 7;
    }
    puts("PASS actual provider enumeration identical through observer");return 0;
}
