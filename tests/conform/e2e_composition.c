/*
 * Real-hardware check of vtable slots: activate a Compositor, create a
 * CompositionColorBrush down a deep slot chain, cast it with cwinrt_query, and
 * round-trip a value-type property (Color).
 * If the generated slots / IIDs / value-type marshalling are wrong, this fails
 * at runtime -- which the compile gate cannot catch.
 */
#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/bootstrap.h>
#include <cwinrt/Windows.UI.Composition.h>
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
    WUC_Comp                  *comp = NULL;
    WUC_CompositionColorBrush *brush = NULL;
    IUnknown                  *as_object = NULL;
    IUnknown                  *dq = NULL;
    WUI_Color                  set = { 255, 10, 20, 30 };
    WUI_Color                  got = { 0, 0, 0, 0 };
    int                        rc = 1;
    HRESULT                    hr;

    /* Composition needs an STA thread with a DispatcherQueue. */
    hr = cwinrt_init(RO_INIT_SINGLETHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    CHECK(cwinrt_bootstrap_dispatcher_queue(&dq), "DispatcherQueue bootstrap");

    CHECK(wuc_comp_new(&comp), "Compositor activate");
    CHECK(wuc_comp_create_color_brush(comp, &brush), "CreateColorBrush");

    /* cast: brush -> ICompositionObject (a base interface) and back. */
    CHECK(cwinrt_query(brush, &CWINRT_IID_WUC_ICompositionObject, (void **)&as_object),
          "cwinrt_query ICompositionObject");

    /* value-type round-trip through put/get on a deep slot. */
    CHECK(wuc_composition_color_brush_put__color(brush, set), "put_Color");
    CHECK(wuc_composition_color_brush_get__color(brush, &got), "get_Color");

    if (got.A != set.A || got.R != set.R || got.G != set.G || got.B != set.B) {
        printf("FAIL Color round-trip: set {%u,%u,%u,%u} got {%u,%u,%u,%u}\n",
               set.A, set.R, set.G, set.B, got.A, got.R, got.G, got.B);
        goto done;
    }

    printf("PASS e2e_composition: activate + cwinrt_query + Color round-trip {%u,%u,%u,%u}\n",
           got.A, got.R, got.G, got.B);
    rc = 0;

done:
    if (as_object)
        as_object->lpVtbl->Release(as_object);
    if (brush)
        ((IUnknown *)brush)->lpVtbl->Release((IUnknown *)brush);
    if (comp)
        ((IUnknown *)comp)->lpVtbl->Release((IUnknown *)comp);
    if (dq)
        dq->lpVtbl->Release(dq);
    cwinrt_uninit();
    return rc;
}
