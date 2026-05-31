/*
 * Headless check that the inbound delegate shim and async await fire on hardware.
 * ThreadPool.RunAsync takes a WorkItemHandler delegate (the cwinrt_delegate shim)
 * and returns an IAsyncAction we block on with cwinrt_async_wait. The work callback
 * sets a flag; we assert it ran.
 */
#include <cwinrt/init.h>
#include <cwinrt/delegate.h>
#include <cwinrt/async.h>
#include <cwinrt/Windows.System.Threading.h>
#include <stdio.h>

static volatile LONG g_ran = 0;

/* WorkItemHandler.Invoke(operation): one-argument delegate; sender is the
   IAsyncAction operation, args is unused. */
static void work_item(void *sender, void *args, void *ctx)
{
    (void)sender;
    (void)args;
    (void)ctx;
    InterlockedExchange(&g_ran, 1);
}

int main(void)
{
    IUnknown        *handler = NULL;
    WF_IAsyncAction *action = NULL;
    int              rc = 1;
    HRESULT          hr;

    hr = cwinrt_init(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("FAIL cwinrt_init: 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    hr = cwinrt_delegate_create(&CWINRT_IID_WSYTH_WorkItemHandler, work_item, NULL, &handler);
    if (FAILED(hr)) {
        printf("FAIL delegate_create: 0x%08lX\n", (unsigned long)hr);
        goto done;
    }

    hr = wsyth_thread_pool_run_async((WSYTH_WorkItemHandler *)handler, &action);
    if (FAILED(hr)) {
        printf("FAIL RunAsync: 0x%08lX\n", (unsigned long)hr);
        goto done;
    }

    hr = cwinrt_async_wait((IUnknown *)action, 10000);
    if (FAILED(hr)) {
        printf("FAIL async_wait: 0x%08lX\n", (unsigned long)hr);
        goto done;
    }

    if (!g_ran) {
        printf("FAIL: work item delegate never invoked\n");
        goto done;
    }

    printf("PASS e2e_async: delegate shim invoked + async awaited\n");
    rc = 0;

done:
    if (action)
        ((IUnknown *)action)->lpVtbl->Release((IUnknown *)action);
    if (handler)
        handler->lpVtbl->Release(handler);
    cwinrt_uninit();
    return rc;
}
