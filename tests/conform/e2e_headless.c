/*
 * Headless real-hardware check of vtable slots and the cast/IID, using a
 * desktop-independent API (Windows.Globalization.Calendar) so it runs in any
 * session (Composition needs an interactive desktop/GPU).
 *
 * Exercises: default-ctor activation, cwinrt_query to the type's IID, and a chain
 * of real vtable-slot method calls returning live data (SetToNow/get_Year/AddYears).
 */
#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/Windows.Globalization.h>
#include <stdio.h>

#define CHECK(hr, what)                                                  \
    do {                                                                 \
        HRESULT _hr = (hr);                                              \
        if (FAILED(_hr)) {                                              \
            printf("FAIL %s: hr=0x%08lX\n", (what), (unsigned long)_hr); \
            goto done;                                                   \
        }                                                                \
    } while (0)

int main(void)
{
    WGL_Calendar *cal = NULL;
    IUnknown     *as_iface = NULL;
    int32_t       year = 0, year2 = 0;
    int           rc = 1;
    HRESULT       hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    CHECK(wgl_calendar_new(&cal), "Calendar activate");

    /* cast: QI to the Calendar interface IID and back. */
    CHECK(cwinrt_query(cal, &CWINRT_IID_WGL_ICalendar, (void **)&as_iface), "cwinrt_query ICalendar");

    /* Deep slot chain returning live data. */
    CHECK(wgl_calendar_set_to_now(cal), "SetToNow");
    CHECK(wgl_calendar_get__year(cal, &year), "get_Year");
    if (year < 2000 || year > 3000) {
        printf("FAIL implausible year %d\n", year);
        goto done;
    }
    CHECK(wgl_calendar_add_years(cal, 5), "AddYears(5)");
    CHECK(wgl_calendar_get__year(cal, &year2), "get_Year (after AddYears)");
    if (year2 != year + 5) {
        printf("FAIL AddYears: %d + 5 != %d\n", year, year2);
        goto done;
    }

    printf("PASS e2e_headless: activate + cwinrt_query + slots (year %d -> +5 -> %d)\n", year, year2);
    rc = 0;

done:
    if (as_iface)
        as_iface->lpVtbl->Release(as_iface);
    if (cal)
        ((IUnknown *)cal)->lpVtbl->Release((IUnknown *)cal);
    cwinrt_uninit();
    return rc;
}
