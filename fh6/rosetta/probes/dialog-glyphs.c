/* Checks actual Wine GDI glyph coverage, without displaying a window. */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    const char utf8[] = "\xe5\x9c\xb0\xe5\xb9\xb3\xe7\xba\xbf";
    WCHAR decoded[16] = {0};
    int count = MultiByteToWideChar(CP_ACP, 0, utf8, -1, decoded, 16);
    int correct = count == 4 && decoded[0] == 0x5730 && decoded[1] == 0x5e73 && decoded[2] == 0x7ebf;
    printf("ANSI dialog ACP=%u UTF8_decoded_correctly=%d\n", GetACP(), correct);
    int failures = 0;
    if (MultiByteToWideChar(CP_ACP, 0, "ASCII", -1, decoded, 16) != 6 || decoded[0] != 'A') ++failures;
    if (MultiByteToWideChar(1252, 0, "\xe9", 1, decoded, 16) != 1 || decoded[0] != 0xe9) ++failures;
    if (MultiByteToWideChar(936, 0, "\xd6\xd0\xce\xc4", 4, decoded, 16) != 2 ||
        decoded[0] != 0x4e2d || decoded[1] != 0x6587) ++failures;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, "\xff", 1, decoded, 16) != 0 ||
        GetLastError() != ERROR_NO_UNICODE_TRANSLATION) ++failures;
    if (MultiByteToWideChar(CP_ACP, 0, utf8, -1, decoded, 1) != 0 ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) ++failures;
    typedef LONG (WINAPI *rtl_size)(DWORD *, const char *, DWORD);
    typedef LONG (WINAPI *rtl_convert)(WCHAR *, DWORD, DWORD *, const char *, DWORD);
    rtl_size sizefn = (rtl_size)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlMultiByteToUnicodeSize");
    rtl_convert convertfn = (rtl_convert)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlMultiByteToUnicodeN");
    DWORD size = 0, written = 0;
    if (!sizefn || !convertfn || sizefn(&size, utf8, 9) || size != 6 ||
        convertfn(decoded, sizeof(decoded), &written, utf8, 9) || written != size || decoded[0] != 0x5730)
        ++failures;
    printf("conversion compatibility failures=%d\n", failures);
    const WCHAR text[] = L"\x5730\x5e73\x7ebf\x517c\x5bb9\x6027\x8b66\x544a";
    HDC dc = CreateCompatibleDC(NULL);
    HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, 0, 0, 0, 0, L"MS Shell Dlg");
    if (!dc || !font) return 2;
    HGDIOBJ previous = SelectObject(dc, font);
    WCHAR face[128];
    char utf8face[512];
    GetTextFaceW(dc, 128, face);
    WideCharToMultiByte(CP_UTF8, 0, face, -1, utf8face, sizeof(utf8face), NULL, NULL);
    printf("selected font=%s\n", utf8face);
    WORD glyphs[8];
    DWORD result = GetGlyphIndicesW(dc, text, 8, glyphs, GGI_MARK_NONEXISTING_GLYPHS);
    int missing = 0;
    for (int i = 0; i < 8; ++i) missing += glyphs[i] == 0xffff;
    printf("dialog glyphs=%lu missing=%d\n", result, missing);
    SelectObject(dc, previous);
    DeleteObject(font);
    DeleteDC(dc);
    return result != 8 || missing || !correct || failures;
}
