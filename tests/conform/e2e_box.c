/*
 * Value-type round-tripping and boxing on real hardware, headless.
 *
 * Boxes scalar and STRUCT value types through Windows.Foundation.PropertyValue
 * Create-functions, then unboxes via IPropertyValue Get-accessors and checks the
 * round-trip. This proves, on real hardware:
 *   - boxing: Create + Get statics+interface dispatch works end-to-end.
 *   - value types: Byte (uint8_t), Single (float), Guid and the by-value
 *     structs Point and DateTime cross the C ABI correctly in BOTH directions
 *     (params are real value types, not degraded pointers). PropertyValue needs
 *     no interactive desktop.
 */
#include <cwinrt/init.h>
#include <cwinrt/cast.h>
#include <cwinrt/Windows.Foundation.h>
#include <stdio.h>

static int guid_eq(GUID const *a, GUID const *b) {
    unsigned char const *pa = (unsigned char const *)a;
    unsigned char const *pb = (unsigned char const *)b;
    int i;
    for (i = 0; i < (int)sizeof(GUID); i++)
        if (pa[i] != pb[i]) return 0;
    return 1;
}

#define CHECK(hr, what)                                                  \
    do {                                                                 \
        HRESULT _hr = (hr);                                              \
        if (FAILED(_hr)) {                                               \
            printf("FAIL %s: hr=0x%08lX\n", (what), (unsigned long)_hr); \
            goto done;                                                   \
        }                                                                \
    } while (0)

static int g_fail = 0;

int main(void)
{
    int     rc = 1;
    HRESULT hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    /* --- Byte (uint8_t) scalar --- */
    {
        IInspectable      *boxed = NULL;
        WF_IPropertyValue *pv    = NULL;
        uint8_t            got   = 0;
        CHECK(wf_property_value_create_u_int8((uint8_t)0xAB, &boxed), "create_u_int8");
        CHECK(cwinrt_query(boxed, &CWINRT_IID_WF_IPropertyValue, (void **)&pv), "QI IPropertyValue (u8)");
        CHECK(wf_property_value_get_u_int8(pv, &got), "get_u_int8");
        if (got != 0xAB) { printf("FAIL Byte round-trip: 0x%02X != 0xAB\n", got); g_fail = 1; }
        ((IUnknown *)pv)->lpVtbl->Release((IUnknown *)pv);
        ((IUnknown *)boxed)->lpVtbl->Release((IUnknown *)boxed);
    }

    /* --- Single (float) scalar --- */
    {
        IInspectable      *boxed = NULL;
        WF_IPropertyValue *pv    = NULL;
        float              got   = 0.0f;
        CHECK(wf_property_value_create_single(3.5f, &boxed), "create_single");
        CHECK(cwinrt_query(boxed, &CWINRT_IID_WF_IPropertyValue, (void **)&pv), "QI IPropertyValue (f32)");
        CHECK(wf_property_value_get_single(pv, &got), "get_single");
        if (got != 3.5f) { printf("FAIL Single round-trip: %f != 3.5\n", got); g_fail = 1; }
        ((IUnknown *)pv)->lpVtbl->Release((IUnknown *)pv);
        ((IUnknown *)boxed)->lpVtbl->Release((IUnknown *)boxed);
    }

    /* --- Point (by-value struct {float X; float Y;}) --- */
    {
        IInspectable      *boxed = NULL;
        WF_IPropertyValue *pv    = NULL;
        WF_Point           in    = { 12.0f, 34.0f };
        WF_Point           got   = { 0.0f, 0.0f };
        CHECK(wf_property_value_create_point(in, &boxed), "create_point");
        CHECK(cwinrt_query(boxed, &CWINRT_IID_WF_IPropertyValue, (void **)&pv), "QI IPropertyValue (Point)");
        CHECK(wf_property_value_get_point(pv, &got), "get_point");
        if (got.X != 12.0f || got.Y != 34.0f) {
            printf("FAIL Point round-trip: {%f,%f} != {12,34}\n", got.X, got.Y);
            g_fail = 1;
        }
        ((IUnknown *)pv)->lpVtbl->Release((IUnknown *)pv);
        ((IUnknown *)boxed)->lpVtbl->Release((IUnknown *)boxed);
    }

    /* --- DateTime (by-value struct {int64 UniversalTime}) --- */
    {
        IInspectable      *boxed = NULL;
        WF_IPropertyValue *pv    = NULL;
        WF_DateTime        in    = { 0x0123456789ABCDEFLL };
        WF_DateTime        got   = { 0 };
        CHECK(wf_property_value_create_date_time(in, &boxed), "create_date_time");
        CHECK(cwinrt_query(boxed, &CWINRT_IID_WF_IPropertyValue, (void **)&pv), "QI IPropertyValue (DateTime)");
        CHECK(wf_property_value_get_date_time(pv, &got), "get_date_time");
        if (got.UniversalTime != in.UniversalTime) {
            printf("FAIL DateTime round-trip: %lld != %lld\n", (long long)got.UniversalTime,
                   (long long)in.UniversalTime);
            g_fail = 1;
        }
        ((IUnknown *)pv)->lpVtbl->Release((IUnknown *)pv);
        ((IUnknown *)boxed)->lpVtbl->Release((IUnknown *)boxed);
    }

    /* --- Guid (by-value 16-byte struct) --- */
    {
        IInspectable      *boxed = NULL;
        WF_IPropertyValue *pv    = NULL;
        GUID               in    = { 0x11223344, 0x5566, 0x7788, { 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 } };
        GUID               got = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };
        CHECK(wf_property_value_create_guid(in, &boxed), "create_guid");
        CHECK(cwinrt_query(boxed, &CWINRT_IID_WF_IPropertyValue, (void **)&pv), "QI IPropertyValue (Guid)");
        CHECK(wf_property_value_get_guid(pv, &got), "get_guid");
        if (!guid_eq(&in, &got)) { printf("FAIL Guid round-trip mismatch\n"); g_fail = 1; }
        ((IUnknown *)pv)->lpVtbl->Release((IUnknown *)pv);
        ((IUnknown *)boxed)->lpVtbl->Release((IUnknown *)boxed);
    }

    if (g_fail) goto done;
    printf("PASS e2e_box: Byte/Single/Point/DateTime/Guid box+unbox round-trip\n");
    rc = 0;

done:
    cwinrt_uninit();
    return rc;
}
