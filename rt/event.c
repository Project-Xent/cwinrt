#include <cwinrt/event.h>

#include <stdlib.h>

typedef HRESULT(STDMETHODCALLTYPE *pfn_add)(IUnknown *self, IUnknown *handler, cwinrt_token *token);
typedef HRESULT(STDMETHODCALLTYPE *pfn_remove)(IUnknown *self, cwinrt_token token);

typedef struct cwinrt_event_handler_obj {
    void *vtable;
    LONG  refs;
    cwinrt_event_fn fn;
    void           *ctx;
} cwinrt_event_handler_obj;

static const IID CWINRT_IID_IAgileObject = {
    0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 }
};

static HRESULT STDMETHODCALLTYPE evh_qi(IUnknown *self, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!riid)
        return E_INVALIDARG;
    /* Event sources keep the handler as IUnknown and may QI it for IAgileObject. */
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &CWINRT_IID_IAgileObject)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE evh_addref(IUnknown *self)
{
    cwinrt_event_handler_obj *h = (cwinrt_event_handler_obj *)self;
    return (ULONG)InterlockedIncrement(&h->refs);
}

static ULONG STDMETHODCALLTYPE evh_release(IUnknown *self)
{
    cwinrt_event_handler_obj *h = (cwinrt_event_handler_obj *)self;
    ULONG n = (ULONG)InterlockedDecrement(&h->refs);
    if (n == 0)
        free(h);
    return n;
}

static HRESULT STDMETHODCALLTYPE evh_invoke(
    IUnknown *self,
    IUnknown *sender,
    IUnknown *args)
{
    cwinrt_event_handler_obj *h = (cwinrt_event_handler_obj *)self;
    if (h->fn)
        h->fn(sender, args, h->ctx);
    return S_OK;
}

static void *g_evh_vtbl[16];

static void cwinrt_event_handler_init_vtbl(void)
{
    static int once;
    if (once)
        return;
    g_evh_vtbl[0] = (void *)evh_qi;
    g_evh_vtbl[1] = (void *)evh_addref;
    g_evh_vtbl[2] = (void *)evh_release;
    /* WinRT delegates are IUnknown-based: Invoke is slot 3, not 6. */
    g_evh_vtbl[3] = (void *)evh_invoke;
    once = 1;
}

HRESULT cwinrt_event_handler_create(cwinrt_event_fn fn, void *ctx, IUnknown **out)
{
    cwinrt_event_handler_obj *h;
    if (!out)
        return E_POINTER;
    *out = NULL;
    cwinrt_event_handler_init_vtbl();
    h = (cwinrt_event_handler_obj *)calloc(1, sizeof(*h));
    if (!h)
        return E_OUTOFMEMORY;
    h->vtable = g_evh_vtbl;
    h->refs = 1;
    h->fn = fn;
    h->ctx = ctx;
    *out = (IUnknown *)h;
    return S_OK;
}

HRESULT cwinrt_event_subscribe(
    IUnknown           *source,
    uint32_t            add_slot,
    uint32_t            remove_slot,
    IUnknown           *handler,
    cwinrt_event_handle *out)
{
    pfn_add fn;
    HRESULT hr;

    if (!source || !handler || !out)
        return E_POINTER;
    if (!add_slot || !remove_slot)
        return E_INVALIDARG;
    out->source = NULL;
    out->remove_slot = remove_slot;
    out->token.value = 0;
    fn = (pfn_add)((void **)source->lpVtbl)[add_slot];
    hr = fn(source, handler, &out->token);
    if (FAILED(hr))
        return hr;
    out->source = source;
    source->lpVtbl->AddRef(source);
    return S_OK;
}

HRESULT cwinrt_event_unsubscribe(cwinrt_event_handle *handle)
{
    pfn_remove fn;
    HRESULT   hr;

    if (!handle || !handle->source || !handle->remove_slot)
        return E_POINTER;
    fn = (pfn_remove)((void **)handle->source->lpVtbl)[handle->remove_slot];
    hr = fn(handle->source, handle->token);
    handle->source->lpVtbl->Release(handle->source);
    handle->source = NULL;
    handle->remove_slot = 0;
    handle->token.value = 0;
    return hr;
}
