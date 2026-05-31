#include "hash.h"
#include <stdint.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════
   SipHash-2-4
   ═══════════════════════════════════════════════════════ */

#define SIPROUND(v0, v1, v2, v3) \
	do {                         \
		v0 += v1;                \
		v1  = rotl64(v1, 13);    \
		v1 ^= v0;                \
		v0  = rotl64(v0, 32);    \
		v2 += v3;                \
		v3  = rotl64(v3, 16);    \
		v3 ^= v2;                \
		v0 += v3;                \
		v3  = rotl64(v3, 21);    \
		v3 ^= v0;                \
		v2 += v1;                \
		v1  = rotl64(v1, 17);    \
		v1 ^= v2;                \
		v2  = rotl64(v2, 32);    \
	}                            \
	while (0)

uvlong siphash(void *data, uvlong len) {
	uvlong k0 = 0x0706050403020100ull, k1 = 0x0f0e0d0c0b0a0908ull;
	uvlong v0   = k0 ^ 0x736f6d6570736575ull;
	uvlong v1   = k1 ^ 0x646f72616e646f6dull;
	uvlong v2   = k0 ^ 0x6c7967656e657261ull;
	uvlong v3   = k1 ^ 0x7465646279746573ull;

	uchar *in   = ( uchar * ) data;
	uvlong left = len & 7;
	uchar *end  = in + len - left;

	for (; in != end; in += 8) {
		uvlong m;
		memcpy(&m, in, 8);
		v3 ^= m;
		SIPROUND(v0, v1, v2, v3);
		SIPROUND(v0, v1, v2, v3);
		v0 ^= m;
	}

	uvlong b = (( uvlong ) len) << 56;
	switch (left) {
	case 7 : b |= (( uvlong ) in [6]) << 48;
	case 6 : b |= (( uvlong ) in [5]) << 40;
	case 5 : b |= (( uvlong ) in [4]) << 32;
	case 4 : b |= (( uvlong ) in [3]) << 24;
	case 3 : b |= (( uvlong ) in [2]) << 16;
	case 2 : b |= (( uvlong ) in [1]) << 8;
	case 1 : b |= (( uvlong ) in [0]); break;
	case 0 : break;
	}

	v3 ^= b;
	SIPROUND(v0, v1, v2, v3);
	SIPROUND(v0, v1, v2, v3);
	v0 ^= b;
	v2 ^= 0xff;
	SIPROUND(v0, v1, v2, v3);
	SIPROUND(v0, v1, v2, v3);
	SIPROUND(v0, v1, v2, v3);
	SIPROUND(v0, v1, v2, v3);

	return v0 ^ v1 ^ v2 ^ v3;
}

/* ═══════════════════════════════════════════════════════
   XXHash64
   ═══════════════════════════════════════════════════════ */

static uvlong PRIME64_1 = 0x9e3779b185ebca87ull;
static uvlong PRIME64_2 = 0xc2b2ae3d27d4eb4full;
static uvlong PRIME64_3 = 0x165667b19e3779f9ull;
static uvlong PRIME64_4 = 0x85ebca77c2b2ae63ull;
static uvlong PRIME64_5 = 0x27d4eb2f165667c5ull;

static uvlong inline xx64_round(uvlong acc, uvlong input) {
	acc += input * PRIME64_2;
	acc  = rotl64(acc, 31);
	acc *= PRIME64_1;
	return acc;
}

static uvlong inline xx64_merge(uvlong acc, uvlong val) {
	acc ^= xx64_round(0, val);
	acc  = acc * PRIME64_1 + PRIME64_4;
	return acc;
}

uvlong xxhash64(void *data, uvlong len) {
	uchar *p    = ( uchar * ) data;
	uchar *bEnd = p + len;
	uvlong h64;

	if (len >= 32) {
		uchar *limit = bEnd - 32;
		uvlong v1    = PRIME64_1 + PRIME64_2;
		uvlong v2    = PRIME64_2;
		uvlong v3    = 0;
		uvlong v4    = -( vlong ) PRIME64_1;

		do {
			uvlong m1, m2, m3, m4;
			memcpy(&m1, p, 8);
			memcpy(&m2, p + 8, 8);
			memcpy(&m3, p + 16, 8);
			memcpy(&m4, p + 24, 8);
			v1  = xx64_round(v1, m1);
			v2  = xx64_round(v2, m2);
			v3  = xx64_round(v3, m3);
			v4  = xx64_round(v4, m4);
			p  += 32;
		}
		while (p <= limit);

		h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
		h64 = xx64_merge(h64, v1);
		h64 = xx64_merge(h64, v2);
		h64 = xx64_merge(h64, v3);
		h64 = xx64_merge(h64, v4);
	}
	else { h64 = PRIME64_5; }

	h64 += ( uvlong ) len;

	while (p + 8 <= bEnd) {
		uvlong k1;
		memcpy(&k1, p, 8);
		h64 ^= xx64_round(0, k1);
		h64  = rotl64(h64, 27) * PRIME64_1 + PRIME64_4;
		p   += 8;
	}

	if (p + 4 <= bEnd) {
		uint k1;
		memcpy(&k1, p, 4);
		h64 ^= ( uvlong ) k1 * PRIME64_1;
		h64  = rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
		p   += 4;
	}

	while (p < bEnd) {
		h64 ^= (*p) * PRIME64_5;
		h64  = rotl64(h64, 11) * PRIME64_1;
		p++;
	}

	h64 ^= h64 >> 33;
	h64 *= PRIME64_2;
	h64 ^= h64 >> 29;
	h64 *= PRIME64_3;
	h64 ^= h64 >> 32;

	return h64;
}

/* ═══════════════════════════════════════════════════════
   BLAKE3-256
   ═══════════════════════════════════════════════════════ */

enum
{
	B3_CHUNK       = 1024,
	B3_BLOCK       = 64,
	B3_WORDS       = 16,
	B3_ROUNDS      = 7,

	B3_CHUNK_START = 1u << 0,
	B3_CHUNK_END   = 1u << 1,
	B3_PARENT      = 1u << 2,
	B3_ROOT        = 1u << 3,
};

static uint32_t b3_iv [8] = {
  0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

static uchar b3_sched [B3_ROUNDS][B3_WORDS] = {
  {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15},
  {2,  6,  3,  10, 7,  0,  4,  13, 1,  11, 12, 5,  9,  14, 15, 8 },
  {3,  4,  10, 12, 13, 2,  7,  14, 6,  5,  9,  0,  11, 15, 8,  1 },
  {10, 7,  12, 9,  14, 3,  13, 15, 4,  0,  11, 2,  5,  8,  1,  6 },
  {12, 13, 9,  11, 15, 10, 14, 8,  7,  2,  5,  3,  0,  1,  6,  4 },
  {9,  14, 11, 5,  8,  12, 15, 1,  13, 3,  0,  10, 2,  6,  4,  7 },
  {11, 15, 5,  0,  1,  9,  8,  6,  14, 10, 2,  12, 3,  4,  7,  13},
};

typedef struct {
	uint32_t cv [8];
	uint32_t block [B3_WORDS];
	uvlong   counter;
	uint     block_len;
	uint     flags;
} b3_out;

static uint32_t b3_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void     b3_g(uint32_t v [16], int a, int b, int c, int d, uint32_t x, uint32_t y) {
	v [a] += v [b] + x;
	v [d]  = b3_rotr(v [d] ^ v [a], 16);
	v [c] += v [d];
	v [b]  = b3_rotr(v [b] ^ v [c], 12);
	v [a] += v [b] + y;
	v [d]  = b3_rotr(v [d] ^ v [a], 8);
	v [c] += v [d];
	v [b]  = b3_rotr(v [b] ^ v [c], 7);
}

static uint32_t b3_load32(uchar *p) {
	return ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8) | (( uint32_t ) p [2] << 16) | (( uint32_t ) p [3] << 24);
}

static void b3_store32(uchar *p, uint32_t x) {
	p [0] = ( uchar ) x;
	p [1] = ( uchar ) (x >> 8);
	p [2] = ( uchar ) (x >> 16);
	p [3] = ( uchar ) (x >> 24);
}

static void b3_words(uint32_t w [16], uchar *p, uint n) {
	memset(w, 0, 16 * sizeof(w [0]));
	for (uint i = 0; i + 4 <= n; i += 4) w [i / 4] = b3_load32(p + i);
	for (uint i = n & ~3u; i < n; i++) w [i / 4] |= ( uint32_t ) p [i] << (8 * (i & 3));
}

static void b3_compress16(uint32_t out [16], uint32_t cv [8], uint32_t block [16], uvlong counter, uint block_len,
                          uint flags) {
	uint32_t v [16];
	memcpy(v, cv, 8 * sizeof(v [0]));
	memcpy(v + 8, b3_iv, 4 * sizeof(v [0]));
	v [12] = ( uint32_t ) counter;
	v [13] = ( uint32_t ) (counter >> 32);
	v [14] = block_len;
	v [15] = flags;

	for (int r = 0; r < B3_ROUNDS; r++) {
		uchar *s = b3_sched [r];
		b3_g(v, 0, 4, 8, 12, block [s [0]], block [s [1]]);
		b3_g(v, 1, 5, 9, 13, block [s [2]], block [s [3]]);
		b3_g(v, 2, 6, 10, 14, block [s [4]], block [s [5]]);
		b3_g(v, 3, 7, 11, 15, block [s [6]], block [s [7]]);
		b3_g(v, 0, 5, 10, 15, block [s [8]], block [s [9]]);
		b3_g(v, 1, 6, 11, 12, block [s [10]], block [s [11]]);
		b3_g(v, 2, 7, 8, 13, block [s [12]], block [s [13]]);
		b3_g(v, 3, 4, 9, 14, block [s [14]], block [s [15]]);
	}

	for (int i = 0; i < 8; i++) out [i] = v [i] ^ v [i + 8];
	for (int i = 0; i < 8; i++) out [i + 8] = v [i + 8] ^ cv [i];
}

static void b3_cv(uint32_t cv [8], b3_out *o) {
	uint32_t wide [16];
	b3_compress16(wide, o->cv, o->block, o->counter, o->block_len, o->flags);
	memcpy(cv, wide, 8 * sizeof(cv [0]));
}

static b3_out b3_chunk_out(uchar *p, uvlong len, uvlong chunk) {
	b3_out   o;
	uint32_t cv [8];
	memcpy(cv, b3_iv, sizeof(cv));

	for (uvlong off = 0;; off += B3_BLOCK) {
		uint n      = len - off > B3_BLOCK ? B3_BLOCK : ( uint ) (len - off);
		o.counter   = chunk;
		o.block_len = n;
		o.flags     = (off == 0 ? B3_CHUNK_START : 0) | (off + n == len ? B3_CHUNK_END : 0);
		memcpy(o.cv, cv, sizeof(o.cv));
		b3_words(o.block, p + off, n);
		if (o.flags & B3_CHUNK_END) return o;
		b3_cv(cv, &o);
	}
}

static b3_out b3_parent_out(uint32_t left [8], uint32_t right [8]) {
	b3_out o;
	memcpy(o.cv, b3_iv, sizeof(o.cv));
	memcpy(o.block, left, 8 * sizeof(o.block [0]));
	memcpy(o.block + 8, right, 8 * sizeof(o.block [0]));
	o.counter   = 0;
	o.block_len = B3_BLOCK;
	o.flags     = B3_PARENT;
	return o;
}

static uvlong b3_left_len(uvlong len) {
	uvlong full = (len - 1) / B3_CHUNK;
	uvlong n    = 1;
	while (n <= full / 2) n <<= 1;
	return n * B3_CHUNK;
}

static b3_out b3_tree_out(uchar *p, uvlong len, uvlong chunk) {
	if (len <= B3_CHUNK) return b3_chunk_out(p, len, chunk);

	uvlong   left_len = b3_left_len(len);
	b3_out   left     = b3_tree_out(p, left_len, chunk);
	b3_out   right    = b3_tree_out(p + left_len, len - left_len, chunk + left_len / B3_CHUNK);
	uint32_t lcv [8], rcv [8];
	b3_cv(lcv, &left);
	b3_cv(rcv, &right);
	return b3_parent_out(lcv, rcv);
}

chash blake3(void *data, uvlong len) {
	chash    h;
	b3_out   o = b3_tree_out(( uchar * ) data, len, 0);
	uint32_t wide [16];
	b3_compress16(wide, o.cv, o.block, o.counter, o.block_len, o.flags | B3_ROOT);
	for (int i = 0; i < 8; i++) b3_store32(h.b + 4 * i, wide [i]);
	return h;
}
