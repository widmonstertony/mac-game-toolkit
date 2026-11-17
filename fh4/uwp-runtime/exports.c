/* LGPL-2.1-or-later. Isolated prototype, not a system DLL replacement. */
#define INITGUID
#include "private.h"
#include <roapi.h>
#include <xmllite.h>
__declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(HSTRING name, IActivationFactory **out)
{
    const WCHAR *s;
    if (!out) return E_POINTER;
    *out = NULL;
    s = WindowsGetStringRawBuffer(name,NULL);
    if (!wcscmp(s,L"Windows.ApplicationModel.Core.CoreApplication")) *out = coreapplication_factory;
    else if (!wcscmp(s,L"Windows.UI.Core.CoreWindow")) *out = corewindow_factory;
    else return CLASS_E_CLASSNOTAVAILABLE;
    IActivationFactory_AddRef(*out);
    return S_OK;
}
