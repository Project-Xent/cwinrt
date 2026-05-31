#include <cwinrt/init.h>
#include <stdio.h>

/*
 * Declaration-only generated headers pull a large include graph; full compile
 * coverage is tracked in tests/conform. Smoke test verifies runtime init only.
 */
int main(void)
{
    HRESULT hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("cwinrt_init failed: 0x%08lx\n", (unsigned long)hr);
        return 1;
    }
    printf("cwinrt smoke: init ok\n");
    cwinrt_uninit();
    return 0;
}
