// Integration check: a downstream consumer that links cwinrt via a SINGLE
// `add_deps("cwinrt")` in xmake.lua -- no add_includedirs, no add_syslinks. If this
// target builds and runs, the public integration surface works end to end (header
// path + runtime + a generated impl + the full link recipe all inherited). It is the
// executable proof behind docs/INTEGRATION.md.
#include <cwinrt/init.h>
#include <cwinrt/Windows.Globalization.h>
#include <stdio.h>

int main(void)
{
    WGL_Calendar *cal = NULL;
    int32_t       year = 0;
    HRESULT       hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) { printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr); return 1; }

    hr = wgl_calendar_new(&cal);
    if (FAILED(hr) || !cal) { printf("FAIL Calendar activate: 0x%08lX\n", (unsigned long)hr); cwinrt_uninit(); return 1; }

    hr = wgl_calendar_get__year(cal, &year);
    ((IUnknown *)cal)->lpVtbl->Release((IUnknown *)cal);
    cwinrt_uninit();

    if (FAILED(hr) || year < 2000) { printf("FAIL get_Year: 0x%08lX year=%d\n", (unsigned long)hr, year); return 1; }
    printf("PASS consume_cwinrt: add_deps(\"cwinrt\") only -> Calendar year %d\n", year);
    return 0;
}
