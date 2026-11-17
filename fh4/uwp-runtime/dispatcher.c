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

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dispatcher);

struct dispatcher_queued_task {
    dispatcher_func func;
    void* return_value;
};

// this is the main message handler for the program
static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // sort through and find what code to run for the message given
    switch(message)
    {
        case FH4_DISPATCH_MESSAGE:
            if(GetWindowLongPtrW(hWnd,GWLP_USERDATA))
                dispatcher_dispatch_one((struct dispatcher_impl*)GetWindowLongPtrW(hWnd,GWLP_USERDATA));
            return 0;
        // this message is read when the window is closed
        case WM_DESTROY:
            {
                // close the application entirely
                if (GetWindowLongPtrW(hWnd, GWLP_USERDATA)) {
                    dispatcher_stop((struct dispatcher_impl*)GetWindowLongPtrW(hWnd,GWLP_USERDATA));
                    SetWindowLongPtrW(hWnd,GWLP_USERDATA,0);PostQuitMessage(0);
                }
                return 0;
            } break;
        case WM_QUIT:
            return wParam;
    }

    // Handle any messages the switch statement didn't
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static inline struct dispatcher_impl *impl_from_ICoreDispatcher(ICoreDispatcher *iface)
{
    return CONTAINING_RECORD(iface, struct dispatcher_impl, ICoreDispatcher_iface);
}

static HRESULT WINAPI dispatcher_impl_QueryInterface( ICoreDispatcher *iface, REFIID iid, void **out )
{
    struct dispatcher_impl *impl = impl_from_ICoreDispatcher( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreDispatcher ))
    {
        *out = &impl->ICoreDispatcher_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dispatcher_impl_AddRef( ICoreDispatcher *iface )
{
    struct dispatcher_impl *impl = impl_from_ICoreDispatcher( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI dispatcher_impl_Release( ICoreDispatcher *iface )
{
    struct dispatcher_impl *impl = impl_from_ICoreDispatcher( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) {dispatcher_stop(impl);free( impl );}
    return ref;
}

static HRESULT WINAPI dispatcher_impl_GetIids( ICoreDispatcher *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI dispatcher_impl_GetRuntimeClassName( ICoreDispatcher *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI dispatcher_impl_GetTrustLevel( ICoreDispatcher *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI dispatcher_impl_get_HasThreadAccess( ICoreDispatcher *iface, boolean *value)
{
    if (!value) return E_POINTER;
    *value = GetCurrentThreadId() == impl_from_ICoreDispatcher(iface)->owner_thread;
    return S_OK;
}

static HRESULT WINAPI dispatcher_impl_ProcessEvents( ICoreDispatcher *iface, CoreProcessEventsOption options)
{
    MSG msg;
    BOOL have;
    struct dispatcher_impl *impl = impl_from_ICoreDispatcher(iface);
    if (GetCurrentThreadId() != impl->owner_thread) return RPC_E_WRONG_THREAD;
    if (options != CoreProcessEventsOption_ProcessOneAndAllPending &&
        options != CoreProcessEventsOption_ProcessOneIfPresent &&
        options != CoreProcessEventsOption_ProcessAllIfPresent &&
        options != CoreProcessEventsOption_ProcessUntilQuit) return E_INVALIDARG;
    if (impl->stopped) return S_OK;
    for (;;)
    {
        if (options == CoreProcessEventsOption_ProcessUntilQuit ||
            options == CoreProcessEventsOption_ProcessOneAndAllPending)
        {
            int result = GetMessageW(&msg, NULL, 0, 0);
            if (result == -1) return HRESULT_FROM_WIN32(GetLastError());
            have = TRUE;
        }
        else have = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        if (!have) break;
        if (msg.message == WM_QUIT) { dispatcher_stop(impl); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (options == CoreProcessEventsOption_ProcessOneIfPresent) break;
        if (options == CoreProcessEventsOption_ProcessOneAndAllPending)
            options = CoreProcessEventsOption_ProcessAllIfPresent;
    }
    return S_OK;
}

static HRESULT WINAPI dispatcher_impl_RunAsync( ICoreDispatcher *iface, CoreDispatcherPriority priority, IDispatchedHandler *callback, IAsyncAction **action)
{
    return dispatcher_queue_action(impl_from_ICoreDispatcher(iface),priority,callback,action);
}

static HRESULT WINAPI dispatcher_impl_RunIdleAsync( ICoreDispatcher *iface, IIdleDispatchedHandler *callback, IAsyncAction **action)
{
    FIXME("iface %p, callback %p, action %p, stub.\n", iface, callback, action);
    return E_NOTIMPL;
}

static const struct ICoreDispatcherVtbl dispatcher_impl_vtbl =
{
    /* IUnknown methods */
    dispatcher_impl_QueryInterface,
    dispatcher_impl_AddRef,
    dispatcher_impl_Release,
    /* IInspectable methods */
    dispatcher_impl_GetIids,
    dispatcher_impl_GetRuntimeClassName,
    dispatcher_impl_GetTrustLevel,
    /* ICoreDispatcher methods */
    dispatcher_impl_get_HasThreadAccess,
    dispatcher_impl_ProcessEvents,
    dispatcher_impl_RunAsync,
    dispatcher_impl_RunIdleAsync,
};

LPWSTR create_wide_string(char* regular_string) {
    LPWSTR widestr;

    int count = MultiByteToWideChar(CP_UTF8, 0, regular_string, -1, NULL, 0);
    if (!count) return NULL;
    widestr = calloc(count, sizeof(WCHAR));
    if (!widestr) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, regular_string, -1, widestr, count);
    return widestr;
}

struct dispatcher_impl *create_dispatcher(struct corewindow_impl *for_window, char *identity_name, char *display_name) {
    struct dispatcher_impl *object;
    WNDCLASSEXW wc;
    LPWSTR widentity_name;
    LPWSTR wdisplay_name;

    TRACE("for_view %p.\n", for_window);

    widentity_name = create_wide_string(identity_name);
    wdisplay_name = create_wide_string(display_name);

    if (!widentity_name || !wdisplay_name || !(object = calloc(1, sizeof(*object))))
    { free(widentity_name); free(wdisplay_name); return NULL; }


    object->ICoreDispatcher_iface.lpVtbl = &dispatcher_impl_vtbl;
    object->owner_thread = GetCurrentThreadId();
    object->ref = 1;


    ZeroMemory(&wc, sizeof(WNDCLASSEXW));

    // fill in the struct with the needed information
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = NULL;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = widentity_name;

    // register the window class
    RegisterClassExW(&wc);

    for_window->window_handle = CreateWindowExW(WS_EX_APPWINDOW, widentity_name, wdisplay_name, WS_OVERLAPPEDWINDOW, 0, 0, 1920, 1080, NULL, NULL, NULL, NULL);
    if (!for_window->window_handle) { free(object); object = NULL; }
    else {object->window=for_window->window_handle;SetWindowLongPtrW(object->window,GWLP_USERDATA,(LONG_PTR)object);}
    free(widentity_name); free(wdisplay_name);

    return object;
}
