#include <cwinrt/delegate.h>

#include <stdlib.h>

/* IAgileObject has the same layout as IUnknown (marker interface); WinRT objects
   are routinely QI'd for it, so a free-threaded shim should answer it. */
static const IID CWINRT_IID_IAgileObject = {
    0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 }
};

typedef struct cwinrt_delegate_obj {
    void              *vtbl; /* &g_delegate_vtbl: QI / AddRef / Release / Invoke */
    LONG               refs;
    IID                iid;  /* this delegate's own IID, for QueryInterface */
    cwinrt_delegate_fn fn;
    void              *ctx;
} cwinrt_delegate_obj;

static HRESULT STDMETHODCALLTYPE del_qi(IUnknown *self, REFIID riid, void **out)
{
    cwinrt_delegate_obj *d = (cwinrt_delegate_obj *)self;
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!riid)
        return E_INVALIDARG;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &CWINRT_IID_IAgileObject)
        || IsEqualGUID(riid, &d->iid)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE del_addref(IUnknown *self)
{
    cwinrt_delegate_obj *d = (cwinrt_delegate_obj *)self;
    return (ULONG)InterlockedIncrement(&d->refs);
}

static ULONG STDMETHODCALLTYPE del_release(IUnknown *self)
{
    cwinrt_delegate_obj *d = (cwinrt_delegate_obj *)self;
    ULONG n = (ULONG)InterlockedDecrement(&d->refs);
    if (n == 0)
        free(d);
    return n;
}

/* Delegate Invoke is vtable slot 3 (right after IUnknown). Two-argument shape:
   Invoke(self, sender, args). */
static HRESULT STDMETHODCALLTYPE del_invoke(IUnknown *self, void *sender, void *args)
{
    cwinrt_delegate_obj *d = (cwinrt_delegate_obj *)self;
    if (d->fn)
        d->fn(sender, args, d->ctx);
    return S_OK;
}

typedef struct cwinrt_delegate_vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG(STDMETHODCALLTYPE *Release)(IUnknown *);
    HRESULT(STDMETHODCALLTYPE *Invoke)(IUnknown *, void *, void *);
} cwinrt_delegate_vtbl;

static const cwinrt_delegate_vtbl g_delegate_vtbl = {
    del_qi, del_addref, del_release, del_invoke
};

HRESULT cwinrt_delegate_create(REFIID iid, cwinrt_delegate_fn fn, void *ctx, IUnknown **out)
{
    cwinrt_delegate_obj *d;
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!iid)
        return E_INVALIDARG;
    d = (cwinrt_delegate_obj *)calloc(1, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->vtbl = (void *)&g_delegate_vtbl;
    d->refs = 1;
    d->iid = *iid;
    d->fn = fn;
    d->ctx = ctx;
    *out = (IUnknown *)d;
    return S_OK;
}
