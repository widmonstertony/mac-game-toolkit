#include <windows.h>
#include <stdio.h>
typedef void * (__cdecl *Factory)(const char *, int *);
struct SteamClient {
    int (__cdecl *create_pipe)(void *);
    unsigned char (__cdecl *release_pipe)(void *, int);
    int (__cdecl *connect_user)(void *, int);
    int (__cdecl *create_user)(void *, int *, int);
    void (__cdecl *release_user)(void *, int, int);
    void * (__cdecl *get_user)(void *, int, int, const char *);
};
struct SteamUser {
    int (__cdecl *get_handle)(void *);
    unsigned char (__cdecl *logged_on)(void *);
};
int wmain(int argc, wchar_t **argv)
{
    HMODULE bridge = argc > 1 ? LoadLibraryExW(argv[1], NULL, LOAD_WITH_ALTERED_SEARCH_PATH) :
                                LoadLibraryW(L"steamclient64.dll");
    if (!bridge) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    Factory create = (Factory)GetProcAddress(bridge, "CreateInterface");
    int result = -1;
    struct SteamClient **client = create ? create("SteamClient017", &result) : NULL;
    printf("native Steam interface: %s (%d)\n", client ? "OK" : "failed", result);
    if (!client) return 2;
    /* Reuse the logged-in client. No Steam executable is started here. */
    int pipe = (*client)->create_pipe(client);
    int user = pipe ? (*client)->connect_user(client, pipe) : 0;
    struct SteamUser **steam_user = user ? (*client)->get_user(client, user, pipe, "SteamUser023") : NULL;
    if (user && !steam_user) steam_user = (*client)->get_user(client, user, pipe, "SteamUser021");
    int online = steam_user && (*steam_user)->logged_on(steam_user);
    printf("native Steam IPC: pipe=%s user=%s online=%s\n",
           pipe ? "OK" : "failed", user ? "OK" : "unavailable", online ? "yes" : "no");
    if (user) (*client)->release_user(client, pipe, user);
    if (pipe) (*client)->release_pipe(client, pipe);
    return !online;
}
