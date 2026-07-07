#pragma once

#include <windows.h>
#include <unknwn.h>
#include "attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create DispatcherQueue on the current thread.
 * Returns DispatcherQueueController; caller must Release *out when done.
 */
CWINRT_NODISCARD HRESULT cwinrt_bootstrap_dispatcher_queue(IUnknown **out);

#ifdef __cplusplus
}
#endif
