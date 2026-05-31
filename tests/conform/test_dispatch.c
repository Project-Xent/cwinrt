/* Runtime: Foundation.Uri factory + instance vtable dispatch. */
#include <cwinrt/init.h>
#include <cwinrt/factory.h>
#include <cwinrt/Windows.Foundation.h>
#include <winstring.h>
#include <unknwn.h>
#include <stdio.h>

/* IUriRuntimeClassFactory — activation factory for Uri (unpackaged-safe). */
static const IID kUriFactoryIid = {
    0x44A9796F,
    0x723E,
    0x4FDF,
    { 0xA2, 0x18, 0x75, 0x3E, 0x01, 0xA2, 0xB8, 0xFC }
};

static const wchar_t kUriClass[] = L"Windows.Foundation.Uri";

int main(void)
{
    HRESULT                    hr;
    WF_IUriRuntimeClassFactory *factory = NULL;
    HSTRING                    hs = NULL;
    WF_Uri                    *uri = NULL;
    cwinrt_hstring             raw = NULL;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;

    hr = cwinrt_factory_get_statics(kUriClass, &kUriFactoryIid, (void **)&factory);
    if (FAILED(hr) || !factory) {
        printf("dispatch: get Uri factory failed 0x%08lx\n", (unsigned long)hr);
        cwinrt_uninit();
        return 1;
    }

    hr = WindowsCreateString(L"https://example.com/", 20, &hs);
    if (FAILED(hr)) {
        ((IUnknown *)factory)->lpVtbl->Release((IUnknown *)factory);
        cwinrt_uninit();
        return 1;
    }

    hr = wf_uri_runtime_class_factory_create_uri(factory, hs, &uri);
    WindowsDeleteString(hs);
    ((IUnknown *)factory)->lpVtbl->Release((IUnknown *)factory);
    if (FAILED(hr) || !uri) {
        printf("dispatch: CreateUri failed 0x%08lx\n", (unsigned long)hr);
        cwinrt_uninit();
        return 1;
    }

    hr = wf_uri_get__raw_uri(uri, &raw);
    if (FAILED(hr)) {
        printf("dispatch: get RawUri failed 0x%08lx\n", (unsigned long)hr);
        ((IUnknown *)uri)->lpVtbl->Release((IUnknown *)uri);
        cwinrt_uninit();
        return 1;
    }

    if (raw)
        WindowsDeleteString(raw);
    ((IUnknown *)uri)->lpVtbl->Release((IUnknown *)uri);
    printf("conform dispatch: Uri vtable dispatch ok\n");
    cwinrt_uninit();
    return 0;
}
