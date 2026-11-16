// Diagnostic forwarding DLL. The original Intel binary remains unmodified.
// All exports except GetLastPresentStatus are PE forwarders generated at build.
// This function returns the SDK's status without changing it or any GPU work.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "xess-fg-sdk/xefg_swapchain.h"

static HMODULE module;
static INIT_ONCE initialize = INIT_ONCE_STATIC_INIT;
static decltype(&xefgSwapChainGetLastPresentStatus) getStatus;
static HANDLE logFile = INVALID_HANDLE_VALUE;
static SRWLOCK logLock = SRWLOCK_INIT;
static unsigned long long queries, doubles, errors;
static unsigned int lines;

static BOOL CALLBACK init(PINIT_ONCE, PVOID, PVOID*) {
    static wchar_t path[32768];
    DWORD n = GetModuleFileNameW(module, path, 32768);
    if (!n || n >= 32700) return TRUE;
    while (n && path[n - 1] != L'\\') --n;
    path[n] = 0;
    lstrcatW(path, L"xefg_intel_real.dll");
    const auto real = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (real) getStatus = reinterpret_cast<decltype(getStatus)>(
        GetProcAddress(real, "xefgSwapChainGetLastPresentStatus"));
    path[n] = 0;
    lstrcatW(path, L"XeFG-present-status.jsonl");
    logFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return TRUE;
}

extern "C" xefg_swapchain_result_t observedGetLastPresentStatus(
    xefg_swapchain_handle_t context, xefg_swapchain_present_status_t* status) {
    const DWORD incomingError = GetLastError();
    InitOnceExecuteOnce(&initialize, init, nullptr, nullptr);
    if (!getStatus) return XEFG_SWAPCHAIN_RESULT_ERROR_CANT_LOAD_LIBRARY;
    SetLastError(incomingError);
    const auto result = getStatus(context, status);
    const DWORD savedError = GetLastError();
    if (TryAcquireSRWLockExclusive(&logLock)) {
        ++queries;
        const bool valid = result == XEFG_SWAPCHAIN_RESULT_SUCCESS && status;
        if (!valid) ++errors;
        if (valid && status->framesPresented == 2 && status->frameGenResult == 0
                  && status->isFrameGenEnabled) ++doubles;
        // The counters count queried SDK statuses, not unique presents or FPS.
        // Bound I/O to 100 small records per process; never change SDK logging.
        if (logFile != INVALID_HANDLE_VALUE && lines < 100
            && (queries <= 8 || queries % 128 == 0)) {
            char line[512];
            const int length = wsprintfA(line,
                "{\"pid\":%lu,\"context\":\"%p\",\"tickMs\":%I64u,"
                "\"statusQueries\":%I64u,\"queriesReportingTwoFrames\":%I64u,"
                "\"queryErrors\":%I64u,\"result\":%d,\"framesPresented\":%u,"
                "\"frameGenResult\":%d,\"enabled\":%u}\n",
                GetCurrentProcessId(), context, GetTickCount64(), queries, doubles,
                errors, int(result), valid ? status->framesPresented : 0,
                valid ? int(status->frameGenResult) : int(result),
                valid ? status->isFrameGenEnabled : 0);
            DWORD written;
            if (length > 0 && length < int(sizeof(line)))
                WriteFile(logFile, line, length, &written, nullptr);
            ++lines;
        }
        ReleaseSRWLockExclusive(&logLock);
    }
    SetLastError(savedError);
    return result;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Retain a PE import dependency so the normal loader finds the real DLL
        // beside this proxy before a caller resolves any forwarded export.
        // This takes an imported function address; it does not call the SDK.
        auto volatile dependency = &xefgSwapChainGetVersion;
        (void)dependency;
        module = instance;
        DisableThreadLibraryCalls(instance);
    }
    // No waits, graphics calls, logging, or dependency loads under loader lock.
    return TRUE;
}
