/* LGPL-2.1-or-later. Query factories only; never activate a game or sign in. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <activation.h>
#include <stdio.h>

int wmain(int argc, WCHAR **argv)
{
    ACTCTXW ac={sizeof(ac)}; HANDLE ctx; ULONG_PTR cookie; int failures=0;
    if(argc<4) return 64;
    ac.lpSource=argv[1]; ac.lpAssemblyDirectory=argv[2];
    ac.dwFlags=ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID;
    ctx=CreateActCtxW(&ac);
    if(ctx==INVALID_HANDLE_VALUE) {printf("CREATE_CONTEXT_FAILED error=%lu\n",GetLastError());return 65;}
    if(!ActivateActCtx(ctx,&cookie)) return 66;
    if(FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 67;
    if(!SetDllDirectoryW(argv[2])) return 68;
    for(int i=3;i<argc;i++)
    {
        HSTRING name=NULL; IActivationFactory *factory=NULL;
        ACTCTX_SECTION_KEYED_DATA data={sizeof(data)};
        BOOL found=FindActCtxSectionStringW(0,NULL,ACTIVATION_CONTEXT_SECTION_WINRT_ACTIVATABLE_CLASSES,argv[i],&data);
        HRESULT hr=WindowsCreateString(argv[i],lstrlenW(argv[i]),&name);
        if(SUCCEEDED(hr)) hr=RoGetActivationFactory(name,&IID_IActivationFactory,(void**)&factory);
        printf("CLASS name=%ls manifest=%d hr=%08lx factory=%d\n",argv[i],found,(unsigned long)hr,factory!=NULL);
        fflush(stdout);
        if(!found || FAILED(hr) || !factory) failures++;
        if(factory) IActivationFactory_Release(factory);
        if(name) WindowsDeleteString(name);
    }
    RoUninitialize(); DeactivateActCtx(0,cookie); ReleaseActCtx(ctx);
    printf("PACKAGE_CLASSES_RESULT failures=%d instancesCreated=0 gameplayVerified=false\n",failures);
    return failures ? 1 : 0;
}
