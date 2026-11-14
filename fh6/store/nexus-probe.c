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

static int endpoint_policy(void)
{
    HMODULE module = LoadLibraryW(L"xgameruntime.dll");
    DWORD (WINAPI *test)(void) = module ? (void *)GetProcAddress(module, "Fh6ProbeEndpointPolicy") : NULL;
    DWORD result = test ? test() : 0;
    printf("Endpoint policy parser: result=%08lx\n", result);
    if (module) FreeLibrary(module);
    return result != 1;
}

static BOOLEAN CALLBACK collect_catalog(const XStoreProduct *product, void *context)
{
    UINT32 *count = context;
    if (!product || strcmp(product->storeId, "9PKWSPL4R607") ||
        product->productKind != XStoreProductKind_Durable || product->isInUserCollection ||
        product->hasDigitalDownload || strcmp(product->inAppOfferToken, "1972_Datsun_240Z_Carbon") ||
        product->skusCount != 1 || strcmp(product->skus[0].skuId, "0010")) {
        *count = ~0u;
        return FALSE;
    }
    ++*count;
    return TRUE;
}

static int xstore(BOOL unrelated, BOOL addons, BOOL catalog)
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
    UINT32 addon_count = ~0u;
    HRESULT hr = E_FAIL, status = E_FAIL;
    int passed = 0;

    if (!module) goto done;
    initialize = (void *)GetProcAddress(module, "InitializeApiImplEx2");
    query = (void *)GetProcAddress(module, "QueryApiImpl");
    if (!initialize || !query || FAILED(hr = initialize(0, 0, 0, &options)) ||
        FAILED(hr = query(&CLSID_XStoreImpl, &IID_IXStoreImpl6, (void **)&store)) ||
        FAILED(hr = query(&CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&threading)) ||
        FAILED(hr = IXStoreImpl6_XStoreCreateContext(store, NULL, &context))) goto done;
    if (catalog) {
        const char *ids[] = {"9PKWSPL4R607", "9ZZZZZZZZZZZ"};
        XStoreProductQueryHandle products = NULL;
        addon_count = 0;
        hr = IXStoreImpl6_XStoreQueryProductsAsync(store, context, XStoreProductKind_Durable,
                ids, sizeof(ids) / sizeof(ids[0]), NULL, 0, &async);
        if (SUCCEEDED(hr)) status = IXThreadingImpl_XAsyncGetStatus(threading, &async, TRUE);
        if (SUCCEEDED(status)) hr = IXStoreImpl6_XStoreQueryProductsResult(store, &async, &products);
        if (SUCCEEDED(hr) && products) {
            hr = IXStoreImpl6_XStoreEnumerateProductsQuery(store, products, &addon_count, collect_catalog);
            passed = SUCCEEDED(hr) && addon_count == (unrelated ? 0 : 1);
            IXStoreImpl6_XStoreCloseProductsQueryHandle(store, products);
        }
        goto done;
    }
    if (addons) {
        hr = IXStoreImpl6_XStoreQueryAddOnLicensesAsync(store, context, &async);
        if (SUCCEEDED(hr)) {
            status = IXThreadingImpl_XAsyncGetStatus(threading, &async, TRUE);
            hr = IXStoreImpl6_XStoreQueryAddOnLicensesResultCount(store, &async, &addon_count);
            if (unrelated && SUCCEEDED(hr) && addon_count == 0)
                passed = SUCCEEDED(status) && SUCCEEDED(
                    IXStoreImpl6_XStoreQueryAddOnLicensesResult(store, &async, 0, NULL));
            else if (!unrelated)
                passed = FAILED(status) && status == hr && addon_count == 0;
        }
        goto done;
    }
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
    printf("XStore unrelated=%u addons=%u catalog=%u count=%u hr=0x%08lx status=0x%08lx passed=%u\n",
           unrelated, addons, catalog, addon_count, hr, status, passed);
    if (context) IXStoreImpl6_XStoreCloseContextHandle(store, context);
    if (threading) IXThreadingImpl_Release(threading);
    if (store) IXStoreImpl6_Release(store);
    return !passed;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "http2")) return http2();
    if (!strcmp(argv[1], "endpoint-policy")) return endpoint_policy();
    if (!strcmp(argv[1], "durable")) return xstore(FALSE, FALSE, FALSE);
    if (!strcmp(argv[1], "unrelated")) return xstore(TRUE, FALSE, FALSE);
    if (!strcmp(argv[1], "addons")) return xstore(FALSE, TRUE, FALSE);
    if (!strcmp(argv[1], "unrelated-addons")) return xstore(TRUE, TRUE, FALSE);
    if (!strcmp(argv[1], "catalog")) return xstore(FALSE, FALSE, TRUE);
    if (!strcmp(argv[1], "unrelated-catalog")) return xstore(TRUE, FALSE, TRUE);
    return 2;
}
