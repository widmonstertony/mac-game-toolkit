#include <windows.h>
#include <stdio.h>
typedef void * (__cdecl *Factory)(const char *, int *);
struct SteamClient {
    int (__cdecl *create_pipe)(void *);
    unsigned char (__cdecl *release_pipe)(void *, int);
    int (__cdecl *connect_user)(void *, int);
    int (__cdecl *create_user)(void *, int *, int);
    void (__cdecl *release_user)(void *, int, int);
};
int main(void)
{
    HMODULE bridge = LoadLibraryW(L"steamclient64.dll");
    if (!bridge) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    Factory create = (Factory)GetProcAddress(bridge, "CreateInterface");
    int result = -1;
    struct SteamClient **client = create ? create("SteamClient017", &result) : NULL;
    printf("native Steam interface: %s (%d)\n", client ? "OK" : "failed", result);
    if (!client) return 2;
    /* Reuse the logged-in client. No Steam executable is started here. */
    int pipe = (*client)->create_pipe(client);
    int user = pipe ? (*client)->connect_user(client, pipe) : 0;
    printf("native Steam IPC: pipe=%s logged-in user=%s\n", pipe ? "OK" : "failed", user ? "OK" : "unavailable");
    if (user) (*client)->release_user(client, pipe, user);
    if (pipe) (*client)->release_pipe(client, pipe);
    return !user;
}
