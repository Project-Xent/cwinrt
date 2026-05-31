/* Runtime: DispatcherQueue bootstrap + real Compositor.CreateSpriteVisual. */
#include <cwinrt/init.h>
#include <cwinrt/bootstrap.h>
#include <cwinrt/factory.h>
#include <cwinrt/iid.h>
#include <cwinrt/Windows.UI.Composition.h>
#include <unknwn.h>
#include <stdio.h>

static const wchar_t kCompositorClass[] = L"Windows.UI.Composition.Compositor";

int main(void)
{
    HRESULT         hr;
    IUnknown       *dq_controller = NULL;
    WUC_Comp *comp = NULL;
    WUC_Sprite *sprite = NULL;

    hr = cwinrt_init(RO_INIT_SINGLETHREADED);
    if (FAILED(hr))
        return 1;

    hr = cwinrt_bootstrap_dispatcher_queue(&dq_controller);
    if (FAILED(hr)) {
        printf("compositor runtime: dispatcher bootstrap failed 0x%08lx\n", (unsigned long)hr);
        cwinrt_uninit();
        return 1;
    }

    hr = cwinrt_factory_activate(
        kCompositorClass,
        &CWINRT_IID_IInspectable,
        (void **)&comp);
    if (FAILED(hr) || !comp) {
        printf("compositor runtime: activate failed 0x%08lx\n", (unsigned long)hr);
        if (hr == (HRESULT)0x80070005) {
            printf(
                "  hint: run scripts/register_conform_sparse.ps1 then rebuild this target\n"
                "  (sparse package identity for unpackaged WinRT Composition)\n");
        }
        if (dq_controller)
            dq_controller->lpVtbl->Release(dq_controller);
        cwinrt_uninit();
        return 1;
    }

    hr = wuc_comp_create_sprite_visual(comp, &sprite);
    if (FAILED(hr)) {
        printf("compositor runtime: CreateSpriteVisual failed 0x%08lx\n", (unsigned long)hr);
        ((IUnknown *)comp)->lpVtbl->Release((IUnknown *)comp);
        if (dq_controller)
            dq_controller->lpVtbl->Release(dq_controller);
        cwinrt_uninit();
        return 1;
    }

    if (sprite)
        ((IUnknown *)sprite)->lpVtbl->Release((IUnknown *)sprite);
    ((IUnknown *)comp)->lpVtbl->Release((IUnknown *)comp);
    if (dq_controller)
        dq_controller->lpVtbl->Release(dq_controller);
    printf("conform compositor runtime: CreateSpriteVisual ok\n");
    cwinrt_uninit();
    return 0;
}
