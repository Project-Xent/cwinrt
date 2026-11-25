#pragma once

#include <stdint.h>

/*
 * Parameterized Interface ID (PIID) for WinRT generic instantiations.
 *
 * A PIID is the RFC-4122 v5 (SHA-1) UUID of the type's "signature string" under
 * the WinRT namespace GUID {11f47ad5-7b73-42c0-abae-878b1e16adee}.
 *
 * The signature of a parameterized interface instantiation is
 *   pinterface({open-generic-guid};arg1;arg2;...)
 * where each arg signature is produced by the rules in piid.c.
 */

/* SHA-1 of `len` bytes into a 20-byte digest. */
void cwinrt_sha1(uint8_t const *data, size_t len, uint8_t digest [20]);

/* RFC-4122 v5 UUID of `name` under 16-byte big-endian namespace `ns`.
   Output is the 16 UUID bytes in standard (big-endian Data1) order. */
void cwinrt_uuid_v5(uint8_t const ns [16], char const *name, size_t namelen, uint8_t out [16]);

int cwinrt_piid_compute(
  uint8_t const open_guid [16], char const *const *arg_sigs, uint32_t argc, uint8_t out [16]
);

int cwinrt_sig_pinterface(
  uint8_t const open_guid [16], char const *const *arg_sigs, uint32_t argc, char *out, size_t cap
);

void cwinrt_piid_from_sig(char const *sig, size_t siglen, uint8_t out [16]);

/* Signature string for a basic ELEMENT_TYPE (e.g. "i4","string","o"), or NULL. */
char const *cwinrt_piid_basic_sig(uint8_t element_type);

void cwinrt_piid_guid_str(uint8_t const guid [16], char *out, size_t cap);
