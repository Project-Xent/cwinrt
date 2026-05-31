#pragma once
#include "atom.h"

/* ═══════════════════════════════════════════════════════
   hash — SipHash-2-4 (cryptographic) & XXHash64 (fast)
   ═══════════════════════════════════════════════════════ */

/* SipHash-2-4: keyed, collision-resistant.  Uses internal fixed key. */
uvlong siphash(void *data, uvlong len);

/* XXHash64: non-cryptographic, very fast.  Good for hash tables. */
uvlong xxhash64(void *data, uvlong len);

/* Fixed 32-byte BLAKE3-256 content digest. */
enum
{
	CHASHLEN = 32,
};

typedef struct chash {
	uchar b [CHASHLEN];
} chash;

chash blake3(void *data, uvlong len);

static uvlong inline siphasha(arrst data) { return siphash(data.x, data.len); }

static uvlong inline xxhash64a(arrst data) { return xxhash64(data.x, data.len); }

static chash inline blake3a(arrst data) { return blake3(data.x, data.len); }
