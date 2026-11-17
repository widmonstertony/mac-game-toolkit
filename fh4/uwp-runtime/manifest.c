/* LGPL-2.1-or-later. Read the executable's own manifest using XmlLite.
 * No game identity, package registration or entitlement is fabricated. */
#include "private.h"
#include <shlwapi.h>
#include <xmllite.h>

static char *utf8(const WCHAR *s, UINT count)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, count, NULL, 0, NULL, NULL);
    char *out;
    if (n <= 0 || !(out = malloc((size_t)n + 1))) return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, count, out, n, NULL, NULL) != n)
    { free(out); return NULL; }
    out[n] = 0;
    return out;
}

void free_app_manifest(struct app_manifest *m)
{
    free(m->identity); free(m->display); free(m->entry); free(m->id);
    memset(m,0,sizeof(*m));
}

BOOL read_app_manifest(struct app_manifest *m)
{
    WCHAR path[32768], exe[32768], *last;
    DWORD size;
    IStream *stream = NULL;
    IXmlReader *reader = NULL;
    XmlNodeType type;
    HRESULT hr;
    BOOL in_display = FALSE, root = FALSE, ok = FALSE;
    char **identity=&m->identity, **display=&m->display;
    memset(m,0,sizeof(*m));
    size = GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    if (!size || size >= ARRAYSIZE(path) || !(last = wcsrchr(path, L'\\'))) return FALSE;
    wcscpy(exe,last+1);
    if ((size_t)(last-path)+18 >= ARRAYSIZE(path)) return FALSE;
    wcscpy(last+1, L"AppxManifest.xml");
    hr = SHCreateStreamOnFileEx(path, STGM_READ | STGM_SHARE_DENY_WRITE,
                              FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &stream);
    if (FAILED(hr)) goto done;
    hr = CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL);
    if (FAILED(hr)) goto done;
    if (FAILED(IXmlReader_SetProperty(reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(IXmlReader_SetInput(reader, (IUnknown *)stream))) goto done;
    while ((hr = IXmlReader_Read(reader, &type)) == S_OK)
    {
        const WCHAR *name, *value; UINT len, depth;
        if (FAILED(IXmlReader_GetDepth(reader, &depth))) goto done;
        if (type == XmlNodeType_Element)
        {
            if (FAILED(IXmlReader_GetLocalName(reader, &name, NULL))) goto done;
            if (!depth) { if (wcscmp(name,L"Package")) goto done; root = TRUE; }
            if (depth == 1 && !wcscmp(name,L"Identity"))
            {
                if (*identity || IXmlReader_MoveToAttributeByName(reader,L"Name",NULL) != S_OK ||
                    FAILED(IXmlReader_GetValue(reader,&value,&len))) goto done;
                *identity = utf8(value,len);
                if (!*identity || FAILED(IXmlReader_MoveToElement(reader))) goto done;
            }
            in_display = depth == 2 && !wcscmp(name,L"DisplayName");
            if (depth == 2 && !wcscmp(name,L"Application"))
            {
                if (IXmlReader_MoveToAttributeByName(reader,L"Executable",NULL)!=S_OK ||
                    FAILED(IXmlReader_GetValue(reader,&value,&len))) goto done;
                if (!_wcsicmp(value,exe))
                {
                    if (m->entry || IXmlReader_MoveToAttributeByName(reader,L"EntryPoint",NULL)!=S_OK ||
                        FAILED(IXmlReader_GetValue(reader,&value,&len))) goto done;
                    m->entry=utf8(value,len);
                    if (!m->entry || IXmlReader_MoveToAttributeByName(reader,L"Id",NULL)!=S_OK ||
                        FAILED(IXmlReader_GetValue(reader,&value,&len))) goto done;
                    m->id=utf8(value,len);
                    if (!m->id) goto done;
                }
                if (FAILED(IXmlReader_MoveToElement(reader))) goto done;
            }
        }
        else if (type == XmlNodeType_Text && in_display)
        {
            if (*display || FAILED(IXmlReader_GetValue(reader,&value,&len))) goto done;
            *display = utf8(value,len); in_display = FALSE;
        }
        else if (type == XmlNodeType_EndElement) in_display = FALSE;
    }
    ok = hr == S_FALSE && root && *identity && *display && m->entry && m->id;
done:
    if (reader) IXmlReader_Release(reader);
    if (stream) IStream_Release(stream);
    if (!ok) free_app_manifest(m);
    return ok;
}
