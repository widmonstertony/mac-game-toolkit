#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

static BOOL contains_cjk(LPCWSTR text, int count)
{
    for (int i = 0; i < count; ++i)
        if ((text[i] >= 0x3400 && text[i] <= 0x9fff) ||
            (text[i] >= 0x3040 && text[i] <= 0x30ff))
            return TRUE;
    return FALSE;
}

static WCHAR *decode_utf8(LPCSTR source, int length)
{
    if (!source || IS_INTRESOURCE(source)) return NULL;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, length, NULL, 0);
    if (!count) return NULL;
    WCHAR *out = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * sizeof(*out));
    if (!out) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, length, out, count) ||
        !contains_cjk(out, count))
    {
        HeapFree(GetProcessHeap(), 0, out);
        return NULL;
    }
    return out;
}

static WCHAR *decode_mojibake(LPCWSTR source)
{
    if (!source || IS_INTRESOURCE(source)) return NULL;
    BOOL replaced = FALSE;
    int count = WideCharToMultiByte(1252, WC_NO_BEST_FIT_CHARS, source, -1, NULL, 0, NULL, &replaced);
    if (!count || replaced) return NULL;
    char *bytes = HeapAlloc(GetProcessHeap(), 0, count);
    if (!bytes) return NULL;
    WCHAR *out = NULL;
    if (WideCharToMultiByte(1252, WC_NO_BEST_FIT_CHARS, source, -1, bytes, count, NULL, &replaced) &&
        !replaced)
        out = decode_utf8(bytes, -1);
    HeapFree(GetProcessHeap(), 0, bytes);
    return out;
}

static int WINAPI utf8_to_wide(UINT page, DWORD flags, LPCCH source, int length, LPWSTR result, int size)
{
    if (page == CP_ACP || page == CP_THREAD_ACP || page == 1252)
    {
        DWORD previous = GetLastError();
        WCHAR *decoded = decode_utf8(source, length);
        if (decoded)
        {
            HeapFree(GetProcessHeap(), 0, decoded);
            SetLastError(previous);
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, length, result, size);
        }
        SetLastError(previous);
    }
    return MultiByteToWideChar(page, flags, source, length, result, size);
}

static HWND WINAPI utf8_window(DWORD ex_style, LPCWSTR class_name, LPCWSTR title, DWORD style,
        int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    WCHAR *decoded = decode_mojibake(title);
    HWND result = CreateWindowExW(ex_style, class_name, decoded ? decoded : title, style,
                                  x, y, width, height, parent, menu, instance, param);
    DWORD previous = GetLastError();
    if (decoded) HeapFree(GetProcessHeap(), 0, decoded);
    SetLastError(previous);
    return result;
}

static LRESULT WINAPI utf8_window_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    WCHAR *decoded = message == WM_SETTEXT ? decode_mojibake((LPCWSTR)lparam) : NULL;
    LRESULT result = SendMessageW(window, message, wparam, decoded ? (LPARAM)decoded : lparam);
    DWORD previous = GetLastError();
    if (decoded) HeapFree(GetProcessHeap(), 0, decoded);
    SetLastError(previous);
    return result;
}

static int WINAPI utf8_message_box(const MSGBOXPARAMSA *params)
{
    if (!params || params->cbSize != sizeof(*params) ||
        IS_INTRESOURCE(params->lpszText) || IS_INTRESOURCE(params->lpszCaption) ||
        (params->lpszIcon && !IS_INTRESOURCE(params->lpszIcon)))
        return MessageBoxIndirectA(params);

    WCHAR *text = decode_utf8(params->lpszText, -1);
    WCHAR *caption = decode_utf8(params->lpszCaption, -1);
    int result;
    if (text || caption)
    {
        WCHAR fallback_text[2048], fallback_caption[256];
        if (!text && !MultiByteToWideChar(CP_ACP, 0, params->lpszText, -1, fallback_text, ARRAYSIZE(fallback_text)))
            goto fallback;
        if (!caption && !MultiByteToWideChar(CP_ACP, 0, params->lpszCaption, -1, fallback_caption, ARRAYSIZE(fallback_caption)))
            goto fallback;
        MSGBOXPARAMSW wide = {
            sizeof(wide), params->hwndOwner, params->hInstance, text ? text : fallback_text,
            caption ? caption : fallback_caption,
            params->dwStyle, (LPCWSTR)params->lpszIcon, params->dwContextHelpId,
            params->lpfnMsgBoxCallback, params->dwLanguageId
        };
        result = MessageBoxIndirectW(&wide);
    }
    else {
fallback:
        result = MessageBoxIndirectA(params);
    }
    if (text) HeapFree(GetProcessHeap(), 0, text);
    if (caption) HeapFree(GetProcessHeap(), 0, caption);
    return result;
}

static void install(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) return;
    IMAGE_IMPORT_DESCRIPTOR *entry = (IMAGE_IMPORT_DESCRIPTOR *)(base + imports.VirtualAddress);
    for (SIZE_T i = 0; i < imports.Size / sizeof(*entry) && entry[i].Name; ++i)
    {
        const char *library = (const char *)(base + entry[i].Name);
        BOOL user = !lstrcmpiA(library, "user32.dll");
        BOOL kernel = !lstrcmpiA(library, "kernel32.dll") || !lstrcmpiA(library, "kernelbase.dll");
        if ((!user && !kernel) || !entry[i].OriginalFirstThunk || !entry[i].FirstThunk)
            continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(base + entry[i].OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iat = (IMAGE_THUNK_DATA64 *)(base + entry[i].FirstThunk);
        for (SIZE_T j = 0; names[j].u1.AddressOfData; ++j)
        {
            if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)(base + names[j].u1.AddressOfData);
            const char *function = (const char *)name->Name;
            void *replacement = NULL;
            if (user && !strcmp(function, "MessageBoxIndirectA")) replacement = (void *)utf8_message_box;
            if (user && !strcmp(function, "CreateWindowExW")) replacement = (void *)utf8_window;
            if (user && !strcmp(function, "SendMessageW")) replacement = (void *)utf8_window_message;
            if (kernel && !strcmp(function, "MultiByteToWideChar")) replacement = (void *)utf8_to_wide;
            if (!replacement) continue;
            void *volatile *slot = (void *volatile *)&iat[j].u1.Function;
            DWORD protect, ignored;
            if (VirtualProtect((void *)slot, sizeof(*slot), PAGE_READWRITE, &protect))
            {
                InterlockedExchangePointer(slot, replacement);
                VirtualProtect((void *)slot, sizeof(*slot), protect, &ignored);
            }
        }
    }
}

DWORD WINAPI version_info_size(LPCWSTR file, LPDWORD handle)
{
    install();
    typedef DWORD (WINAPI *query)(LPCWSTR, LPDWORD);
    query original = (query)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetFileVersionInfoSizeW");
    return original ? original(file, handle) : 0;
}

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID reserved)
{
    (void)dll;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) install();
    return TRUE;
}
