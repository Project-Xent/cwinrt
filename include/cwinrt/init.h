#pragma once

#include <roapi.h>
#include "attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize WinRT on the current thread (caller chooses STA/MTA). */
CWINRT_NODISCARD HRESULT cwinrt_init(RO_INIT_TYPE type);

/* Balance cwinrt_init; safe if init was not called. */
void cwinrt_uninit(void);

#ifdef __cplusplus
}
#endif
