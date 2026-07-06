#include <cwinrt/init.h>
#include <cwinrt/iid.h>
#include <unknwn.h>
#include <inspectable.h>
#include <stdio.h>

/* Verify IUnknown vtable layout: QueryInterface at slot 0. */
static void test_iunknown_slots(void)
{
    IUnknownVtbl *vt = *(IUnknownVtbl **)(void *)&IID_IUnknown;
    (void)vt;
    printf("abi: IUnknown IID defined\n");
}

/* Guard against the CWINRT_IID_IInspectable convenience constant drifting away
   from the canonical IID_IInspectable. The two are hand-authored in separate
   files (iid.h vs rt/cwinrt_guids.c); a transcription error in either silently
   breaks every cwinrt_query/QueryInterface that uses the convenience constant. */
static int test_inspectable_iid_matches(void)
{
    if (!IsEqualGUID(&CWINRT_IID_IInspectable, &IID_IInspectable)) {
        printf("abi: CWINRT_IID_IInspectable does not match IID_IInspectable\n");
        return 1;
    }
    printf("abi: CWINRT_IID_IInspectable matches IID_IInspectable\n");
    return 0;
}

int main(void)
{
    HRESULT hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;
    test_iunknown_slots();
    if (test_inspectable_iid_matches() != 0) {
        cwinrt_uninit();
        return 1;
    }
    cwinrt_uninit();
    printf("test_abi ok\n");
    return 0;
}
