/* FH4 isolated package context, LGPL-2.1-or-later.
 * Launcher supplies metadata parsed from the owned manifests. Package identity
 * is NOT a license check. No entitlement or activation success is synthesized.
 * Included by Highball kernelbase/version.c, with normal Wine headers in scope.
 */
LONG WINAPI GetPackagePath(const PACKAGE_ID *, UINT32, UINT32 *, WCHAR *);
LONG WINAPI GetPackagePathByFullName(const WCHAR *name, UINT32 *length, WCHAR *path)
{
    BYTE data[2048];
    UINT32 bytes = sizeof(data);
    LONG err;
    if (!name || !length || (*length && !path) || wcslen(name) > 255) return ERROR_INVALID_PARAMETER;
    if ((err = PackageIdFromFullName(name, 0, &bytes, data))) return err;
    return GetPackagePath((PACKAGE_ID *)data, 0, length, path);
}

struct fh4_package
{
    WCHAR full[256], family[256], path[1024], publisher[512];
};
static struct
{
    BOOL valid;
    UINT32 count;
    WCHAR aumid[512];
    struct fh4_package packages[2];
} fh4_context;
static INIT_ONCE fh4_context_once = INIT_ONCE_STATIC_INIT;

static BOOL fh4_env( const WCHAR *name, WCHAR *out, DWORD cap )
{
    DWORD n = GetEnvironmentVariableW( name, out, cap );
    return n && n < cap;
}

static BOOL fh4_read_package( struct fh4_package *p, BOOL dependency )
{
    WCHAR registered[1024], family[256];
    BYTE id_data[2048];
    PACKAGE_ID *id = (PACKAGE_ID *)id_data;
    UINT32 n = ARRAY_SIZE(registered), bytes = sizeof(id_data);
    if (!fh4_env( dependency ? L"FH4_PACKAGE_DEP_FULL" : L"FH4_PACKAGE_FULL", p->full, ARRAY_SIZE(p->full) ) ||
        !fh4_env( dependency ? L"FH4_PACKAGE_DEP_PATH" : L"FH4_PACKAGE_PATH", p->path, ARRAY_SIZE(p->path) ) ||
        !fh4_env( dependency ? L"FH4_PACKAGE_DEP_PUBLISHER" : L"FH4_PACKAGE_PUBLISHER", p->publisher, ARRAY_SIZE(p->publisher) ))
        return FALSE;
    if (wcschr( p->path, ';' ) || GetPackagePathByFullName( p->full, &n, registered ) || wcsicmp( registered, p->path ))
        return FALSE;
    if (PackageIdFromFullName( p->full, 0, &bytes, id_data )) return FALSE;
    if (wcslen(id->name) + wcslen(id->publisherId) + 2 > ARRAY_SIZE(family)) return FALSE;
    wcscpy( family, id->name ); wcscat( family, L"_" ); wcscat( family, id->publisherId );
    wcscpy( p->family, family );
    return TRUE;
}

static BOOL CALLBACK fh4_init_context( INIT_ONCE *once, void *param, void **context )
{
    WCHAR exe[2048], target[2048], app[128];
    DWORD n;
    (void)once; (void)param; (void)context;
    /* Exact executable scoping also rejects inherited environment in helpers. */
    if (!fh4_env( L"FH4_PACKAGE_EXE", target, ARRAY_SIZE(target) )) return TRUE;
    n = GetModuleFileNameW( NULL, exe, ARRAY_SIZE(exe) );
    if (!n || n >= ARRAY_SIZE(exe) || wcsicmp( exe, target )) return TRUE;
    if (!fh4_read_package( &fh4_context.packages[0], FALSE )) return TRUE;
    n = wcslen( fh4_context.packages[0].path );
    if (wcsnicmp( target, fh4_context.packages[0].path, n ) || target[n] != '\\') return TRUE;
    if (!fh4_env( L"FH4_PACKAGE_APP_ID", app, ARRAY_SIZE(app) ) || wcschr(app, '!') || wcschr(app, '\\')) return TRUE;
    wcscpy( fh4_context.aumid, fh4_context.packages[0].family );
    wcscat( fh4_context.aumid, L"!" ); wcscat( fh4_context.aumid, app );
    fh4_context.count = 1;
    if (GetEnvironmentVariableW( L"FH4_PACKAGE_DEP_FULL", NULL, 0 ))
    {
        if (!fh4_read_package( &fh4_context.packages[1], TRUE )) return TRUE;
        fh4_context.count = 2;
    }
    fh4_context.valid = TRUE;
    return TRUE;
}
static BOOL fh4_has_context(void)
{
    InitOnceExecuteOnce( &fh4_context_once, fh4_init_context, NULL, NULL );
    return fh4_context.valid;
}
static LONG fh4_string( UINT32 *size, WCHAR *buffer, const WCHAR *value, LONG absent )
{
    UINT32 needed;
    if (!size || (*size && !buffer)) return ERROR_INVALID_PARAMETER;
    if (!fh4_has_context()) return absent;
    needed = wcslen(value) + 1;
    if (*size < needed) { *size = needed; return ERROR_INSUFFICIENT_BUFFER; }
    memcpy( buffer, value, needed * sizeof(WCHAR) );
    *size = needed;
    return ERROR_SUCCESS;
}
LONG WINAPI GetCurrentApplicationUserModelId( UINT32 *n, WCHAR *out )
{ return fh4_string(n, out, fh4_context.aumid, APPMODEL_ERROR_NO_APPLICATION); }
LONG WINAPI GetCurrentPackageFamilyName( UINT32 *n, WCHAR *out )
{ return fh4_string(n, out, fh4_context.packages[0].family, APPMODEL_ERROR_NO_PACKAGE); }
LONG WINAPI GetCurrentPackageFullName( UINT32 *n, WCHAR *out )
{ return fh4_string(n, out, fh4_context.packages[0].full, APPMODEL_ERROR_NO_PACKAGE); }
LONG WINAPI GetCurrentPackagePath( UINT32 *n, WCHAR *out )
{ return fh4_string(n, out, fh4_context.packages[0].path, APPMODEL_ERROR_NO_PACKAGE); }
LONG WINAPI GetCurrentPackageId( UINT32 *n, BYTE *out )
{
    if (!n || (*n && !out)) return ERROR_INVALID_PARAMETER;
    if (!fh4_has_context()) return APPMODEL_ERROR_NO_PACKAGE;
    return PackageIdFromFullName(fh4_context.packages[0].full, 0, n, out);
}
struct fh4_package_info
{
    UINT32 reserved, flags;
    WCHAR *path, *packageFullName, *packageFamilyName;
    PACKAGE_ID packageId;
};
static WCHAR *fh4_put_string( BYTE *buffer, UINT32 *offset, const WCHAR *s )
{
    UINT32 bytes = (wcslen(s) + 1) * sizeof(WCHAR);
    WCHAR *out = buffer ? (WCHAR *)(buffer + *offset) : NULL;
    if (out) memcpy(out, s, bytes);
    *offset += bytes;
    return out;
}
LONG WINAPI GetCurrentPackageInfo( UINT32 flags, UINT32 *size, BYTE *buffer, UINT32 *count )
{
    struct fh4_package_info *infos = (void *)buffer;
    UINT32 chosen[2], n = 0, i, pass, offset = 0, capacity;
    const UINT32 head = 0x10, direct = 0x20, stat = 0x80000, dynamic = 0x100000;
    if (!size || (*size && !buffer)) return ERROR_INVALID_PARAMETER;
    if (!fh4_has_context()) return APPMODEL_ERROR_NO_PACKAGE;
    if (flags & ~(head | direct | 0x40 | 0x80 | 0x20000 | 0x40000 | stat | dynamic | 0x200000))
        return ERROR_INVALID_PARAMETER;
    /* Only static main/direct packages exist in this manifest graph. */
    if (!(flags & dynamic) || flags & stat)
    {
        UINT32 select = flags & (head | direct | 0x40 | 0x80 | 0x20000 | 0x40000 | 0x200000);
        if (!select || select & head) chosen[n++] = 0;
        if (fh4_context.count == 2 && (!select || select & direct)) chosen[n++] = 1;
    }
    capacity = *size;
    /* Two passes, aligned PACKAGE_ID records; no writes for a short buffer. */
    for (pass = 0; pass < 2; ++pass)
    {
        BYTE *dst = pass ? buffer : NULL;
        offset = n * sizeof(*infos);
        for (i = 0; i < n; ++i)
        {
            struct fh4_package *p = &fh4_context.packages[chosen[i]];
            WCHAR *path, *full, *family, *publisher;
            UINT32 id_size = 0;
            LONG err = PackageIdFromFullName(p->full, 0, &id_size, NULL);
            if (err != ERROR_INSUFFICIENT_BUFFER) return err;
            path = fh4_put_string(dst, &offset, p->path);
            full = fh4_put_string(dst, &offset, p->full);
            family = fh4_put_string(dst, &offset, p->family);
            publisher = fh4_put_string(dst, &offset, p->publisher);
            offset = (offset + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
            if (pass)
            {
                err = PackageIdFromFullName(p->full, 0, &id_size, dst + offset);
                if (err) return err;
                memset(&infos[i], 0, sizeof(infos[i]));
                infos[i].flags = 0x80000 | (chosen[i] ? 1 : 0); /* PACKAGE_PROPERTY_FRAMEWORK */
                infos[i].path = path; infos[i].packageFullName = full; infos[i].packageFamilyName = family;
                infos[i].packageId = *(PACKAGE_ID *)(dst + offset);
                infos[i].packageId.publisher = publisher;
            }
            offset += id_size;
        }
        if (!pass)
        {
            *size = offset;
            if (count) *count = n;
            if (capacity < offset) return ERROR_INSUFFICIENT_BUFFER;
            if (!n) return ERROR_SUCCESS;
        }
    }
    return ERROR_SUCCESS;
}
