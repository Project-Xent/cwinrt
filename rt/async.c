#include <cwinrt/async.h>
#include <cwinrt/cast.h>

#include <stdlib.h>
#include <stdint.h>

/* IAsyncInfo: ABI slots after IInspectable (6). */
enum {
    IAsyncInfo_slot_get_Status = 7,
    IAsyncInfo_slot_get_ErrorCode = 8
};

/* put_Completed / GetResults slots differ by async interface shape:
   IAsyncAction / IAsyncOperation<T>           : Completed=6, GetResults=8
   IAsyncActionWithProgress<P> / WithProgress<T,P>: a put/get_Progress pair is
   inserted first, so Completed=8, GetResults=10. The caller (which holds a typed
   pointer and knows the shape) selects via the *_with_progress entry points. */
enum {
    ASYNC_SLOT_COMPLETED          = 6,
    ASYNC_SLOT_GETRESULTS         = 8,
    ASYNC_SLOT_COMPLETED_PROGRESS = 8,
    ASYNC_SLOT_GETRESULTS_PROGRESS = 10
};

static const IID IID_IAsyncInfo = {
    0x00000036,
    0x0000,
    0x0000,
    { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

static const IID IID_AsyncActionCompletedHandler = {
    0x3a2dca94,
    0x8953,
    0x45a2,
    { 0x84, 0x47, 0x4d, 0xda, 0xc5, 0x59, 0x57, 0x8b }
};

typedef enum {
    AsyncStatus_Started = 0,
    AsyncStatus_Completed = 1,
    AsyncStatus_Canceled = 2,
    AsyncStatus_Error = 3
} AsyncStatus;

typedef HRESULT(STDMETHODCALLTYPE *pfn_get_Status)(IUnknown *self, AsyncStatus *status);
typedef HRESULT(STDMETHODCALLTYPE *pfn_get_ErrorCode)(IUnknown *self, HRESULT *code);
typedef HRESULT(STDMETHODCALLTYPE *pfn_put_Completed)(IUnknown *self, IUnknown *handler);

typedef struct IAsyncActionCompletedHandlerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG(STDMETHODCALLTYPE *Release)(IUnknown *);
    HRESULT(STDMETHODCALLTYPE *Invoke)(IUnknown *, IUnknown *, AsyncStatus);
} IAsyncActionCompletedHandlerVtbl;

typedef struct async_wait_handler {
    IAsyncActionCompletedHandlerVtbl const *lpVtbl;
    LONG                                    refs;
    HANDLE                                  done;
    HRESULT                                 wait_hr;
} async_wait_handler;

static HRESULT STDMETHODCALLTYPE async_handler_QueryInterface(
    IUnknown *self,
    REFIID    riid,
    void    **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_AsyncActionCompletedHandler)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE async_handler_AddRef(IUnknown *self)
{
    async_wait_handler *h = (async_wait_handler *)self;
    return (ULONG)InterlockedIncrement(&h->refs);
}

static ULONG STDMETHODCALLTYPE async_handler_Release(IUnknown *self)
{
    async_wait_handler *h = (async_wait_handler *)self;
    ULONG n = (ULONG)InterlockedDecrement(&h->refs);
    if (n == 0) {
        if (h->done)
            CloseHandle(h->done);
        free(h);
    }
    return n;
}

static HRESULT STDMETHODCALLTYPE async_handler_Invoke(
    IUnknown    *self,
    IUnknown    *async,
    AsyncStatus  status)
{
    async_wait_handler *h = (async_wait_handler *)self;
    IUnknown           *info = NULL;
    pfn_get_ErrorCode   get_err;

    (void)async;
    if (status == AsyncStatus_Error) {
        h->wait_hr = E_FAIL;
        if (async && SUCCEEDED(async->lpVtbl->QueryInterface(async, &IID_IAsyncInfo, (void **)&info))) {
            get_err = (pfn_get_ErrorCode)((void **)info->lpVtbl)[IAsyncInfo_slot_get_ErrorCode];
            get_err(info, &h->wait_hr);
            info->lpVtbl->Release(info);
        }
    } else if (status == AsyncStatus_Canceled)
        h->wait_hr = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    else
        h->wait_hr = S_OK;
    if (h->done)
        SetEvent(h->done);
    return S_OK;
}

static IAsyncActionCompletedHandlerVtbl const g_async_handler_vtbl = {
    async_handler_QueryInterface,
    async_handler_AddRef,
    async_handler_Release,
    async_handler_Invoke
};

static HRESULT async_wait_handler_create(async_wait_handler **out)
{
    async_wait_handler *h;

    if (!out)
        return E_POINTER;
    *out = NULL;
    h = (async_wait_handler *)calloc(1, sizeof(*h));
    if (!h)
        return E_OUTOFMEMORY;
    h->lpVtbl = &g_async_handler_vtbl;
    h->refs = 1;
    h->wait_hr = S_OK;
    h->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!h->done) {
        free(h);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *out = h;
    return S_OK;
}

static HRESULT async_terminal_hr(IUnknown *info)
{
    pfn_get_Status    get_status;
    pfn_get_ErrorCode get_err;
    AsyncStatus       st = AsyncStatus_Started;
    HRESULT           hr;

    get_status = (pfn_get_Status)((void **)info->lpVtbl)[IAsyncInfo_slot_get_Status];
    hr = get_status(info, &st);
    if (FAILED(hr))
        return hr;
    if (st == AsyncStatus_Error) {
        get_err = (pfn_get_ErrorCode)((void **)info->lpVtbl)[IAsyncInfo_slot_get_ErrorCode];
        hr = S_OK;
        get_err(info, &hr);
        return hr;
    }
    if (st == AsyncStatus_Canceled)
        return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    return S_OK;
}

static HRESULT async_wait_poll(IUnknown *info, DWORD timeout_ms)
{
    HRESULT        hr;
    DWORD          start = GetTickCount();
    pfn_get_Status get_status;

    get_status = (pfn_get_Status)((void **)info->lpVtbl)[IAsyncInfo_slot_get_Status];
    for (;;) {
        AsyncStatus st = AsyncStatus_Started;

        hr = get_status(info, &st);
        if (FAILED(hr))
            return hr;
        if (st != AsyncStatus_Started)
            return async_terminal_hr(info);
        if (timeout_ms != INFINITE) {
            DWORD elapsed = GetTickCount() - start;
            if (elapsed >= timeout_ms)
                return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        }
        Sleep(1);
    }
}

static HRESULT async_wait_slot(IUnknown *async_info, DWORD timeout_ms, unsigned completed_slot)
{
    IUnknown           *info = NULL;
    async_wait_handler *handler = NULL;
    HRESULT             hr;
    pfn_get_Status      get_status;
    AsyncStatus         st;
    pfn_put_Completed   put_completed;
    DWORD               w;

    if (!async_info)
        return E_POINTER;

    hr = async_info->lpVtbl->QueryInterface(async_info, &IID_IAsyncInfo, (void **)&info);
    if (FAILED(hr))
        return hr;

    get_status = (pfn_get_Status)((void **)info->lpVtbl)[IAsyncInfo_slot_get_Status];
    hr = get_status(info, &st);
    if (FAILED(hr))
        goto done;
    if (st != AsyncStatus_Started) {
        hr = async_terminal_hr(info);
        goto done;
    }

    hr = async_wait_handler_create(&handler);
    if (FAILED(hr))
        goto done;

    put_completed =
        (pfn_put_Completed)((void **)async_info->lpVtbl)[completed_slot];
    hr = put_completed(async_info, (IUnknown *)handler);
    if (FAILED(hr)) {
        hr = async_wait_poll(info, timeout_ms);
        goto done;
    }

    w = WaitForSingleObject(handler->done, timeout_ms);
    if (w == WAIT_TIMEOUT)
        hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    else if (w == WAIT_FAILED)
        hr = HRESULT_FROM_WIN32(GetLastError());
    else
        hr = handler->wait_hr;

    if (SUCCEEDED(hr))
        hr = async_terminal_hr(info);

done:
    if (handler)
        ((IUnknown *)handler)->lpVtbl->Release((IUnknown *)handler);
    if (info)
        info->lpVtbl->Release(info);
    return hr;
}

HRESULT cwinrt_async_wait(IUnknown *async_info, DWORD timeout_ms)
{
    return async_wait_slot(async_info, timeout_ms, ASYNC_SLOT_COMPLETED);
}

HRESULT cwinrt_async_wait_with_progress(IUnknown *async_info, DWORD timeout_ms)
{
    return async_wait_slot(async_info, timeout_ms, ASYNC_SLOT_COMPLETED_PROGRESS);
}

typedef HRESULT(STDMETHODCALLTYPE *pfn_get_results)(IUnknown *self, void **result);

static HRESULT async_get_slot(
    void *async_op, REFIID result_iid, void **result, unsigned completed_slot, unsigned getresults_slot)
{
    IUnknown        *op = (IUnknown *)async_op;
    pfn_get_results  get_results;
    void            *raw = NULL;
    HRESULT          hr;

    if (!op)
        return E_POINTER;
    if (result)
        *result = NULL;

    hr = async_wait_slot(op, INFINITE, completed_slot);
    if (FAILED(hr))
        return hr;

    if (!result)
        return S_OK; /* awaited a typed op for its side effects only */

    get_results = (pfn_get_results)((void **)op->lpVtbl)[getresults_slot];
    hr = get_results(op, &raw);
    if (FAILED(hr))
        return hr;
    if (!raw) {
        *result = NULL;
        return S_OK; /* e.g. operation produced a null reference */
    }
    if (result_iid) {
        hr = cwinrt_query(raw, result_iid, result);
        ((IUnknown *)raw)->lpVtbl->Release((IUnknown *)raw);
        return hr;
    }
    *result = raw;
    return S_OK;
}

HRESULT cwinrt_async_get(void *async_op, REFIID result_iid, void **result)
{
    return async_get_slot(async_op, result_iid, result, ASYNC_SLOT_COMPLETED, ASYNC_SLOT_GETRESULTS);
}

HRESULT cwinrt_async_get_with_progress(void *async_op, REFIID result_iid, void **result)
{
    return async_get_slot(
        async_op, result_iid, result, ASYNC_SLOT_COMPLETED_PROGRESS, ASYNC_SLOT_GETRESULTS_PROGRESS);
}
