/* Golden: cwinrt Foundation GuidHelper vs system GUID API. */
#include <cwinrt/init.h>
#include <cwinrt/Windows.Foundation.h>
#include <objbase.h>
#include <stdio.h>

int main(void) {
	GUID    a, b;
	HRESULT hr;

	hr = cwinrt_init(RO_INIT_MULTITHREADED);
	if (FAILED(hr)) {
		printf("golden: init failed 0x%08lx\n", ( unsigned long ) hr);
		return 1;
	}
	hr = CoCreateGuid(&a);
	if (FAILED(hr)) return 1;
	hr = wf_guid_create_new_guid(&b);
	if (FAILED(hr)) {
		printf("golden: wf_guid_create_new_guid failed 0x%08lx\n", ( unsigned long ) hr);
		return 1;
	}
	printf("golden: GuidHelper dispatch ok\n");
	cwinrt_uninit();
	return 0;
}
