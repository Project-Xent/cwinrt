/* Diagnostic: isolate RoGetActivationFactory / statics / Compositor activation. */
#include <cwinrt/init.h>
#include <cwinrt/iid.h>
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <stdint.h>
#include <stdio.h>

static const IID IID_IGuidHelperStatics_runtime = {
    0x59C7966B,
    0xAE52,
    0x5283,
    { 0xAD, 0x7F, 0xA1, 0xB9, 0xE9, 0x67, 0x8A, 0xDD }
};

static const IID IID_ICompositor = {
    0x4EC2B192,
    0x7C8F,
    0x4F0F,
    { 0x94, 0x4B, 0x29, 0xD2, 0xCD, 0x12, 0x3E, 0x04 }
};

static void try_statics(const wchar_t *cls, REFIID iid, const char *label)
{
    HSTRING hs = NULL;
    void   *p = NULL;
    HRESULT hr;
    hr = WindowsCreateString(cls, (UINT32)wcslen(cls), &hs);
    if (FAILED(hr)) {
        printf("%s: CreateString 0x%08lx\n", label, (unsigned long)hr);
        return;
    }
    hr = RoGetActivationFactory(hs, iid, &p);
    printf("%s: RoGetActivationFactory(statics_iid) -> 0x%08lx p=%p\n", label, (unsigned long)hr, p);
    if (p)
        ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
    p = NULL;
    hr = RoGetActivationFactory(hs, &IID_IActivationFactory, &p);
    printf("%s: RoGetActivationFactory(IActivationFactory) -> 0x%08lx p=%p\n", label, (unsigned long)hr, p);
    if (p) {
        void   *st = NULL;
        HRESULT hr2 = ((IUnknown *)p)->lpVtbl->QueryInterface((IUnknown *)p, iid, &st);
        GUID    g = { 0 };
        typedef HRESULT(STDMETHODCALLTYPE *pfn_CreateNewGuid)(void *self, GUID *retval);
        pfn_CreateNewGuid create =
            (pfn_CreateNewGuid)((void **)((IUnknown *)p)->lpVtbl)[6];
        HRESULT hr3 = create(p, &g);
        printf("%s: QI to statics -> 0x%08lx p=%p\n", label, (unsigned long)hr2, st);
        printf("%s: vtable[6] as CreateNewGuid -> 0x%08lx\n", label, (unsigned long)hr3);
        {
            ULONG  n = 0;
            IID   *iids = NULL;
            HRESULT hr4 =
                ((IInspectable *)p)->lpVtbl->GetIids((IInspectable *)p, &n, &iids);
            uint32_t k;
            printf("%s: GetIids -> 0x%08lx count=%lu\n", label, (unsigned long)hr4, (unsigned long)n);
            for (k = 0; k < n && iids; k++) {
                printf(
                    "  iid[%u] {%08lx-%04x-%04x-{%02x%02x-%02x%02x%02x%02x%02x%02x}}\n",
                    k,
                    (unsigned long)iids[k].Data1,
                    (unsigned)iids[k].Data2,
                    (unsigned)iids[k].Data3,
                    iids[k].Data4[0],
                    iids[k].Data4[1],
                    iids[k].Data4[2],
                    iids[k].Data4[3],
                    iids[k].Data4[4],
                    iids[k].Data4[5],
                    iids[k].Data4[6],
                    iids[k].Data4[7]);
            }
            if (iids)
                CoTaskMemFree(iids);
        }
        if (st)
            ((IUnknown *)st)->lpVtbl->Release((IUnknown *)st);
        ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
    }
    WindowsDeleteString(hs);
}

typedef HRESULT(STDMETHODCALLTYPE *pfn_ActivateInstance)(IActivationFactory *self, IInspectable **instance);

static void try_instance(const wchar_t *cls, REFIID iid, const char *label)
{
    HSTRING              hs = NULL;
    IActivationFactory  *fact = NULL;
    IInspectable        *inst = NULL;
    void                *qi = NULL;
    HRESULT              hr;
    hr = WindowsCreateString(cls, (UINT32)wcslen(cls), &hs);
    if (FAILED(hr)) {
        printf("%s: CreateString 0x%08lx\n", label, (unsigned long)hr);
        return;
    }
    hr = RoActivateInstance(hs, &inst);
    printf("%s: RoActivateInstance -> 0x%08lx inst=%p\n", label, (unsigned long)hr, (void *)inst);
    if (inst)
        inst->lpVtbl->Release(inst);
    inst = NULL;
    hr = RoGetActivationFactory(hs, &IID_IActivationFactory, (void **)&fact);
    printf("%s: RoGetActivationFactory -> 0x%08lx fact=%p\n", label, (unsigned long)hr, (void *)fact);
    if (SUCCEEDED(hr) && fact) {
        pfn_ActivateInstance act = (pfn_ActivateInstance)((void **)fact->lpVtbl)[6];
        hr = act(fact, &inst);
        printf("%s: IActivationFactory.ActivateInstance -> 0x%08lx inst=%p\n", label, (unsigned long)hr, (void *)inst);
        if (SUCCEEDED(hr) && inst) {
            hr = inst->lpVtbl->QueryInterface(inst, iid, &qi);
            printf("%s: QI after ActivateInstance -> 0x%08lx p=%p\n", label, (unsigned long)hr, qi);
            if (qi)
                ((IUnknown *)qi)->lpVtbl->Release((IUnknown *)qi);
            inst->lpVtbl->Release(inst);
        }
        fact->lpVtbl->Release(fact);
    }
    WindowsDeleteString(hs);
}

int main(void)
{
    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return 1;
    try_statics(L"Windows.Foundation.GuidHelper", &IID_IGuidHelperStatics_runtime, "GuidHelper");
    try_instance(L"Windows.UI.Composition.Compositor", &CWINRT_IID_IInspectable, "Compositor/IInspectable");
    try_instance(L"Windows.UI.Composition.Compositor", &IID_ICompositor, "Compositor/ICompositor");
    RoUninitialize();
    return 0;
}
