/* LGPL-2.1-or-later. Isolated prototype, not a system DLL replacement. */
#define INITGUID
#include "private.h"
#include <roapi.h>
#include <xmllite.h>
/* Use unbuffered writes here: the original game can hold a CRT lock while a
 * second thread activates WinRT. Diagnostics must not wait for that lock. */
static void activation_stage(const char *s, DWORD bytes)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),s,bytes,&written,NULL);
}
#define STAGE(s) activation_stage(s "\n", sizeof(s "\n")-1)
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void *reserved)
{
    if(reason==DLL_PROCESS_ATTACH) STAGE("FH4_UWP_PROCESS_ATTACH");
    return TRUE;
}
__declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(HSTRING name, IActivationFactory **out)
{
    const WCHAR *s;
    STAGE("FH4_UWP_FACTORY_ENTER");
    if (!out) return E_POINTER;
    *out = NULL;
    s = WindowsGetStringRawBuffer(name,NULL);
    if (!wcscmp(s,L"Windows.ApplicationModel.Core.CoreApplication")) *out = coreapplication_factory;
    else if (!wcscmp(s,L"Windows.UI.Core.CoreWindow")) *out = corewindow_factory;
    else return CLASS_E_CLASSNOTAVAILABLE;
    IActivationFactory_AddRef(*out);
    STAGE("FH4_UWP_FACTORY_RETURN");
    return S_OK;
}
