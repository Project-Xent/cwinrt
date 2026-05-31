#include <cwinrt/init.h>
#include <cwinrt/async.h>
#include <unknwn.h>
#include <stdio.h>

enum {
    IAsyncInfo_slot_get_Status = 7,
    IAsyncInfo_slot_get_ErrorCode = 8
};

typedef enum {
    AsyncStatus_Started = 0,
    AsyncStatus_Completed = 1,
    AsyncStatus_Canceled = 2,
    AsyncStatus_Error = 3
} AsyncStatus;

static const IID IID_IAsyncInfo = {
    0x00000036,
    0x0000,
    0x0000,
    { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

typedef HRESULT(STDMETHODCALLTYPE *pfn_get_Status)(IUnknown *self, AsyncStatus *status);
typedef HRESULT(STDMETHODCALLTYPE *pfn_get_ErrorCode)(IUnknown *self, HRESULT *code);

static HRESULT STDMETHODCALLTYPE mock_qi(IUnknown *self, REFIID riid, void **pp)
{
    (void)self;
    if (!pp)
        return E_POINTER;
    *pp = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAsyncInfo)) {
        *pp = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
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

static HRESULT STDMETHODCALLTYPE mock_get_status(IUnknown *self, AsyncStatus *st)
{
    (void)self;
    if (!st)
        return E_POINTER;
    *st = AsyncStatus_Completed;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mock_get_error(IUnknown *self, HRESULT *code)
{
    (void)self;
    if (!code)
        return E_POINTER;
    *code = S_OK;
    return S_OK;
}

typedef struct {
    IUnknownVtbl *lpVtbl;
} mock_async;

int main(void)
{
    static void *vtbl[16];
    mock_async  obj;
    IUnknown   *async = NULL;
    HRESULT     hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;

    hr = cwinrt_async_wait(NULL, 0);
    if (hr != E_POINTER) {
        printf("async: expected E_POINTER got 0x%08lx\n", (unsigned long)hr);
        cwinrt_uninit();
        return 1;
    }

    vtbl[0] = (void *)mock_qi;
    vtbl[1] = (void *)mock_addref;
    vtbl[2] = (void *)mock_release;
    vtbl[IAsyncInfo_slot_get_Status] = (void *)mock_get_status;
    vtbl[IAsyncInfo_slot_get_ErrorCode] = (void *)mock_get_error;
    obj.lpVtbl = (IUnknownVtbl *)&vtbl;
    async = (IUnknown *)&obj;

    hr = cwinrt_async_wait(async, 1000);
    if (FAILED(hr)) {
        printf("async: wait completed mock failed 0x%08lx\n", (unsigned long)hr);
        cwinrt_uninit();
        return 1;
    }

    printf("conform async: wait completed ok\n");
    cwinrt_uninit();
    return 0;
}
