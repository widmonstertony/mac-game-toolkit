/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Headless contract probe: own hidden window, no game or account access. */
#define INITGUID
#include "private.h"
#include <roapi.h>
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); ExitProcess(2); } } while(0)
#define OK(x) CHECK((x) == S_OK)
static ICoreApplication *app;
static ICoreWindowStatic *windows;
static ICoreDispatcher *retained_dispatcher;
static HWND last_window;
static unsigned fail_stage, stage, uninitializes;
static LONG refs = 1;
static BOOL run_gpu;
static BOOL gpu_bridge;
static HRESULT gpu_result=E_PENDING;
static HRESULT stage_result(void) { return ++stage == fail_stage ? E_ABORT : S_OK; }
static LONG handler_refs=1;
static unsigned activations;
static EventRegistrationToken tokens[3];
static IActivatedEventArgs *retained_args;
static HRESULT WINAPI activated_qi(ITypedEventHandler_CoreApplicationView_IActivatedEventArgs *p,REFIID id,void **out)
{ *out=NULL; return E_NOINTERFACE; }
static ULONG WINAPI activated_add(ITypedEventHandler_CoreApplicationView_IActivatedEventArgs *p)
{ return InterlockedIncrement(&handler_refs); }
static ULONG WINAPI activated_release(ITypedEventHandler_CoreApplicationView_IActivatedEventArgs *p)
{ return InterlockedDecrement(&handler_refs); }
static HRESULT WINAPI activated(ITypedEventHandler_CoreApplicationView_IActivatedEventArgs *p,
    ICoreApplicationView *sender,IActivatedEventArgs *args)
{
    ActivationKind kind; ApplicationExecutionState previous;
    CHECK(stage==4); CHECK(activations<2);
    OK(args->lpVtbl->get_Kind(args,&kind)); CHECK(kind==ActivationKind_Launch);
    OK(args->lpVtbl->get_PreviousExecutionState(args,&previous));
    CHECK(previous==ApplicationExecutionState_NotRunning);
    fh4_launch_iface *launch=NULL; HSTRING id=NULL,arguments=NULL; IUnknown *identity=NULL;
    OK(args->lpVtbl->QueryInterface(args,&fh4_IID_ILaunchActivatedEventArgs,(void**)&launch));
    OK(launch->lpVtbl->get_TileId(launch,&id));
    CHECK(!wcscmp(WindowsGetStringRawBuffer(id,NULL),L"Probe")); WindowsDeleteString(id);
    OK(launch->lpVtbl->get_Arguments(launch,&arguments));
    CHECK(WindowsGetStringLen(arguments)==0); WindowsDeleteString(arguments);
    OK(launch->lpVtbl->QueryInterface(launch,&IID_IUnknown,(void**)&identity));
    CHECK(identity==(IUnknown*)args); IUnknown_Release(identity); launch->lpVtbl->Release(launch);
    OK(ICoreApplicationView_remove_Activated(sender,tokens[activations]));
    if (!activations) { retained_args=args; args->lpVtbl->AddRef(args); }
    ++activations;
    return fail_stage==6 ? E_ABORT : S_OK;
}
static const ITypedEventHandler_CoreApplicationView_IActivatedEventArgsVtbl activated_vtbl={
    activated_qi,activated_add,activated_release,activated};
static ITypedEventHandler_CoreApplicationView_IActivatedEventArgs handler={&activated_vtbl};


static HRESULT WINAPI view_qi(IFrameworkView *p, REFIID id, void **out)
{
    *out = NULL;
    if (!IsEqualGUID(id,&IID_IUnknown) && !IsEqualGUID(id,&IID_IInspectable) &&
        !IsEqualGUID(id,&IID_IFrameworkView)) return E_NOINTERFACE;
    *out=p; InterlockedIncrement(&refs); return S_OK;
}
static ULONG WINAPI view_add(IFrameworkView *p) { return InterlockedIncrement(&refs); }
static ULONG WINAPI view_release(IFrameworkView *p) { return InterlockedDecrement(&refs); }
static HRESULT WINAPI view_iids(IFrameworkView *p, ULONG *n, IID **v) { *n=0; *v=NULL; return S_OK; }
static HRESULT WINAPI view_name(IFrameworkView *p,HSTRING *v) { *v=NULL; return E_NOTIMPL; }
static HRESULT WINAPI view_trust(IFrameworkView *p,TrustLevel *v) { *v=BaseTrust; return S_OK; }
static HRESULT WINAPI view_init(IFrameworkView *p,ICoreApplicationView *v)
{
    ICoreApplicationView *current=NULL; ICoreWindow *a=NULL,*b=NULL;
    CHECK(stage==1);
    for (unsigned i=0;i<3;++i)
    { OK(ICoreApplicationView_add_Activated(v,&handler,&tokens[i])); CHECK(tokens[i].value); }
    CHECK(tokens[0].value!=tokens[1].value && tokens[1].value!=tokens[2].value);
    OK(ICoreApplicationView_remove_Activated(v,tokens[2]));
    OK(ICoreApplication_GetCurrentView(app,&current)); CHECK(current==v);
    OK(ICoreApplicationView_get_CoreWindow(v,&a)); CHECK(a);
    OK(ICoreWindowStatic_GetForCurrentThread(windows,&b)); CHECK(a==b);
    ICoreWindow_Release(a); ICoreWindow_Release(b); ICoreApplicationView_Release(current);
    return stage_result();
}
static DWORD WINAPI wrong_thread(void *arg)
{
    ICoreDispatcher *d=arg; boolean access=TRUE; ICoreWindow *w=(void*)1; ICoreApplicationView *v=(void*)1;
    CHECK(ICoreApplication_GetCurrentView(app,&v)==RPC_E_WRONG_THREAD); CHECK(!v);
    OK(ICoreWindowStatic_GetForCurrentThread(windows,&w)); CHECK(!w);
    OK(ICoreDispatcher_get_HasThreadAccess(d,&access)); CHECK(!access);
    CHECK(ICoreDispatcher_ProcessEvents(d,CoreProcessEventsOption_ProcessOneIfPresent)==RPC_E_WRONG_THREAD);
    return 0;
}
static HRESULT WINAPI view_window(IFrameworkView *p,ICoreWindow *w)
{
    ICoreWindowInterop *interop=NULL; ICoreDispatcher *d=NULL; boolean access=FALSE; HANDLE t;
    CHECK(stage==2);
    OK(ICoreWindow_QueryInterface(w,&IID_ICoreWindowInterop,(void**)&interop));
    OK(interop->lpVtbl->get_WindowHandle(interop,&last_window)); CHECK(IsWindow(last_window));
    IUnknown *u1=NULL,*u2=NULL; ICoreWindow *roundtrip=NULL;
    OK(interop->lpVtbl->QueryInterface(interop,&IID_ICoreWindow,(void**)&roundtrip)); CHECK(roundtrip==w);
    ICoreWindow_Release(roundtrip);
    OK(interop->lpVtbl->QueryInterface(interop,&IID_IUnknown,(void**)&u1));
    OK(ICoreWindow_QueryInterface(w,&IID_IUnknown,(void**)&u2)); CHECK(u1==u2);
    IUnknown_Release(u1); IUnknown_Release(u2);
    CHECK(!IsWindowVisible(last_window));
    if (run_gpu && !fail_stage && gpu_result==E_PENDING)
    {
        HMODULE dll=LoadLibraryW(L"gpu-probe.dll");
        HRESULT (WINAPI *probe)(IUnknown*,BOOL)=dll ? (void*)GetProcAddress(dll,"RunGpuProbe") : NULL;
        CHECK(probe);
        gpu_result=probe((IUnknown*)w,gpu_bridge);
        printf("GPU_COREWINDOW_RESULT=%08lx\n",(unsigned long)gpu_result);
        FreeLibrary(dll);
    }
    interop->lpVtbl->Release(interop);
    OK(ICoreWindow_get_Dispatcher(w,&d)); CHECK(d);
    OK(ICoreDispatcher_get_HasThreadAccess(d,&access)); CHECK(access);
    OK(ICoreDispatcher_ProcessEvents(d,CoreProcessEventsOption_ProcessAllIfPresent));
    /* Calling an empty queue must never dispatch uninitialized stack data. */
    OK(ICoreDispatcher_ProcessEvents(d,CoreProcessEventsOption_ProcessOneIfPresent));
    CHECK(ICoreDispatcher_ProcessEvents(d,(CoreProcessEventsOption)77)==E_INVALIDARG);
    t=CreateThread(NULL,0,wrong_thread,d,0,NULL); CHECK(t);
    CHECK(WaitForSingleObject(t,5000)==WAIT_OBJECT_0); CloseHandle(t);
    retained_dispatcher=d;
    return stage_result();
}
static HRESULT WINAPI view_load(IFrameworkView *p,HSTRING entry)
{
    HSTRING id; CHECK(stage==3);
    CHECK(!wcscmp(WindowsGetStringRawBuffer(entry,NULL),L"Probe.App"));
    OK(ICoreApplication_get_Id(app,&id));
    CHECK(!wcscmp(WindowsGetStringRawBuffer(id,NULL),L"Probe")); WindowsDeleteString(id);
    return stage_result();
}
static HRESULT WINAPI view_run(IFrameworkView *p)
{
    MSG msg; CHECK(stage==4); CHECK(activations==2);
    CHECK(PostThreadMessageW(GetCurrentThreadId(),WM_APP+1,0,0));
    CHECK(PostThreadMessageW(GetCurrentThreadId(),WM_APP+2,0,0));
    OK(ICoreDispatcher_ProcessEvents(retained_dispatcher,CoreProcessEventsOption_ProcessOneAndAllPending));
    CHECK(!PeekMessageW(&msg,NULL,WM_APP+1,WM_APP+2,PM_NOREMOVE));
    return stage_result();
}
static HRESULT WINAPI view_end(IFrameworkView *p) { ++uninitializes; return S_OK; }
static const IFrameworkViewVtbl view_vtbl={view_qi,view_add,view_release,view_iids,view_name,view_trust,
    view_init,view_window,view_load,view_run,view_end};
static IFrameworkView view={&view_vtbl};
static HRESULT WINAPI source_qi(IFrameworkViewSource *p,REFIID id,void **v) { *v=NULL; return E_NOINTERFACE; }
static ULONG WINAPI source_add(IFrameworkViewSource *p) { return 2; }
static ULONG WINAPI source_release(IFrameworkViewSource *p) { return 1; }
static HRESULT WINAPI source_iids(IFrameworkViewSource *p,ULONG *n,IID **v) { *n=0; *v=NULL; return S_OK; }
static HRESULT WINAPI source_name(IFrameworkViewSource *p,HSTRING *v) { *v=NULL; return E_NOTIMPL; }
static HRESULT WINAPI source_trust(IFrameworkViewSource *p,TrustLevel *v) { *v=BaseTrust; return S_OK; }
static HRESULT WINAPI source_create(IFrameworkViewSource *p,IFrameworkView **out)
{
    HRESULT hr=stage_result(); *out=NULL;
    if (FAILED(hr)) return hr;
    *out=&view; IFrameworkView_AddRef(*out); return S_OK;
}
static const IFrameworkViewSourceVtbl source_vtbl={source_qi,source_add,source_release,source_iids,
    source_name,source_trust,source_create};
static IFrameworkViewSource source={&source_vtbl};

int main(int argc,char **argv)
{
    HMODULE dll; HSTRING name; IActivationFactory *factory=NULL; ICoreWindow *w=NULL;
    HRESULT (WINAPI *get_factory)(HSTRING,IActivationFactory**);
    ACTCTXW ctx={sizeof(ctx)}; HANDLE context=INVALID_HANDLE_VALUE; ULONG_PTR cookie=0;
    BOOL automatic=FALSE;
    for(int i=1;i<argc;i++)
    {
        if(!strcmp(argv[i],"--gpu"))run_gpu=TRUE;
        else if(!strcmp(argv[i],"--gpu-bridge"))run_gpu=gpu_bridge=TRUE;
        else if(!strcmp(argv[i],"--auto-manifest"))automatic=TRUE;
        else CHECK(FALSE);
    }
    OK(RoInitialize(RO_INIT_SINGLETHREADED));
    if(!automatic)
    {
        ctx.lpSource=L"runtime.manifest";
        context=CreateActCtxW(&ctx); CHECK(context!=INVALID_HANDLE_VALUE);
        CHECK(ActivateActCtx(context,&cookie));
    }
    dll=LoadLibraryW(L"fh4-uwp-runtime.dll"); CHECK(dll);
    get_factory=(void*)GetProcAddress(dll,"DllGetActivationFactory"); CHECK(get_factory);
    OK(WindowsCreateString(L"Windows.ApplicationModel.Core.CoreApplication",45,&name));
    OK(get_factory(name,&factory)); WindowsDeleteString(name);
    OK(IActivationFactory_QueryInterface(factory,&IID_ICoreApplication,(void**)&app));
    ICoreApplication *routed=NULL;
    OK(WindowsCreateString(L"Windows.ApplicationModel.Core.CoreApplication",45,&name));
    OK(RoGetActivationFactory(name,&IID_ICoreApplication,(void**)&routed));
    CHECK(routed==app); ICoreApplication_Release(routed); WindowsDeleteString(name);
    IActivationFactory_Release(factory);
    OK(WindowsCreateString(L"Windows.UI.Core.CoreWindow",26,&name));
    OK(get_factory(name,&factory)); WindowsDeleteString(name);
    OK(IActivationFactory_QueryInterface(factory,&IID_ICoreWindowStatic,(void**)&windows));
    ICoreWindowStatic *routed_windows=NULL;
    OK(WindowsCreateString(L"Windows.UI.Core.CoreWindow",26,&name));
    OK(RoGetActivationFactory(name,&IID_ICoreWindowStatic,(void**)&routed_windows));
    CHECK(routed_windows==windows); ICoreWindowStatic_Release(routed_windows); WindowsDeleteString(name);
    IActivationFactory_Release(factory);
    OK(ICoreWindowStatic_GetForCurrentThread(windows,&w)); CHECK(!w);
    CHECK(ICoreApplication_Run(app,NULL)==E_POINTER);
    for (unsigned pass=0;pass<8;++pass)
    {
        HRESULT hr; MSG msg;
        fail_stage=pass < 7 ? pass : 0;
        stage=uninitializes=activations=0; last_window=NULL;
        hr=ICoreApplication_Run(app,&source);
        CHECK(hr==(fail_stage ? E_ABORT : S_OK));
        CHECK(stage==(fail_stage==6 ? 4 : (fail_stage ? fail_stage : 5)));
        CHECK(activations==(fail_stage==6 ? 1 : (fail_stage==0 || fail_stage==5 ? 2 : 0)));
        CHECK(handler_refs==1);
        if (retained_args) { CHECK(retained_args->lpVtbl->Release(retained_args)==0); retained_args=NULL; }
        CHECK(uninitializes==(fail_stage==1 || fail_stage==2 ? 0 : 1));
        CHECK(refs==1); CHECK(!last_window || !IsWindow(last_window));
        CHECK(!PeekMessageW(&msg,NULL,WM_QUIT,WM_QUIT,PM_NOREMOVE));
        OK(ICoreWindowStatic_GetForCurrentThread(windows,&w)); CHECK(!w);
        if (retained_dispatcher)
        {
            OK(ICoreDispatcher_ProcessEvents(retained_dispatcher,CoreProcessEventsOption_ProcessAllIfPresent));
            CHECK(ICoreDispatcher_Release(retained_dispatcher)==0); retained_dispatcher=NULL;
        }
        printf("case=%u stage=%u cleanup=%u result=%08lx refs=%ld OK\n",pass,stage,uninitializes,(unsigned long)hr,refs);
    }
    ICoreWindowStatic_Release(windows); ICoreApplication_Release(app);
    /* Window classes keep module procedures registered until process exit. */
    if(!automatic) { CHECK(DeactivateActCtx(0,cookie)); ReleaseActCtx(context); }
    else puts("AUTOMATIC_MANIFEST_ROUTING_PASS no_registry_changes=true");
    RoUninitialize(); puts("LIFECYCLE_PASS gameplayVerified=false activationEventSubsetVerified=true activationRoutingVerified=true");
    return run_gpu && FAILED(gpu_result) ? 3 : 0;
}
