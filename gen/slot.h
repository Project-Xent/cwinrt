#pragma once

#include "model.h"

enum
{
	CWINRT_VTBL_SLOT_FIRST = 6,
};

/* Fill vtable_slot + dispatch_token on raw methods (0 = unknown). */
int cwinrt_slot_assign(cwinrt_raw_db *raw);

/* Independently validate assigned slots against the WinRT ABI rule:
   each interface's own instance methods occupy slots 6,7,8,... in metadata
   (method-token) order with dispatch_token == the interface; runtimeclass
   instance methods dispatch through an interface at a slot >= 6. Returns the
   number of violations and accumulates per-method ok/bad counts. */
int cwinrt_slot_check(cwinrt_raw_db const *raw, uint32_t *inout_ok, uint32_t *inout_bad);
