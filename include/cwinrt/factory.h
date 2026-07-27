#pragma once

#include <windows.h>
#include <unknwn.h>
#include "attributes.h"

/* Activate a runtime class by wide name; result is IInspectable* in *out. */
CWINRT_NODISCARD HRESULT cwinrt_factory_activate(
    const wchar_t *class_name,
    REFIID iid,
    void **out);

/*
 * Resolve activation factory statics interface for a runtime class.
 * statics_iid may be NULL: resolve via IActivationFactory + GetIids (WinRT unique IIDs).
 * When non-NULL, tries RoGetActivationFactory(iid), QI, then GetIids fallback.
 */
CWINRT_NODISCARD HRESULT cwinrt_factory_get_statics(
    const wchar_t *class_name,
    REFIID         statics_iid,
    void         **out);

/* Release cached factories (call from cwinrt_uninit). */
void cwinrt_factory_clear(void);
