/* WinRT Windows.ApplicationModel.Core.CoreApplication implementation
 *
 * Copyright 2024 Onni Kukkonen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "private.h"
#include <assert.h>
#include <winuser.h>
#include <pathcch.h>
#include <shlwapi.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(coreapp);

struct activation_subscription {
    struct activation_subscription *next;
    ITypedEventHandler_CoreApplicationView_IActivatedEventArgs *handler;
    EventRegistrationToken token;
};

struct coreapp_impl
{
    IActivationFactory IActivationFactory_iface;
    ICoreApplication ICoreApplication_iface;
    ICoreApplicationView ICoreApplicationView_iface;
    IFrameworkView *current_view;
    struct corewindow_impl *window;
    LONG running;
    struct app_manifest manifest;
    struct activation_subscription *subscriptions;
    INT64 next_token;
    DWORD owner_thread;
    LONG ref;
};

static inline struct coreapp_impl *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct coreapp_impl, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(
        IActivationFactory *iface, REFIID iid, void **out)
{
    struct coreapp_impl *factory = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ICoreApplication))
    {
        IUnknown_AddRef(iface);
        *out = &factory->ICoreApplication_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    struct coreapp_impl *factory = impl_from_IActivationFactory(iface);
    ULONG refcount = InterlockedIncrement(&factory->ref);

    TRACE("iface %p, refcount %lu.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    struct coreapp_impl *factory = impl_from_IActivationFactory(iface);
    ULONG refcount = InterlockedDecrement(&factory->ref);

    TRACE("iface %p, refcount %lu.\n", iface, refcount);

    return refcount;
}

static HRESULT WINAPI factory_GetIids(
        IActivationFactory *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName(
        IActivationFactory *iface, HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel(
        IActivationFactory *iface, TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance(
        IActivationFactory *iface, IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};


static inline struct coreapp_impl *impl_from_ICoreApplication( ICoreApplication *iface )
{
    return CONTAINING_RECORD( iface, struct coreapp_impl, ICoreApplication_iface );
}

static HRESULT WINAPI coreapp_impl_QueryInterface( ICoreApplication *iface, REFIID iid, void **out )
{
    struct coreapp_impl *impl = impl_from_ICoreApplication( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreApplication ))
    {
        *out = &impl->ICoreApplication_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI coreapp_impl_AddRef( ICoreApplication *iface )
{
    struct coreapp_impl *impl = impl_from_ICoreApplication( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI coreapp_impl_Release( ICoreApplication *iface )
{
    struct coreapp_impl *impl = impl_from_ICoreApplication( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    /* Singleton retains its initial reference. */
    return ref;
}

static HRESULT WINAPI coreapp_impl_GetIids( ICoreApplication *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI coreapp_impl_GetRuntimeClassName( ICoreApplication *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI coreapp_impl_GetTrustLevel( ICoreApplication *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT string_from_utf8(const char *s,HSTRING *out)
{
    int n; WCHAR *wide; HRESULT hr;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!s) return E_ILLEGAL_METHOD_CALL;
    n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);
    if (!n) return HRESULT_FROM_WIN32(GetLastError());
    if (!(wide=malloc(n*sizeof(*wide)))) return E_OUTOFMEMORY;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,wide,n))
    { hr=HRESULT_FROM_WIN32(GetLastError()); free(wide); return hr; }
    hr=WindowsCreateString(wide,n-1,out); free(wide); return hr;
}

static HRESULT WINAPI coreapp_impl_get_Id(ICoreApplication *iface,HSTRING *value)
{
    return string_from_utf8(impl_from_ICoreApplication(iface)->manifest.id,value);
}

static void clear_subscriptions(struct coreapp_impl *impl)
{
    struct activation_subscription *p;
    while ((p=impl->subscriptions))
    {
        impl->subscriptions=p->next;
        p->handler->lpVtbl->Release(p->handler); free(p);
    }
}

static HRESULT dispatch_activation(struct coreapp_impl *impl)
{
    struct activation_subscription *p;
    ITypedEventHandler_CoreApplicationView_IActivatedEventArgs **snapshot;
    IActivatedEventArgs *args=NULL;
    size_t count=0, i=0;
    HRESULT hr=create_launch_args(impl->manifest.id,&args);
    if (FAILED(hr)) return hr;
    for (p=impl->subscriptions;p;p=p->next) ++count;
    snapshot=calloc(count ? count : 1,sizeof(*snapshot));
    if (!snapshot) { args->lpVtbl->Release(args); return E_OUTOFMEMORY; }
    for (p=impl->subscriptions;p;p=p->next)
    { snapshot[i++]=p->handler; p->handler->lpVtbl->AddRef(p->handler); }
    for (i=0;i<count;i++)
    {
        if (SUCCEEDED(hr)) hr=snapshot[i]->lpVtbl->Invoke(snapshot[i],&impl->ICoreApplicationView_iface,args);
        snapshot[i]->lpVtbl->Release(snapshot[i]);
    }
    free(snapshot); args->lpVtbl->Release(args); return hr;
}

static HRESULT WINAPI coreapp_impl_add_Suspending( ICoreApplication *iface, IEventHandler_SuspendingEventArgs *handler, EventRegistrationToken *token)
{
    FIXME("iface %p, handler %p, token %p stub.\n", iface, handler, token);
    if (token) token->value=0;
    return E_NOTIMPL;
}

static HRESULT WINAPI coreapp_impl_remove_Suspending( ICoreApplication *iface, EventRegistrationToken token)
{
    FIXME("iface %p, token %#I64x stub.\n", iface, token.value);
    return E_NOTIMPL;
}

static HRESULT WINAPI coreapp_impl_add_Resuming( ICoreApplication *iface, IEventHandler_IInspectable *handler, EventRegistrationToken *token)
{
    FIXME("iface %p, handler %p, token %p stub.\n", iface, handler, token);
    if (token) token->value=0;
    return E_NOTIMPL;
}

static HRESULT WINAPI coreapp_impl_remove_Resuming( ICoreApplication *iface, EventRegistrationToken token)
{
    FIXME("iface %p, token %#I64x stub.\n", iface, token.value);
    return E_NOTIMPL;
}

static HRESULT WINAPI coreapp_impl_get_Properties( ICoreApplication *iface, IPropertySet **value)
{
    FIXME("iface %p, value %p stub.\n", iface, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI coreapp_impl_GetCurrentView( ICoreApplication *iface, ICoreApplicationView **value)
{
    struct coreapp_impl *impl = impl_from_ICoreApplication(iface);
    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->running && GetCurrentThreadId()!=impl->owner_thread) return RPC_E_WRONG_THREAD;
    *value = impl->running ? &impl->ICoreApplicationView_iface : NULL;
    if (*value) ICoreApplicationView_AddRef(*value);
    return *value ? S_OK : E_ILLEGAL_METHOD_CALL;
}

DWORD corewindow_tls = TLS_OUT_OF_INDEXES;
static HRESULT WINAPI coreapp_impl_Run(ICoreApplication *iface, IFrameworkViewSource *source)
{
    struct coreapp_impl *impl = impl_from_ICoreApplication(iface);
    IFrameworkView *view = NULL;
    HSTRING entry = NULL;
    HRESULT hr = E_FAIL;
    BOOL initialized = FALSE;
    if (!source) return E_POINTER;
    if (InterlockedCompareExchange(&impl->running,1,0)) return E_ILLEGAL_METHOD_CALL;
    impl->owner_thread=GetCurrentThreadId();
    if (!read_app_manifest(&impl->manifest))
    { hr = APPX_E_INVALID_MANIFEST; goto done; }
    corewindow_tls = TlsAlloc();
    if (corewindow_tls == TLS_OUT_OF_INDEXES) { hr = E_OUTOFMEMORY; goto done; }
    hr = IFrameworkViewSource_CreateView(source,&view);
    if (FAILED(hr)) goto done;
    if (!view) { hr = E_UNEXPECTED; goto done; }
    impl->window = create_corewindow(view,impl->manifest.identity,impl->manifest.display);
    if (!impl->window) { hr = E_OUTOFMEMORY; goto done; }
    hr = IFrameworkView_Initialize(view,&impl->ICoreApplicationView_iface);
    if (FAILED(hr)) goto done;
    initialized = TRUE;
    hr = IFrameworkView_SetWindow(view,&impl->window->ICoreWindow_iface);
    if (FAILED(hr)) goto done;
    hr = string_from_utf8(impl->manifest.entry,&entry);
    if (FAILED(hr)) goto done;
    hr = IFrameworkView_Load(view,entry);
    if (FAILED(hr)) goto done;
    hr = dispatch_activation(impl);
    if (FAILED(hr)) goto done;
    hr = IFrameworkView_Run(view);
done:
    if (initialized)
    {
        HRESULT end = IFrameworkView_Uninitialize(view);
        if (SUCCEEDED(hr) && FAILED(end)) hr = end;
    }
    WindowsDeleteString(entry);
    if (corewindow_tls != TLS_OUT_OF_INDEXES)
    {
        LocalFree(TlsGetValue(corewindow_tls));
        TlsSetValue(corewindow_tls,NULL);
        TlsFree(corewindow_tls); corewindow_tls = TLS_OUT_OF_INDEXES;
    }
    if (impl->window)
    {
        impl->window->dispatcher->stopped = TRUE;
        SetWindowLongPtrW(impl->window->window_handle,GWLP_USERDATA,0);
        DestroyWindow(impl->window->window_handle);
        impl->window->window_handle = NULL;
        ICoreWindow_Release(&impl->window->ICoreWindow_iface); impl->window = NULL;
    }
    if (view) IFrameworkView_Release(view);
    clear_subscriptions(impl);
    free_app_manifest(&impl->manifest);
    impl->owner_thread=0;
    InterlockedExchange(&impl->running,0);
    return hr;
}

static HRESULT WINAPI coreapp_impl_RunWithActivationFactories( ICoreApplication *iface, IGetActivationFactory *factory)
{
    FIXME("iface %p, factory %p stub.\n", iface, factory);
    return E_NOTIMPL;
}

static const struct ICoreApplicationVtbl coreapp_impl_vtbl =
{
    coreapp_impl_QueryInterface,
    coreapp_impl_AddRef,
    coreapp_impl_Release,
    /* IInspectable methods */
    coreapp_impl_GetIids,
    coreapp_impl_GetRuntimeClassName,
    coreapp_impl_GetTrustLevel,
    /* ICoreApplication methods */
    coreapp_impl_get_Id,
    coreapp_impl_add_Suspending,
    coreapp_impl_remove_Suspending,
    coreapp_impl_add_Resuming,
    coreapp_impl_remove_Resuming,
    coreapp_impl_get_Properties,
    coreapp_impl_GetCurrentView,
    coreapp_impl_Run,
    coreapp_impl_RunWithActivationFactories
};

static inline struct coreapp_impl *impl_from_ICoreApplicationView( ICoreApplicationView *iface )
{
    return CONTAINING_RECORD( iface, struct coreapp_impl, ICoreApplicationView_iface );
}

static HRESULT WINAPI coreappview_impl_QueryInterface( ICoreApplicationView *iface, REFIID iid, void **out )
{
    struct coreapp_impl *impl = impl_from_ICoreApplicationView( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreApplicationView ))
    {
        *out = &impl->ICoreApplicationView_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI coreappview_impl_AddRef( ICoreApplicationView *iface )
{
    struct coreapp_impl *impl = impl_from_ICoreApplicationView( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI coreappview_impl_Release( ICoreApplicationView *iface )
{
    struct coreapp_impl *impl = impl_from_ICoreApplicationView( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    /* Singleton retains its initial reference. */
    return ref;
}

static HRESULT WINAPI coreappview_impl_GetIids( ICoreApplicationView *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI coreappview_impl_GetRuntimeClassName( ICoreApplicationView *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI coreappview_impl_GetTrustLevel( ICoreApplicationView *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI coreappview_impl_get_CoreWindow( ICoreApplicationView *iface, __x_ABI_CWindows_CUI_CCore_CICoreWindow **value)
{
    struct coreapp_impl *impl = impl_from_ICoreApplicationView(iface);
    if (!value) return E_POINTER;
    *value = impl->window ? &impl->window->ICoreWindow_iface : NULL;
    if (*value) ICoreWindow_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI coreappview_impl_add_Activated(ICoreApplicationView *iface,
    ITypedEventHandler_CoreApplicationView_IActivatedEventArgs *handler,EventRegistrationToken *token)
{
    struct coreapp_impl *impl=impl_from_ICoreApplicationView(iface);
    struct activation_subscription *p, **tail=&impl->subscriptions;
    if (!token) return E_POINTER;
    token->value=0;
    if (!handler) return E_POINTER;
    if (!impl->running) return E_ILLEGAL_METHOD_CALL;
    if (GetCurrentThreadId()!=impl->owner_thread) return RPC_E_WRONG_THREAD;
    if (!(p=calloc(1,sizeof(*p)))) return E_OUTOFMEMORY;
    p->handler=handler; handler->lpVtbl->AddRef(handler);
    p->token.value=++impl->next_token;
    while (*tail) tail=&(*tail)->next;
    *tail=p; *token=p->token; return S_OK;
}

static HRESULT WINAPI coreappview_impl_remove_Activated(ICoreApplicationView *iface,EventRegistrationToken token)
{
    struct coreapp_impl *impl=impl_from_ICoreApplicationView(iface);
    struct activation_subscription **p=&impl->subscriptions, *removed;
    if (GetCurrentThreadId()!=impl->owner_thread) return RPC_E_WRONG_THREAD;
    while (*p && (*p)->token.value!=token.value) p=&(*p)->next;
    if (!*p) return S_OK;
    removed=*p; *p=removed->next;
    removed->handler->lpVtbl->Release(removed->handler); free(removed); return S_OK;
}

static HRESULT WINAPI coreappview_impl_get_IsMain( ICoreApplicationView *iface, boolean *value)
{
    FIXME("iface %p, value %p stub.\n", iface, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI coreappview_impl_get_IsHosted( ICoreApplicationView *iface, boolean *value)
{
    FIXME("iface %p, value %p stub.\n", iface, value);
    return E_NOTIMPL;
}

static const struct ICoreApplicationViewVtbl coreappview_impl_vtbl =
{
    coreappview_impl_QueryInterface,
    coreappview_impl_AddRef,
    coreappview_impl_Release,
    /* IInspectable methods */
    coreappview_impl_GetIids,
    coreappview_impl_GetRuntimeClassName,
    coreappview_impl_GetTrustLevel,
    /* ICoreApplicationView methods */
    coreappview_impl_get_CoreWindow,
    coreappview_impl_add_Activated,
    coreappview_impl_remove_Activated,
    coreappview_impl_get_IsMain,
    coreappview_impl_get_IsHosted
};



static struct coreapp_impl coreapp_impl_global =
{
    .IActivationFactory_iface.lpVtbl = &factory_vtbl,
    .ICoreApplication_iface.lpVtbl = &coreapp_impl_vtbl,
    .ICoreApplicationView_iface.lpVtbl = &coreappview_impl_vtbl,
    .ref = 1,
};

IActivationFactory *coreapplication_factory = &coreapp_impl_global.IActivationFactory_iface;
