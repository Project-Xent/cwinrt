#include <cwinrt/hstring.h>

#include <stdlib.h>
#include <wchar.h>

HRESULT cwinrt_hstring_from(const wchar_t *src, cwinrt_hstring *out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!src)
        return E_INVALIDARG;
    return WindowsCreateString(src, (UINT32)wcslen(src), out);
}

void cwinrt_hstring_free(cwinrt_hstring hs)
{
    if (hs)
        WindowsDeleteString(hs);
}

HRESULT cwinrt_hstring_from_utf8(const char *utf8, cwinrt_hstring *out)
{
    int      wn;
    wchar_t *w;
    HRESULT  hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!utf8)
        return E_INVALIDARG;
    wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0); /* incl. NUL */
    if (wn <= 0)
        return HRESULT_FROM_WIN32(GetLastError());
    w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    if (!w)
        return E_OUTOFMEMORY;
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wn) <= 0) {
        free(w);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    hr = WindowsCreateString(w, (UINT32)(wn - 1), out);
    free(w);
    return hr;
}

int cwinrt_hstring_to_utf8(cwinrt_hstring hs, char *buf, int bufsz)
{
    wchar_t const *w;
    UINT32         wlen = 0;
    int            n;

    if (!buf || bufsz < 1)
        return -1;
    buf[0] = '\0';
    w = WindowsGetStringRawBuffer(hs, &wlen); /* NUL for empty/NULL hs */
    if (!w)
        return 0;
    n = WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen, buf, bufsz - 1, NULL, NULL);
    if (n < 0 || n > bufsz - 1)
        return -1;
    buf[n] = '\0';
    return n;
}
