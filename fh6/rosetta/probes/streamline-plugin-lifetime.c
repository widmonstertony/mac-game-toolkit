/* Authored loader/hook regression, not a game or a GPU feature test.
 * Build as a DLL with -DPLUGIN, or as the executable without it.
 * All plugin copies are this fixture's own code. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifdef PLUGIN
static int marker;
__declspec(dllexport) void *slGetPluginFunction(const char *name)
{
    return name && strcmp(name, "fixture-marker") == 0 ? &marker : NULL;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }
#else
typedef void *(__cdecl *plugin_fn)(const char *);
static int check(HMODULE module, const char *label)
{
    plugin_fn get = (plugin_fn)GetProcAddress(module, "slGetPluginFunction");
    if (!get) return 10;
    void *first = get("fixture-marker");
    if (!first) return 11;
    for (int i = 0; i < 200; ++i)
    {
        if (get("fixture-marker") != first) return 12;
        get("slOnPluginLoad"); /* resolve the callback, never call fake callbacks */
    }
    printf("PASS %s calls=400\n", label); fflush(stdout);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("preload first fixture\n");
    HMODULE first = LoadLibraryA(argv[2]);
    if (!first) { printf("plugin error=%lu\n", GetLastError()); return 3; }
    printf("load OptiScaler\n");
    if (!LoadLibraryA(argv[1])) { printf("Opti error=%lu\n", GetLastError()); return 4; }
    Sleep(1500);
    int r = check(first, "preloaded"); if (r) return r;
    printf("load concurrent second copy\n");
    HMODULE second = LoadLibraryA(argv[3]);
    if (!second || second == first) return 5;
    Sleep(100);
    if ((r = check(first, "first-live")) || (r = check(second, "second-live"))) return r;
    printf("unload and reload second copy\n");
    FreeLibrary(second);
    second = LoadLibraryA(argv[3]);
    if (!second) return 6;
    if ((r = check(first, "first-after-reload")) || (r = check(second, "second-reloaded"))) return r;
    printf("PASS plugin lifetime fixture\n");
    return 0;
}
#endif
