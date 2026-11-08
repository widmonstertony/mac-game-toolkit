#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

static WCHAR *decode_utf8(LPCSTR source, BOOL *has_cjk)
{
    if (!source || IS_INTRESOURCE(source)) return NULL;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, NULL, 0);
    if (!count) return NULL;
    WCHAR *out = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * sizeof(*out));
    if (!out) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, out, count))
    {
        HeapFree(GetProcessHeap(), 0, out);
        return NULL;
    }
    for (int i = 0; i < count; ++i)
        if ((out[i] >= 0x3400 && out[i] <= 0x9fff) ||
            (out[i] >= 0x3040 && out[i] <= 0x30ff))
            *has_cjk = TRUE;
    return out;
}

static int WINAPI utf8_message_box(const MSGBOXPARAMSA *params)
{
    if (!params || params->cbSize != sizeof(*params) ||
        IS_INTRESOURCE(params->lpszText) || IS_INTRESOURCE(params->lpszCaption) ||
        (params->lpszIcon && !IS_INTRESOURCE(params->lpszIcon)))
        return MessageBoxIndirectA(params);

    BOOL has_cjk = FALSE;
    WCHAR *text = decode_utf8(params->lpszText, &has_cjk);
    WCHAR *caption = decode_utf8(params->lpszCaption, &has_cjk);
    int result;
    if (has_cjk && text && caption)
    {
        MSGBOXPARAMSW wide = {
            sizeof(wide), params->hwndOwner, params->hInstance, text, caption,
            params->dwStyle, (LPCWSTR)params->lpszIcon, params->dwContextHelpId,
            params->lpfnMsgBoxCallback, params->dwLanguageId
        };
        result = MessageBoxIndirectW(&wide);
    }
    else
        result = MessageBoxIndirectA(params);
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
        if (lstrcmpiA((LPCSTR)(base + entry[i].Name), "user32.dll") ||
            !entry[i].OriginalFirstThunk || !entry[i].FirstThunk)
            continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(base + entry[i].OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iat = (IMAGE_THUNK_DATA64 *)(base + entry[i].FirstThunk);
        for (SIZE_T j = 0; names[j].u1.AddressOfData; ++j)
        {
            if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)(base + names[j].u1.AddressOfData);
            if (strcmp((const char *)name->Name, "MessageBoxIndirectA")) continue;
            void *volatile *slot = (void *volatile *)&iat[j].u1.Function;
            DWORD protect, ignored;
            if (VirtualProtect((void *)slot, sizeof(*slot), PAGE_READWRITE, &protect))
            {
                InterlockedExchangePointer(slot, (void *)utf8_message_box);
                VirtualProtect((void *)slot, sizeof(*slot), protect, &ignored);
            }
            return;
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
