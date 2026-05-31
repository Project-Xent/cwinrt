#include <cwinrt/init.h>
#include <cwinrt/factory.h>

static LONG g_init_count;

HRESULT cwinrt_init(RO_INIT_TYPE type)
{
    LONG n = InterlockedIncrement(&g_init_count);
    if (n != 1)
        return S_OK;
    return RoInitialize(type);
}

void cwinrt_uninit(void)
{
    LONG n = InterlockedDecrement(&g_init_count);
    if (n > 0)
        return;
    if (n < 0) {
        InterlockedIncrement(&g_init_count);
        return;
    }
    cwinrt_factory_clear();
    RoUninitialize();
}
