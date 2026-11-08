#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

/* The documented PlayFab Lobby ABI starts with the event type. The selected
 * completion events put their HRESULT immediately after it. */
typedef struct { uint32_t type; } LobbyEvent;
typedef struct { LobbyEvent event; HRESULT result; } LobbyCompletion;
typedef HRESULT (WINAPI *LobbyPoll)(void *, uint32_t *, const LobbyEvent *const **);
typedef HRESULT (WINAPI *LobbyJoin)(void *, const void *, const char *, const void *, void *, void **);
typedef HRESULT (WINAPI *LobbyFind)(void *, const void *, const void *, void *);
typedef HRESULT (WINAPI *MultiplayerInit)(const char *, void **);

static LobbyPoll poll_lobby;
static LobbyJoin join_lobby, join_arranged;
static LobbyFind find_lobbies;
static MultiplayerInit initialize_multiplayer;
static HANDLE log_file = INVALID_HANDLE_VALUE;
static LONG log_lines;
static HMODULE self;

static void report(const char *operation, uint32_t kind, HRESULT result)
{
    if (log_file == INVALID_HANDLE_VALUE || InterlockedIncrement(&log_lines) > 1024) return;
    DWORD previous = GetLastError(), written;
    SYSTEMTIME time;
    GetLocalTime(&time);
    char line[192];
    int length = snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u pid=%lu %s kind=%u result=0x%08lx\r\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId(),
        operation, kind, (unsigned long)result);
    if (length > 0 && (size_t)length < sizeof(line)) WriteFile(log_file, line, length, &written, NULL);
    SetLastError(previous);
}

static HRESULT WINAPI trace_init(const char *title, void **handle)
{
    HRESULT result = initialize_multiplayer(title, handle);
    report("multiplayer initialize", 0, result);
    return result;
}

static HRESULT WINAPI trace_join(void *handle, const void *member, const char *connection,
        const void *config, void *context, void **lobby)
{
    HRESULT result = join_lobby(handle, member, connection, config, context, lobby);
    report("join request", 0, result);
    return result;
}

static HRESULT WINAPI trace_arranged(void *handle, const void *member, const char *arrangement,
        const void *config, void *context, void **lobby)
{
    HRESULT result = join_arranged(handle, member, arrangement, config, context, lobby);
    report("join arranged request", 0, result);
    return result;
}

static HRESULT WINAPI trace_find(void *handle, const void *entity, const void *config, void *context)
{
    HRESULT result = find_lobbies(handle, entity, config, context);
    report("find request", 0, result);
    return result;
}

static HRESULT WINAPI trace_poll(void *handle, uint32_t *count, const LobbyEvent *const **events)
{
    HRESULT result = poll_lobby(handle, count, events);
    if (FAILED(result)) {
        report("lobby poll failed", 0, result);
        return result;
    }
    if (!count || !events || !*events) return result;
    for (uint32_t i = 0; i < *count; ++i) {
        const LobbyEvent *event = (*events)[i];
        if (!event) continue;
        switch (event->type) {
        case 0:  /* CreateAndJoinLobbyCompleted */
        case 1:  /* JoinLobbyCompleted */
        case 11: /* JoinArrangedLobbyCompleted */
        case 12: /* FindLobbiesCompleted */
        case 23: /* ConnectToLobbyCompleted */
            report("lobby completion", event->type, ((const LobbyCompletion *)event)->result);
            break;
        case 9:  /* Disconnecting */
        case 10: /* Disconnected */
        case 14: /* InviteListenerStatusChanged */
            report("lobby notification", event->type, 0);
            break;
        }
    }
    return result;
}

static BOOL WINAPI trace_headers(HINTERNET request, DWORD level, LPCWSTR name, LPVOID output,
        LPDWORD length, LPDWORD index)
{
    BOOL result = WinHttpQueryHeaders(request, level, name, output, length, index);
    if (result && level == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER) &&
        output && length && *length >= sizeof(DWORD))
        report("WinHTTP status", *(DWORD *)output, 0);
    return result;
}

static BOOL WINAPI trace_response(HINTERNET request, LPVOID reserved)
{
    BOOL result = WinHttpReceiveResponse(request, reserved);
    if (!result) {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) report("WinHTTP receive error", error, 0);
    }
    return result;
}

struct ImportHook { const char *name; FARPROC hook; void *original; };

static unsigned install(HMODULE image, const char *library, const struct ImportHook *hooks, unsigned count)
{
    if (!image) return 0;
    BYTE *base = (BYTE *)image;
    const IMAGE_DOS_HEADER *dos = (const void *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const IMAGE_NT_HEADERS64 *nt = (const void *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return 0;
    IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return 0;
    const IMAGE_IMPORT_DESCRIPTOR *entry = (const void *)(base + imports.VirtualAddress);
    unsigned installed = 0;
    for (DWORD i = 0; i < imports.Size / sizeof(*entry) && entry[i].Name; ++i) {
        if (lstrcmpiA((const char *)(base + entry[i].Name), library) ||
            !entry[i].OriginalFirstThunk || !entry[i].FirstThunk) continue;
        const IMAGE_THUNK_DATA64 *names = (const void *)(base + entry[i].OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iat = (void *)(base + entry[i].FirstThunk);
        for (unsigned j = 0; names[j].u1.AddressOfData; ++j) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *name = (const void *)(base + names[j].u1.AddressOfData);
            for (unsigned k = 0; k < count; ++k) {
                if (lstrcmpA((const char *)name->Name, hooks[k].name)) continue;
                void *volatile *slot = (void *volatile *)&iat[j].u1.Function;
                DWORD protect, ignored;
                if (!VirtualProtect((void *)slot, sizeof(*slot), PAGE_READWRITE, &protect)) break;
                if (hooks[k].original) memcpy(hooks[k].original, &iat[j].u1.Function, sizeof(FARPROC));
                InterlockedExchangePointer(slot, (void *)hooks[k].hook);
                VirtualProtect((void *)slot, sizeof(*slot), protect, &ignored);
                ++installed;
                break;
            }
        }
    }
    return installed;
}

__declspec(dllexport) void InitializeASI(void)
{
    static LONG started;
    if (InterlockedExchange(&started, 1)) return;
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    WCHAR *basename = wcsrchr(path, L'\\');
    const WCHAR file[] = L"fh6-session-results.log";
    if (!basename || (size_t)(basename + 1 - path) + ARRAYSIZE(file) > ARRAYSIZE(path)) return;
    memcpy(basename + 1, file, sizeof(file));
    log_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE) return;
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCWSTR)&self, &pinned)) return;
    const struct ImportHook lobby[] = {
        {"PFMultiplayerInitialize", (FARPROC)trace_init, &initialize_multiplayer},
        {"PFMultiplayerFindLobbies", (FARPROC)trace_find, &find_lobbies},
        {"PFMultiplayerJoinLobby", (FARPROC)trace_join, &join_lobby},
        {"PFMultiplayerJoinArrangedLobby", (FARPROC)trace_arranged, &join_arranged},
        {"PFMultiplayerStartProcessingLobbyStateChanges", (FARPROC)trace_poll, &poll_lobby},
    };
    const struct ImportHook http[] = {
        {"WinHttpQueryHeaders", (FARPROC)trace_headers, NULL},
        {"WinHttpReceiveResponse", (FARPROC)trace_response, NULL},
    };
    report("lobby hooks", install(GetModuleHandleW(NULL), "PlayFabMultiplayerWin.dll", lobby, ARRAYSIZE(lobby)), 0);
    report("PlayFab WinHTTP hooks", install(GetModuleHandleW(L"PlayFabMultiplayerWin.dll"), "winhttp.dll",
                                           http, ARRAYSIZE(http)), 0);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) self = module;
    return TRUE;
}
