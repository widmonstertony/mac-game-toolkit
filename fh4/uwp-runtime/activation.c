/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Minimal local-launch event. No licensing or login result is synthesized.
 * Splash-screen support remains unimplemented. */
#include "private.h"

struct launch_args { IActivatedEventArgs iface; fh4_launch_iface launch; LONG refs; HSTRING id; };
static struct launch_args *impl(IActivatedEventArgs *p)
{ return CONTAINING_RECORD(p,struct launch_args,iface); }
static HRESULT WINAPI query(IActivatedEventArgs *p,REFIID id,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (IsEqualGUID(id,&IID_IUnknown) || IsEqualGUID(id,&IID_IInspectable) ||
        IsEqualGUID(id,&IID_IActivatedEventArgs)) *out=p;
    else if (IsEqualGUID(id,&fh4_IID_ILaunchActivatedEventArgs)) *out=&impl(p)->launch;
    else return E_NOINTERFACE;
    InterlockedIncrement(&impl(p)->refs); return S_OK;
}
static ULONG WINAPI add(IActivatedEventArgs *p) { return InterlockedIncrement(&impl(p)->refs); }
static ULONG WINAPI release(IActivatedEventArgs *p)
{ ULONG n=InterlockedDecrement(&impl(p)->refs); if (!n) { WindowsDeleteString(impl(p)->id); free(impl(p)); } return n; }
static HRESULT WINAPI iids(IActivatedEventArgs *p,ULONG *n,IID **out)
{
    if (!n || !out) return E_POINTER;
    *n=0; *out=CoTaskMemAlloc(2*sizeof(IID));
    if (!*out) return E_OUTOFMEMORY;
    (*out)[0]=IID_IActivatedEventArgs; (*out)[1]=fh4_IID_ILaunchActivatedEventArgs; *n=2; return S_OK;
}
static HRESULT WINAPI name(IActivatedEventArgs *p,HSTRING *out)
{
    const WCHAR cls[]=L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs";
    if (!out) return E_POINTER;
    return WindowsCreateString(cls,ARRAYSIZE(cls)-1,out);
}
static HRESULT WINAPI trust(IActivatedEventArgs *p,TrustLevel *out)
{ if (!out) return E_POINTER; *out=BaseTrust; return S_OK; }
static HRESULT WINAPI kind(IActivatedEventArgs *p,ActivationKind *out)
{ if (!out) return E_POINTER; *out=ActivationKind_Launch; return S_OK; }
static HRESULT WINAPI previous(IActivatedEventArgs *p,ApplicationExecutionState *out)
{ if (!out) return E_POINTER; *out=ApplicationExecutionState_NotRunning; return S_OK; }
static HRESULT WINAPI splash(IActivatedEventArgs *p,ISplashScreen **out)
{ if (!out) return E_POINTER; *out=NULL; return E_NOTIMPL; }
static const IActivatedEventArgsVtbl vtable={query,add,release,iids,name,trust,kind,previous,splash};
static IActivatedEventArgs *base(fh4_launch_iface *p)
{ return &CONTAINING_RECORD(p,struct launch_args,launch)->iface; }
static HRESULT WINAPI launch_query(fh4_launch_iface *p,REFIID id,void **out) { return query(base(p),id,out); }
static ULONG WINAPI launch_add(fh4_launch_iface *p) { return add(base(p)); }
static ULONG WINAPI launch_release(fh4_launch_iface *p) { return release(base(p)); }
static HRESULT WINAPI launch_iids(fh4_launch_iface *p,ULONG *n,IID **out) { return iids(base(p),n,out); }
static HRESULT WINAPI launch_name(fh4_launch_iface *p,HSTRING *out) { return name(base(p),out); }
static HRESULT WINAPI launch_trust(fh4_launch_iface *p,TrustLevel *out) { return trust(base(p),out); }
static HRESULT WINAPI launch_arguments(fh4_launch_iface *p,HSTRING *out)
{ if (!out) return E_POINTER; return WindowsCreateString(NULL,0,out); }
static HRESULT WINAPI launch_id(fh4_launch_iface *p,HSTRING *out)
{ if (!out) return E_POINTER; return WindowsDuplicateString(impl(base(p))->id,out); }
static const struct fh4_launch_vtbl launch_vtable={launch_query,launch_add,launch_release,
    launch_iids,launch_name,launch_trust,launch_arguments,launch_id};
HRESULT create_launch_args(const char *id,IActivatedEventArgs **out)
{
    struct launch_args *p;
    int n; WCHAR *wide; HRESULT hr;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!(p=calloc(1,sizeof(*p)))) return E_OUTOFMEMORY;
    p->iface.lpVtbl=&vtable; p->launch.lpVtbl=&launch_vtable; p->refs=1;
    n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id,-1,NULL,0);
    if (!n) { free(p); return E_INVALIDARG; }
    if (!(wide=malloc(n*sizeof(*wide)))) { free(p); return E_OUTOFMEMORY; }
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id,-1,wide,n))
    { free(wide); free(p); return E_INVALIDARG; }
    hr=WindowsCreateString(wide,n-1,&p->id); free(wide);
    if (FAILED(hr)) { free(p); return hr; }
    *out=&p->iface; return S_OK;
}
