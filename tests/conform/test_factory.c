#include <cwinrt/init.h>
#include <cwinrt/factory.h>
#include <roapi.h>
#include <winstring.h>
#include <unknwn.h>
#include <stdio.h>

static const wchar_t kUriClass[] = L"Windows.Foundation.Uri";

int main(void)
{
    HRESULT hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;

    {
        HSTRING              hs = NULL;
        IActivationFactory  *f = NULL;
        hr = WindowsCreateString(kUriClass, (UINT32)wcslen(kUriClass), &hs);
        if (SUCCEEDED(hr))
            hr = RoGetActivationFactory(hs, &IID_IActivationFactory, (void **)&f);
        if (hs)
            WindowsDeleteString(hs);
        if (FAILED(hr) || !f) {
            printf("factory RoGetActivationFactory failed: 0x%08lx\n", (unsigned long)hr);
            cwinrt_uninit();
            return 1;
        }
        f->lpVtbl->Release(f);
    }
    printf("conform factory: activation factory ok\n");
    cwinrt_uninit();
    return 0;
}
