#pragma once

#include <windows.h>
#include <winstring.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef HSTRING cwinrt_hstring;

/* Create from null-terminated wide string; caller must cwinrt_hstring_free. */
HRESULT cwinrt_hstring_from(const wchar_t *src, cwinrt_hstring *out);

/* Release; null-safe. */
void cwinrt_hstring_free(cwinrt_hstring hs);

/* utf-8 -> HSTRING; caller must cwinrt_hstring_free. */
HRESULT cwinrt_hstring_from_utf8(const char *utf8, cwinrt_hstring *out);

/* HSTRING -> utf-8 into buf (NUL-terminated). Returns the byte length written
   (excluding the NUL), or -1 on error or if buf is too small. */
int cwinrt_hstring_to_utf8(cwinrt_hstring hs, char *buf, int bufsz);

/*
 * Stack HSTRING for string literals. HSTRING_HEADER must live as long as HSTRING.
 * Expands at call site so hdr lifetime covers all uses in the same scope.
 */
#define CWINRT_HSTRING_STACK(name, literal)                              \
    HSTRING_HEADER name##_hdr;                                             \
    HSTRING        name = NULL;                                            \
    HRESULT        name##_hr = WindowsCreateStringReference(               \
        (literal),                                                         \
        (UINT32)((sizeof(literal) / sizeof((literal)[0])) - 1),            \
        &name##_hdr,                                                       \
        &(name))

#ifdef __cplusplus
}
#endif
