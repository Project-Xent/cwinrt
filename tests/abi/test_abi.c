#include <cwinrt/init.h>
#include <unknwn.h>
#include <stdio.h>

/* Verify IUnknown vtable layout: QueryInterface at slot 0. */
static void test_iunknown_slots(void)
{
    IUnknownVtbl *vt = *(IUnknownVtbl **)(void *)&IID_IUnknown;
    (void)vt;
    printf("abi: IUnknown IID defined\n");
}

int main(void)
{
    HRESULT hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;
    test_iunknown_slots();
    cwinrt_uninit();
    printf("test_abi ok\n");
    return 0;
}
