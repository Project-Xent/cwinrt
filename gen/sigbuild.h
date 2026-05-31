#pragma once

#include "winmd.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Recursive WinRT type-signature builder.
 *
 * Produces the canonical WinRT signature string for a type, matching cppwinrt's
 * `signature<T>::data` (winrt/base.h):
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

/* Build the signature of the TypeDef at row1 (non-generic kinds). Returns 0 on
   success, -1 on error/overflow. */
int cwinrt_sigbuild_typedef(winmd_db const *db, uint32_t typedef_row1, char *out, size_t cap);

/* Build the signature of a single Type read from a metadata blob region
   [*p, end). On success advances *p past the consumed type and returns 0. */
int cwinrt_sigbuild_type(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap);

/* Build the signature of a TypeSpec (generic instantiation) blob — i.e. the
   "pinterface(...)" string. Returns 0 on success. Also returns the WinRT display
   name (e.g. "...IVector`1<String>") in `name` if non-NULL. */
int cwinrt_sigbuild_typespec(
  winmd_db const *db, uint32_t typespec_row1, char *sig, size_t sig_cap, char *name, size_t name_cap
);
