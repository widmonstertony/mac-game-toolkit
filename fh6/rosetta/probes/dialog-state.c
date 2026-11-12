/* Read-only diagnostic for the current FH6 compatibility dialog. */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
static void print_wide(const WCHAR *value)
{
    char out[8192];
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, out, sizeof(out), NULL, NULL)) printf("%s", out);
}
static BOOL CALLBACK child(HWND window, LPARAM unused)
{
    WCHAR cls[256] = {0}, text[2048] = {0};
    DWORD_PTR result = 0;
    GetClassNameW(window, cls, 256);
    SendMessageTimeoutW(window, WM_GETTEXT, 2048, (LPARAM)text, SMTO_ABORTIFHUNG, 200, &result);
    printf("class="); print_wide(cls); printf(" text="); print_wide(text); printf("\n");
    if (SendMessageTimeoutW(window, WM_GETFONT, 0, 0, SMTO_ABORTIFHUNG, 200, &result) && result)
    {
        LOGFONTW font;
        if (GetObjectW((HFONT)result, sizeof(font), &font))
        {
            printf("font="); print_wide(font.lfFaceName);
            printf(" charset=%u height=%ld\n", font.lfCharSet, font.lfHeight);
        }
    }
    return TRUE;
}
static BOOL CALLBACK top(HWND window, LPARAM unused)
{
    DWORD pid; WCHAR path[4096]; DWORD length = 4096;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    BOOL ok = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!ok || (!wcsstr(path, L"forzahorizon6.exe") && !wcsstr(path, L"ForzaHorizon6.exe"))) return TRUE;
    if (IsWindowVisible(window)) { child(window, 0); EnumChildWindows(window, child, 0); }
    return TRUE;
}
int main(void) { EnumWindows(top, 0); return 0; }
