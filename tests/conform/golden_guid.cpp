/* Golden: cwinrt Foundation GuidHelper vs system GUID API. */
#include <cwinrt/init.h>
extern "C" {
#include <cwinrt/Windows.Foundation.h>
}
#include <objbase.h>
#include <stdio.h>

int main()
{
    GUID  a{};
    GUID  b{};
    HRESULT hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;
    hr = CoCreateGuid(&a);
    if (FAILED(hr))
        return 1;
    hr = wf_guid_helper_create_new_guid(&b);
    if (FAILED(hr)) {
        printf("golden: wf_guid_helper_create_new_guid failed 0x%08lx\n", (unsigned long)hr);
        return 1;
    }
  /* Both succeed; values differ by design. */
    printf("golden: GuidHelper dispatch ok\n");
    cwinrt_uninit();
    return 0;
}
