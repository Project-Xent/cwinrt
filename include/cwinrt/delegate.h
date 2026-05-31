#pragma once

#include <unknwn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inbound delegate shim: wrap a C callback as a WinRT delegate COM
 * object so it can be passed to add_<Event> / put_Completed / etc.
 *
 * Covers the two-argument delegate shape (sender, args) used by EventHandler<T>,
 * TypedEventHandler<S,A> and the *CompletedHandler delegates. The created object:
 *   - QueryInterface succeeds for IUnknown, IAgileObject, and `iid` (the
 *     delegate's own IID, from the CWINRT_IID_<Delegate> header constant);
 *   - Invoke is at vtable slot 3 (WinRT delegates are IUnknown-based);
 *   - refcounts itself and frees on the final Release.
 *
 * Pass the delegate's IID so the source's QueryInterface(handler, delegate_iid)
 * succeeds. Caller Release()s *out after a successful add/subscribe.
 */
typedef void (*cwinrt_delegate_fn)(void *sender, void *args, void *ctx);

HRESULT cwinrt_delegate_create(REFIID iid, cwinrt_delegate_fn fn, void *ctx, IUnknown **out);

#ifdef __cplusplus
}
#endif
