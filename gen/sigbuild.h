#pragma once

#include "winmd.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Recursive WinRT type-signature builder.
 *
 * Produces the canonical WinRT parameterized-type signature:
 *   basic       -> "b1","i4","string","cinterface(IInspectable)", ...
 *   enum        -> "enum(Namespace.Name;<underlying>)"
 *   struct      -> "struct(Namespace.Name;f1;f2;...)"
 *   interface   -> "{guid}"
 *   delegate    -> "delegate({guid})"
 *   runtimeclass-> "rc(Namespace.Name;<default-interface-sig>)"
 *   generic inst-> "pinterface({open-guid};arg1;arg2;...)"
 *
 * The PIID of a generic instantiation is cwinrt_piid_from_sig() of its
 * "pinterface(...)" string (see piid.h).
 */

int cwinrt_sigbuild_typedef(winmd_db const *db, uint32_t typedef_row1, char *out, size_t cap);

int cwinrt_sigbuild_type(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap);

int cwinrt_sigbuild_typespec(
  winmd_db const *db, uint32_t typespec_row1, char *sig, size_t sig_cap, char *name, size_t name_cap
);
