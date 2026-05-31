#pragma once

#include <stdint.h>
#include <windows.h>
#include <unknwn.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int64_t value; } cwinrt_token;

/* Subscription handle: stores WinRT token + everything needed to unsubscribe. */
typedef struct cwinrt_event_handle {
    IUnknown    *source;
    uint32_t     remove_slot;
    cwinrt_token token;
} cwinrt_event_handle;

typedef void (*cwinrt_event_fn)(void *sender, void *args, void *ctx);

/* add_slot / remove_slot are vtable indices on source for WinRT event methods. */
HRESULT cwinrt_event_subscribe(
    IUnknown           *source,
    uint32_t            add_slot,
    uint32_t            remove_slot,
    IUnknown           *handler,
    cwinrt_event_handle *out);

/* Uses fields stored in handle; source/remove_slot need not be passed again. */
HRESULT cwinrt_event_unsubscribe(cwinrt_event_handle *handle);

/*
 * Build a minimal EventHandler-compatible object that forwards Invoke to fn.
 * Caller Release()s *out after a successful subscribe.
 */
HRESULT cwinrt_event_handler_create(cwinrt_event_fn fn, void *ctx, IUnknown **out);

#ifdef __cplusplus
}
#endif
