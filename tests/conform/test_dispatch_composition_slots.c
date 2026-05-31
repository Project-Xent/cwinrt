/* ABI: verify multiple Compositor vtable slots used by generated thunks. */
#include <inspectable.h>
#include <unknwn.h>
#include <stdio.h>

typedef struct WUC_Comp WUC_Comp;
typedef struct WUC_Sprite WUC_Sprite;
typedef struct WUC_Container WUC_Container;

HRESULT wuc_comp_create_sprite_visual(WUC_Comp *self, WUC_Sprite **retval);
HRESULT wuc_comp_create_container_visual(WUC_Comp *self, WUC_Container **retval);

#define SLOT_CREATE_SPRITE_VISUAL 22u
#define SLOT_CREATE_CONTAINER_VISUAL 9u

static HRESULT STDMETHODCALLTYPE mock_qi(IUnknown *self, REFIID riid, void **pp)
{
    (void)self;
    (void)riid;
    (void)pp;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mock_addref(IUnknown *self)
{
    (void)self;
    return 2;
}

static ULONG STDMETHODCALLTYPE mock_release(IUnknown *self)
{
    (void)self;
    return 1;
}

static HRESULT STDMETHODCALLTYPE mock_get_iids(IInspectable *self, ULONG *n, IID **iids)
{
    (void)self;
    *n = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mock_get_class(IInspectable *self, HSTRING *name)
{
    (void)self;
    if (name)
        *name = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mock_get_trust(IInspectable *self, TrustLevel *level)
{
    (void)self;
    if (level)
        *level = FullTrust;
    return S_OK;
}

static char g_sprite_sentinel;
static char g_container_sentinel;

static HRESULT STDMETHODCALLTYPE mock_create_sprite(void *self, WUC_Sprite **retval)
{
    (void)self;
    if (!retval)
        return E_POINTER;
    *retval = (WUC_Sprite *)&g_sprite_sentinel;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mock_create_container(void *self, WUC_Container **retval)
{
    (void)self;
    if (!retval)
        return E_POINTER;
    *retval = (WUC_Container *)&g_container_sentinel;
    return S_OK;
}

typedef struct {
    IUnknownVtbl *lpVtbl;
} mock_comp;

int main(void)
{
    static void *vtbl[32];
    mock_comp       obj;
    WUC_Comp       *comp;
    WUC_Sprite     *sprite = NULL;
    WUC_Container  *container = NULL;
    HRESULT         hr;

    vtbl[0] = (void *)mock_qi;
    vtbl[1] = (void *)mock_addref;
    vtbl[2] = (void *)mock_release;
    vtbl[3] = (void *)mock_get_iids;
    vtbl[4] = (void *)mock_get_class;
    vtbl[5] = (void *)mock_get_trust;
    vtbl[SLOT_CREATE_SPRITE_VISUAL] = (void *)mock_create_sprite;
    vtbl[SLOT_CREATE_CONTAINER_VISUAL] = (void *)mock_create_container;

    obj.lpVtbl = (IUnknownVtbl *)&vtbl;
    comp = (WUC_Comp *)&obj;

    hr = wuc_comp_create_sprite_visual(comp, &sprite);
    if (FAILED(hr) || !sprite) {
        printf("composition slots: sprite failed 0x%08lx\n", (unsigned long)hr);
        return 1;
    }
    hr = wuc_comp_create_container_visual(comp, &container);
    if (FAILED(hr) || !container) {
        printf("composition slots: container failed 0x%08lx\n", (unsigned long)hr);
        return 1;
    }
    printf("conform composition slots: %u %u ok\n", SLOT_CREATE_SPRITE_VISUAL, SLOT_CREATE_CONTAINER_VISUAL);
    return 0;
}
