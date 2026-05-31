#pragma once

#include <windows.h>
#include <unknwn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Block until an async completes; timeout_ms may be INFINITE.
 *
 * put_Completed sits at a different vtable slot depending on the interface
 * shape, so there are two entry points — pick the one matching the typed
 * pointer you hold (the C type name tells you):
 *   cwinrt_async_wait               — IAsyncAction, IAsyncOperation<T>
 *   cwinrt_async_wait_with_progress — IAsyncActionWithProgress<P>,
 *                                     IAsyncOperationWithProgress<T,P>
 * (the WithProgress interfaces insert a put/get_Progress pair before Completed).
 */
HRESULT cwinrt_async_wait(IUnknown *async_info, DWORD timeout_ms);
HRESULT cwinrt_async_wait_with_progress(IUnknown *async_info, DWORD timeout_ms);

/*
 * Blocking await for an async that produces a result: wait, then GetResults.
 * If result_iid is non-NULL the result is cast to it (cwinrt_query) and the
 * original reference released; otherwise the raw reference is returned. Caller
 * Release()s *result. Use the _with_progress variant for
 * IAsyncOperationWithProgress<T,P> (GetResults is at a later slot).
 */
HRESULT cwinrt_async_get(void *async_op, REFIID result_iid, void **result);
HRESULT cwinrt_async_get_with_progress(void *async_op, REFIID result_iid, void **result);

#ifdef __cplusplus
}
#endif
