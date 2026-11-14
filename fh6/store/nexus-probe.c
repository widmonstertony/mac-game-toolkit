/* Probe the isolated Nexus DLLs. No account signs in and no game is launched. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include "xstore.h"
#include "xasyncprovider.h"

static int http2(void)
{
    DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2, actual = 0, status = 0;
    DWORD size = sizeof(actual);
    HINTERNET session, connection = NULL, request = NULL;
    int passed = 0;

    session = WinHttpOpen(L"FH6 Nexus regression", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (!session || !WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000) ||
        !WinHttpSetOption(session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols))) goto done;
    connection = WinHttpConnect(session, L"displaycatalog.mp.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) goto done;
    request = WinHttpOpenRequest(connection, L"GET",
        L"/v7.0/products?bigIds=9NTBFBZMB84T&market=US&languages=en-us",
        NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!request || !WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryOption(request, WINHTTP_OPTION_HTTP_PROTOCOL_USED, &actual, &size)) goto done;
    size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &status, &size, NULL)) goto done;
    passed = status == 200 && (actual & WINHTTP_PROTOCOL_FLAG_HTTP2);
done:
    printf("HTTP status=%lu protocol=0x%lx winerr=%lu passed=%u\n", status, actual, GetLastError(), passed);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return !passed;
}

struct initialize_options { UINT32 unknown; BOOL is_inline; const char *config; };

static int xstore(BOOL unrelated)
{
    struct initialize_options options = {0, TRUE, unrelated ?
        "<Game><StoreId>9NTBFBZMB84T</StoreId></Game>" :
        "<Game><Identity Name=\"Microsoft.ForteBaseGame\" Publisher=\"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US\"/>"
        "<StoreId>9NR1R1XWLCNB</StoreId></Game>"};
    HMODULE module = LoadLibraryW(L"xgameruntime.dll");
    HRESULT (WINAPI *initialize)(ULONG, ULONG, char, const struct initialize_options *);
    HRESULT (WINAPI *query)(const GUID *, REFIID, void **);
    IXStoreImpl6 *store = NULL;
    IXThreadingImpl *threading = NULL;
    XStoreContextHandle context = NULL;
    XAsyncBlock async = {0};
    XStoreLicenseHandle license = NULL;
    HRESULT hr = E_FAIL, status = E_FAIL;
    int passed = 0;

    if (!module) goto done;
    initialize = (void *)GetProcAddress(module, "InitializeApiImplEx2");
    query = (void *)GetProcAddress(module, "QueryApiImpl");
    if (!initialize || !query || FAILED(hr = initialize(0, 0, 0, &options)) ||
        FAILED(hr = query(&CLSID_XStoreImpl, &IID_IXStoreImpl6, (void **)&store)) ||
        FAILED(hr = query(&CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&threading)) ||
        FAILED(hr = IXStoreImpl6_XStoreCreateContext(store, NULL, &context))) goto done;
    hr = IXStoreImpl6_XStoreAcquireLicenseForDurablesAsync(store, context, "9NTBFBZMB84T", &async);
    if (unrelated) {
        passed = hr == E_NOTIMPL;
    } else if (SUCCEEDED(hr)) {
        status = IXThreadingImpl_XAsyncGetStatus(threading, &async, TRUE);
        hr = IXStoreImpl6_XStoreAcquireLicenseForDurablesResult(store, &async, &license);
        passed = FAILED(status) && FAILED(hr) && !license;
        if (license) IXStoreImpl6_XStoreCloseLicenseHandle(store, license);
    }
done:
    printf("XStore unrelated=%u hr=0x%08lx status=0x%08lx passed=%u\n", unrelated, hr, status, passed);
    if (context) IXStoreImpl6_XStoreCloseContextHandle(store, context);
    if (threading) IXThreadingImpl_Release(threading);
    if (store) IXStoreImpl6_Release(store);
    return !passed;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "http2")) return http2();
    if (!strcmp(argv[1], "durable")) return xstore(FALSE);
    if (!strcmp(argv[1], "unrelated")) return xstore(TRUE);
    return 2;
}
