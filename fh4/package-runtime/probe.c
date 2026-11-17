/* Standalone ABI and graph test. LGPL-2.1-or-later. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <appmodel.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
HMODULE WINAPI LoadPackagedLibrary(LPCWSTR, DWORD);
#define CHECK(x) do { if (!(x)) { printf("FAIL line=%d last_error=%lu\n",__LINE__,GetLastError()); return 1; } } while(0)
static int inside(const void *p, const BYTE *b, UINT32 n) { return (const BYTE *)p>=b && (const BYTE *)p<b+n; }
int wmain(int argc, WCHAR **argv)
{
    UINT32 n=0, size, count=0; LONG e; WCHAR name[1024], expected[1024]; BYTE *data;
    PACKAGE_INFO *infos; HMODULE m; int (*value)(void);
    if (argc>1 && !wcscmp(argv[1],L"--absent"))
    {
        CHECK(GetCurrentPackageFullName(&n,NULL)==APPMODEL_ERROR_NO_PACKAGE);
        CHECK(!LoadPackagedLibrary(L"head.dll",0) && GetLastError()==APPMODEL_ERROR_NO_PACKAGE);
        puts("UNPACKAGED_SCOPE_PASS"); return 0;
    }
    CHECK(GetCurrentPackageFullName(NULL,NULL)==ERROR_INVALID_PARAMETER);
    n=8; CHECK(GetCurrentPackageFullName(&n,NULL)==ERROR_INVALID_PARAMETER);
    n=0; e=GetCurrentPackageFullName(&n,NULL);
    if(e!=ERROR_INSUFFICIENT_BUFFER) {
        WCHAR actual[2048], wanted[2048], full[256], path[1024]; UINT32 length=1024, idsize=0;
        GetModuleFileNameW(NULL,actual,2048); GetEnvironmentVariableW(L"FH4_PACKAGE_EXE",wanted,2048);
        GetEnvironmentVariableW(L"FH4_PACKAGE_FULL",full,256);
        printf("context=%ld exe_equal=%d catalog=%ld id=%ld\n",e,!wcsicmp(actual,wanted),GetPackagePathByFullName(full,&length,path),PackageIdFromFullName(full,0,&idsize,NULL));
        wprintf(L"actual=%ls expected=%ls catalog_path=%ls\n",actual,wanted,path);
    }
    CHECK(e==ERROR_INSUFFICIENT_BUFFER);
    GetEnvironmentVariableW(L"FH4_PACKAGE_FULL",expected,1024); CHECK(n==wcslen(expected)+1);
    name[0]=0x1234; size=n-1;
    CHECK(GetCurrentPackageFullName(&size,name)==ERROR_INSUFFICIENT_BUFFER && name[0]==0x1234);
    size=1024; CHECK(!GetCurrentPackageFullName(&size,name) && !wcscmp(name,expected));
    size=1024; CHECK(!GetCurrentApplicationUserModelId(&size,name) && wcsstr(name,L"!Probe"));
    size=1024; CHECK(!GetCurrentPackageFamilyName(&size,name) && !wcscmp(name,L"Test.Main_0123456789abc"));
    size=1024; CHECK(!GetCurrentPackagePath(&size,name));
    GetEnvironmentVariableW(L"FH4_PACKAGE_PATH",expected,1024); CHECK(!wcscmp(name,expected));
    size=0; CHECK(GetCurrentPackageId(&size,NULL)==ERROR_INSUFFICIENT_BUFFER);
    data=malloc(size); CHECK(!GetCurrentPackageId(&size,data));
    CHECK(!wcscmp(((PACKAGE_ID *)data)->name,L"Test.Main")); free(data);
    size=0; CHECK(GetCurrentPackageInfo(0x30,&size,NULL,&count)==ERROR_INSUFFICIENT_BUFFER && count==2);
    data=malloc(size+32); memset(data,0xA5,size+32); n=size-1;
    CHECK(GetCurrentPackageInfo(0x30,&n,data,&count)==ERROR_INSUFFICIENT_BUFFER && data[0]==0xA5 && n==size);
    CHECK(!GetCurrentPackageInfo(0x30,&n,data,&count) && count==2);
    infos=(PACKAGE_INFO *)data;
    for (UINT32 i=0;i<count;i++)
    {
        CHECK(inside(infos[i].path,data,size) && inside(infos[i].packageFullName,data,size));
        CHECK(inside(infos[i].packageId.publisher,data,size));
        CHECK(!wcscmp(infos[i].packageId.publisher,L"CN=Fixture"));
        CHECK(infos[i].flags==(0x80000u | (i?1:0)));
    }
    CHECK(!wcscmp(infos[1].packageId.name,L"Test.Dependency"));
    for(UINT32 i=size;i<size+32;i++) CHECK(data[i]==0xA5);
    free(data);
    size=0; CHECK(GetCurrentPackageInfo(0x10,&size,NULL,&count)==ERROR_INSUFFICIENT_BUFFER && count==1);
    size=0; CHECK(GetCurrentPackageInfo(0x20,&size,NULL,&count)==ERROR_INSUFFICIENT_BUFFER && count==1);
    size=0; CHECK(!GetCurrentPackageInfo(0x40,&size,NULL,&count) && count==0 && size==0);
    size=0; CHECK(!GetCurrentPackageInfo(0x100000,&size,NULL,&count) && count==0);
    CHECK(!LoadPackagedLibrary(L"..\\head.dll",0) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!LoadPackagedLibrary(L"Z:\\head.dll",0) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!LoadPackagedLibrary(L"head.dll",1) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(!LoadPackagedLibrary(L"outside.dll",0) && GetLastError()==ERROR_MOD_NOT_FOUND);
    m=LoadPackagedLibrary(L"head",0); CHECK(m);
    value=(void *)GetProcAddress(m,"value"); CHECK(value && value()==42); FreeLibrary(m);
    m=LoadPackagedLibrary(L"first.dll",0); CHECK(m);
    value=(void *)GetProcAddress(m,"value"); CHECK(value && value()==1); FreeLibrary(m);
    CHECK(!LoadPackagedLibrary(L"broken.dll",0));
    puts("PACKAGE_CONTEXT_PASS strings=true short_buffer=true pointer_bounds=true graph_order=true transitive_dependency=true cwd_excluded=true missing_dependency_fails=true");
    return 0;
}
