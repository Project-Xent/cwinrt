#pragma once

#include <roapi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize WinRT on the current thread (caller chooses STA/MTA). */
HRESULT cwinrt_init(RO_INIT_TYPE type);

/* Balance cwinrt_init; safe if init was not called. */
void cwinrt_uninit(void);

#ifdef __cplusplus
}
#endif
