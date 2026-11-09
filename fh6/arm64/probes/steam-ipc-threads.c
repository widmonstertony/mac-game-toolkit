#include <windows.h>
#include <stdio.h>

typedef void * (__cdecl *Factory)(const char *, int *);
typedef int (__cdecl *CreateSteamPipe)(void *);
typedef int (__cdecl *ConnectUser)(void *, int);
typedef void * (__cdecl *GenericInterface)(void *, int, int, const char *);

struct SteamThreadProbe {
    void ***client;
    int pipe;
    int user;
};

static void *query(const struct SteamThreadProbe *probe, int user, const char *version)
{
    GenericInterface get_interface = (GenericInterface)(*probe->client)[12];
    void *result = get_interface(probe->client, user, probe->pipe, version);
    printf("%s: %s\n", version, result ? "interface" : "unavailable");
    fflush(stdout);
    return result;
}

static DWORD WINAPI query_worker(void *context)
{
    const struct SteamThreadProbe *probe = context;
    return !query(probe, 0, "SteamUtils010") ||
           !query(probe, probe->user, "SteamController008") ||
           !query(probe, probe->user, "SteamInput006");
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"steamclient64.dll");
    Factory create = module ? (Factory)GetProcAddress(module, "CreateInterface") : NULL;
    void ***client = create ? create("SteamClient021", NULL) : NULL;
    if (!client) return 2;
    int pipe = ((CreateSteamPipe)(*client)[0])(client);
    int user = pipe ? ((ConnectUser)(*client)[2])(client, pipe) : 0;
    if (!user) return 3;
    const struct SteamThreadProbe probe = {client, pipe, user};
    if (!query(&probe, 0, "SteamUtils010")) return 4;

    HANDLE worker = CreateThread(NULL, 0, query_worker, (void *)&probe, 0, NULL);
    if (!worker) return 5;
    DWORD status = 1;
    if (WaitForSingleObject(worker, 10000) == WAIT_OBJECT_0)
        GetExitCodeThread(worker, &status);
    CloseHandle(worker);
    if (status) return 6;

    /* Process teardown closes the probe pipe. This test exercises the query
     * path in both directions across a switch between calling threads. */
    return query(&probe, user, "STEAMUSERSTATS_INTERFACE_VERSION012") ? 0 : 7;
}
