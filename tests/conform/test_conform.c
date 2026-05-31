#include <cwinrt/init.h>
#include <stdio.h>

/* Driver placeholder; see test_factory, test_dispatch, test_header_compile, golden_guid. */
int main(void)
{
    HRESULT hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;
    printf("conform: run test_factory test_dispatch test_header_compile golden_guid\n");
    cwinrt_uninit();
    return 0;
}
