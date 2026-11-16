// Read only the diagnostic proxy's counters. No debugger attach, suspension,
// remote calls, or writes. RVAs apply ONLY to the verified v3 proxy hash
// 7125376043934d4385860425ede5b3db2786081324e441c5464d0150df1982b3.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
extern "C" void entry() {
    const wchar_t* arg = GetCommandLineW();
    if (*arg == L'"') { ++arg; while (*arg && *arg != L'"') ++arg; if (*arg) ++arg; }
    else while (*arg && *arg != L' ') ++arg;
    while (*arg == L' ') ++arg;
    DWORD pid = 0;
    while (*arg >= L'0' && *arg <= L'9') pid = pid * 10 + (*arg++ - L'0');
    if (!pid) ExitProcess(2);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) ExitProcess(3);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    MODULEENTRY32W m = {}; m.dwSize = sizeof(m);
    bool found = false;
    if (Module32FirstW(snap, &m)) do {
        if (!lstrcmpiW(m.szModule, L"libxess_fg.dll")) { found = true; break; }
    } while (Module32NextW(snap, &m));
    if (!found) ExitProcess(4);
    // Four independently updated counters: not an atomic frame snapshot.
    struct Counters { DWORD lines, padding; unsigned long long errors, doubles, queries; } c = {};
    SIZE_T bytes = 0;
    if (!ReadProcessMemory(process, m.modBaseAddr + 0x16000, &c, sizeof(c), &bytes)
        || bytes != sizeof(c)) ExitProcess(5);
    char out[512]; DWORD written = 0;
    int n = wsprintfA(out, "{\"pid\":%lu,\"observerLines\":%lu,\"queryErrors\":%I64u,\"queriesReportingTwoFrames\":%I64u,\"statusQueries\":%I64u}\n",
                      pid, c.lines, c.errors, c.doubles, c.queries);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out, n, &written, nullptr);
    CloseHandle(snap); CloseHandle(process); ExitProcess(0);
}
