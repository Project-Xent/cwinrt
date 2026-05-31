#include <cwinrt/factory.h>
#include <activation.h>
#include <inspectable.h>
#include <stdint.h>
#include <roapi.h>
#include <winstring.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

typedef struct factory_entry {
    wchar_t *name;
    IActivationFactory *factory;
} factory_entry;

/* Activation factories are process-lifetime singletons; this caches one per
   class name. Linear lookup is fine — an app touches at most a few dozen
   distinct classes and activation is dominated by the COM call, not the scan.
   The cache grows on demand (no fixed cap). */
static factory_entry *g_cache;
static int g_cache_count;
static int g_cache_cap;

static factory_entry *factory_find(const wchar_t *class_name)
{
    int i;
    for (i = 0; i < g_cache_count; i++) {
        if (wcscmp(g_cache[i].name, class_name) == 0)
            return &g_cache[i];
    }
    return NULL;
}

static int iid_is_activation_factory(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IActivationFactory) != 0;
}

static int iid_is_inspectable(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IInspectable) != 0;
}

static int iid_is_null(REFIID iid)
{
    static const IID k_null = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };
    if (!iid)
        return 1;
    return IsEqualGUID(iid, &k_null) != 0;
}

static HRESULT factory_try_iids_from_inspectable(IInspectable *obj, void **out)
{
    ULONG  n = 0;
    IID   *iids = NULL;
    HRESULT hr;
    uint32_t i;

    if (!obj || !out)
        return E_POINTER;
    *out = NULL;
    hr = obj->lpVtbl->GetIids(obj, &n, &iids);
    if (FAILED(hr) || !n || !iids)
        return hr;
    for (i = 0; i < n; i++) {
        if (iid_is_inspectable(&iids[i]) || iid_is_activation_factory(&iids[i]))
            continue;
        hr = obj->lpVtbl->QueryInterface(obj, &iids[i], out);
        if (SUCCEEDED(hr) && *out)
            break;
        *out = NULL;
    }
    CoTaskMemFree(iids);
    return *out ? S_OK : E_NOINTERFACE;
}

static HRESULT factory_resolve_statics(HSTRING hs, REFIID optional_iid, void **out)
{
    IActivationFactory *f = NULL;
    IInspectable        *ins = NULL;
    HRESULT              hr;

    if (optional_iid && !iid_is_null(optional_iid)) {
        hr = RoGetActivationFactory(hs, optional_iid, out);
        if (SUCCEEDED(hr))
            return hr;
    }

    hr = RoGetActivationFactory(hs, &IID_IActivationFactory, (void **)&f);
    if (FAILED(hr))
        return hr;

    if (optional_iid && !iid_is_null(optional_iid)) {
        hr = f->lpVtbl->QueryInterface(f, optional_iid, out);
        if (SUCCEEDED(hr)) {
            f->lpVtbl->Release(f);
            return hr;
        }
    }

    ins = (IInspectable *)f;
    hr = factory_try_iids_from_inspectable(ins, out);
    f->lpVtbl->Release(f);
    return hr;
}

typedef HRESULT(STDMETHODCALLTYPE *pfn_ActivateInstance)(IActivationFactory *self, IInspectable **instance);

static HRESULT factory_activate_instance(
    IActivationFactory *fact,
    HSTRING              hs,
    REFIID               iid,
    void                **out)
{
    HRESULT              hr;
    IInspectable        *inst = NULL;

    if (!hs || !out)
        return E_POINTER;
    *out = NULL;

    /* iid == NULL: default-constructor activation. The activated IInspectable IS
       the object's default interface, so return it directly (no QI). Otherwise
       QI to the requested interface. */
    if (fact) {
        pfn_ActivateInstance act = (pfn_ActivateInstance)((void **)fact->lpVtbl)[6];
        hr = act(fact, &inst);
        if (SUCCEEDED(hr) && inst) {
            if (!iid) {
                *out = inst;
                return S_OK;
            }
            hr = inst->lpVtbl->QueryInterface(inst, iid, out);
            inst->lpVtbl->Release(inst);
            if (SUCCEEDED(hr))
                return hr;
        }
        inst = NULL;
    }

    hr = RoActivateInstance(hs, &inst);
    if (FAILED(hr))
        return hr;
    if (!iid) {
        *out = inst;
        return S_OK;
    }
    hr = inst->lpVtbl->QueryInterface(inst, iid, out);
    inst->lpVtbl->Release(inst);
    return hr;
}

/* Cache `f` under `class_name`, ADOPTING the caller's reference (no AddRef). On
   success the cache owns the reference; on failure the caller still owns it. */
static HRESULT factory_store(const wchar_t *class_name, IActivationFactory *f)
{
    factory_entry *e;
    size_t nlen;
    if (g_cache_count >= g_cache_cap) {
        int            ncap = g_cache_cap ? g_cache_cap * 2 : 32;
        factory_entry *grown = (factory_entry *)realloc(g_cache, (size_t)ncap * sizeof(*grown));
        if (!grown)
            return E_OUTOFMEMORY;
        g_cache = grown;
        g_cache_cap = ncap;
    }
    nlen = wcslen(class_name) + 1;
    e = &g_cache[g_cache_count];
    e->name = (wchar_t *)malloc(nlen * sizeof(wchar_t));
    if (!e->name)
        return E_OUTOFMEMORY;
    memcpy(e->name, class_name, nlen * sizeof(wchar_t));
    e->factory = f;
    g_cache_count++;
    return S_OK;
}

HRESULT cwinrt_factory_get_statics(
    const wchar_t *class_name,
    REFIID         statics_iid,
    void         **out)
{
    HRESULT hr;
    HSTRING hs = NULL;

    if (!class_name || !out)
        return E_POINTER;
    *out = NULL;

    hr = WindowsCreateString(class_name, (UINT32)wcslen(class_name), &hs);
    if (FAILED(hr))
        return hr;

    hr = factory_resolve_statics(hs, statics_iid, out);
    WindowsDeleteString(hs);
    return hr;
}

HRESULT cwinrt_factory_activate(
    const wchar_t *class_name,
    REFIID iid,
    void **out)
{
    HRESULT              hr;
    HSTRING              hs = NULL;
    factory_entry       *e;
    IActivationFactory  *fact = NULL;

    if (!class_name || !out)
        return E_POINTER;
    *out = NULL;

    hr = WindowsCreateString(class_name, (UINT32)wcslen(class_name), &hs);
    if (FAILED(hr))
        return hr;

    e = factory_find(class_name);
    if (e) {
        hr = factory_activate_instance(e->factory, hs, iid, out); /* cache owns the ref */
    }
    else {
        hr = RoGetActivationFactory(hs, &IID_IActivationFactory, (void **)&fact);
        if (FAILED(hr) || !fact) {
            hr = factory_activate_instance(NULL, hs, iid, out); /* fall back to RoActivateInstance */
        }
        else if (factory_store(class_name, fact) == S_OK) {
            hr = factory_activate_instance(fact, hs, iid, out); /* cache adopted the ref */
        }
        else {
            hr = factory_activate_instance(fact, hs, iid, out); /* couldn't cache: use then drop */
            fact->lpVtbl->Release(fact);
        }
    }
    WindowsDeleteString(hs);
    return hr;
}

void cwinrt_factory_clear(void)
{
    int i;
    for (i = 0; i < g_cache_count; i++) {
        free(g_cache[i].name);
        g_cache[i].name = NULL;
        if (g_cache[i].factory) {
            g_cache[i].factory->lpVtbl->Release(g_cache[i].factory);
            g_cache[i].factory = NULL;
        }
    }
    free(g_cache);
    g_cache = NULL;
    g_cache_count = 0;
    g_cache_cap = 0;
}
