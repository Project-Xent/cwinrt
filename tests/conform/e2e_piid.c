/*
 * Real-hardware validation of a computed parameterized IID (PIID).
 * Calendar.Languages returns IVectorView<String>; QI'ing that live
 * object for the generated CWINRT_IID_WFOCO_IVectorView_HSTRING must succeed
 * (E_NOINTERFACE would mean the PIID is wrong). Then a vtable-slot call
 * (get_Size, slot 7) returns live data over the parameterized interface.
 */
#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/cwinrt_piids.h>
#include <cwinrt/Windows.Globalization.h>
#include <stdio.h>

#define CHECK(hr, what)                                                  \
    do {                                                                 \
        HRESULT _hr = (hr);                                              \
        if (FAILED(_hr)) {                                               \
            printf("FAIL %s: hr=0x%08lX\n", (what), (unsigned long)_hr); \
            goto done;                                                   \
        }                                                                \
    } while (0)

/* IVectorView<T> vtable: IInspectable occupies slots 0-5; interface methods from
   slot 6 in metadata order: GetAt(6), get_Size(7), IndexOf(8), GetMany(9). */
typedef HRESULT(__stdcall *get_size_fn)(void *self, uint32_t *value);

int main(void)
{
    WGL_Calendar               *cal   = NULL;
    WFOCO_IVectorView_HSTRING  *langs = NULL;
    void                       *qi    = NULL;
    uint32_t                    size  = 0;
    int                         rc    = 1;
    HRESULT                     hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    CHECK(wgl_calendar_new(&cal), "Calendar activate");
    CHECK(wgl_calendar_get__languages(cal, &langs), "get_Languages -> IVectorView<String>");
    if (!langs) {
        printf("FAIL get_Languages returned NULL\n");
        goto done;
    }

    /* QI the live IVectorView<String> for the computed PIID. */
    CHECK(cwinrt_query(langs, &CWINRT_IID_WFOCO_IVectorView_HSTRING, &qi),
          "cwinrt_query(IVectorView<String> PIID)");

    /* Drive the parameterized interface: get_Size at slot 7. */
    {
        get_size_fn get_size = (get_size_fn)(*(void ***)qi)[7];
        CHECK(get_size(qi, &size), "IVectorView<String>::get_Size");
    }
    if (size < 1) {
        printf("FAIL implausible language count %u\n", size);
        goto done;
    }

    printf("PASS e2e_piid: QI live IVectorView<String> by computed PIID + get_Size = %u\n", size);
    rc = 0;

done:
    if (qi) ((IUnknown *)qi)->lpVtbl->Release((IUnknown *)qi);
    if (langs) ((IUnknown *)langs)->lpVtbl->Release((IUnknown *)langs);
    if (cal) ((IUnknown *)cal)->lpVtbl->Release((IUnknown *)cal);
    cwinrt_uninit();
    return rc;
}
