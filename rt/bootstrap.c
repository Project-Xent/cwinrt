#include <cwinrt/bootstrap.h>
#include <cwinrt/factory.h>
#include <unknwn.h>

typedef HRESULT(STDMETHODCALLTYPE *pfn_create_on_current_thread)(void *self, void **controller);

static HRESULT bootstrap_call_create_on_current_thread(void *statics, void **controller)
{
    pfn_create_on_current_thread fn;
    fn = (pfn_create_on_current_thread)((void **)((IUnknown *)statics)->lpVtbl)[6];
    return fn(statics, controller);
}

HRESULT cwinrt_bootstrap_dispatcher_queue(IUnknown **out)
{
    IUnknown *statics = NULL;
    HRESULT   hr;

    if (!out)
        return E_POINTER;
    *out = NULL;

    hr = cwinrt_factory_get_statics(
        L"Windows.System.DispatcherQueueController",
        NULL,
        (void **)&statics);
    if (FAILED(hr))
        return hr;

    hr = bootstrap_call_create_on_current_thread(statics, (void **)out);
    statics->lpVtbl->Release(statics);
    return hr;
}
