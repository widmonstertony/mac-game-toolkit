#include <windows.h>
#include <string.h>

typedef void * (__cdecl *Factory)(const char *, int *);
typedef void * (__cdecl *GenericInterface)(void *, int, int, const char *);

/* ISteamClient::GetISteamGenericInterface in the SteamClient021 vtable. */
enum { GET_GENERIC_INTERFACE = 12 };
static const char client_version[] = "SteamClient021";
static SRWLOCK bridge_lock = SRWLOCK_INIT;
static GenericInterface original_generic;

static void *create_native_interface(const char *name, int *code)
{
    HMODULE bridge = GetModuleHandleW(L"lsteamclient.dll");
    if (!bridge) bridge = LoadLibraryW(L"lsteamclient.dll");
    Factory create = bridge ? (Factory)GetProcAddress(bridge, "CreateInterface") : NULL;
    return create ? create(name, code) : NULL;
}

static void * __cdecl get_generic(void *client, int user, int pipe, const char *version)
{
    /* A query may fault after the bridge was used by another thread.
     * Reacquire the same native client on the calling thread and keep the
     * factory and query together. All arguments and the result are unchanged. */
    AcquireSRWLockExclusive(&bridge_lock);
    void *result = NULL;
    if (create_native_interface(client_version, NULL))
        result = original_generic(client, user, pipe, version);
    ReleaseSRWLockExclusive(&bridge_lock);
    return result;
}

void * __cdecl CreateInterface(const char *name, int *code)
{
    AcquireSRWLockExclusive(&bridge_lock);
    void ***client = create_native_interface(name, code);
    if (client && name && !strcmp(name, client_version))
    {
        PVOID volatile *slot = &(*client)[GET_GENERIC_INTERFACE];
        PVOID method = *slot;
        DWORD protect, ignored;
        if (method && method != (PVOID)get_generic &&
            VirtualProtect((void *)slot, sizeof(*slot), PAGE_READWRITE, &protect))
        {
            if (!original_generic || original_generic == (GenericInterface)method)
            {
                original_generic = (GenericInterface)method;
                InterlockedExchangePointer(slot, (PVOID)get_generic);
            }
            VirtualProtect((void *)slot, sizeof(*slot), protect, &ignored);
        }
    }
    ReleaseSRWLockExclusive(&bridge_lock);
    return client;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE;
}
