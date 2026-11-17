/* Scoped Highball package graph loading. LGPL-2.1-or-later. */
#include "appmodel.h"
LONG WINAPI GetCurrentPackageInfo(UINT32, UINT32 *, BYTE *, UINT32 *);
struct fh4_loader_package_info
{
    UINT32 reserved, flags;
    WCHAR *path, *full, *family;
    PACKAGE_ID id;
};
HMODULE WINAPI LoadPackagedLibrary( LPCWSTR name, DWORD reserved )
{
    struct fh4_loader_package_info *graph;
    UINT32 bytes = 0, count = 0, i;
    WCHAR *paths, *target, *filename, system[MAX_PATH];
    SIZE_T path_chars = 1, max_root = 0, name_chars;
    HMODULE module = NULL;
    UNICODE_STRING unicode;
    LONG err;
    NTSTATUS status;
    if (reserved || !name || !*name || wcschr(name, '/') || wcsstr(name, L"..") ||
        wcschr(name, ':') || RtlIsDosDeviceName_U(name) ||
        RtlDetermineDosPathNameType_U(name) != RtlPathTypeRelative)
    { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    err = GetCurrentPackageInfo(0x30, &bytes, NULL, &count);
    if (err != ERROR_INSUFFICIENT_BUFFER) { SetLastError(err); return NULL; }
    if (!(graph = HeapAlloc(GetProcessHeap(), 0, bytes))) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if ((err = GetCurrentPackageInfo(0x30, &bytes, (BYTE *)graph, &count))) goto done;
    for (i = 0; i < count; ++i)
    {
        SIZE_T n = wcslen(graph[i].path);
        path_chars += n + 1;
        if (n > max_root) max_root = n;
    }
    if (!GetSystemDirectoryW(system, ARRAY_SIZE(system))) { err = GetLastError(); goto done; }
    path_chars += wcslen(system) + 1;
    name_chars = wcslen(name) + 5;
    if (name_chars > 1024) { err = ERROR_FILENAME_EXCED_RANGE; goto done; }
    paths = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (path_chars + max_root + name_chars + 2 + name_chars) * sizeof(WCHAR));
    if (!paths) { err = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    target = paths + path_chars;
    filename = target + max_root + name_chars + 2;
    wcscpy(filename, name);
    {
        WCHAR *base = wcsrchr(filename, '\\');
        if (!wcschr(base ? base + 1 : filename, '.')) wcscat(filename, L".dll");
    }
    for (i = 0; i < count; ++i) { wcscat(paths, graph[i].path); wcscat(paths, L";"); }
    /* OS imports still use system32; CWD/PATH are deliberately absent. */
    wcscat(paths, system);
    err = ERROR_MOD_NOT_FOUND;
    for (i = 0; i < count; ++i)
    {
        DWORD attrs;
        wcscpy(target, graph[i].path); wcscat(target, L"\\"); wcscat(target, filename);
        attrs = GetFileAttributesW(target);
        if (attrs == INVALID_FILE_ATTRIBUTES || attrs & FILE_ATTRIBUTE_DIRECTORY) continue;
        RtlInitUnicodeString(&unicode, target);
        status = LdrLoadDll(paths, 0, &unicode, &module);
        err = RtlNtStatusToDosError(status);
        /* Do not hide a bad/missing dependency by selecting another package. */
        break;
    }
    HeapFree(GetProcessHeap(), 0, paths);
done:
    HeapFree(GetProcessHeap(), 0, graph);
    if (!module) SetLastError(err);
    return module;
}
