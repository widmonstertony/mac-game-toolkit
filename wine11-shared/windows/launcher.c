#define UNICODE
#define _UNICODE
#include <windows.h>

#ifndef YYB_URL
#define YYB_URL L"yybbridge://launch"
#endif

#ifndef YYB_TITLE
#define YYB_TITLE L"Application Treasure Wine 11"
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;

    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    WCHAR command[] = L"C:\\windows\\system32\\winebrowser.exe " YYB_URL;
    startup.cb = sizeof(startup);

    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        MessageBoxW(NULL, L"Unable to open the Wine 11 bridge.", YYB_TITLE, MB_OK | MB_ICONERROR);
        return (int)GetLastError();
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
