#pragma once
#include <stddef.h>
#include <limits.h>

#if defined(_MSC_VER)
  #include <intrin.h>
#endif

#ifndef __has_builtin
  #define __has_builtin(x) 0
#endif

#if defined(__clang__)
  #define COETUA_ATOM_CLANG 1
#elif defined(_MSC_VER)
  #define COETUA_ATOM_MSVC 1
#elif defined(__GNUC__)
  #define COETUA_ATOM_GNU 1
#endif

/* ═══════════════════════════════════════════════════════
   atom — portable types, bit operations, u128 helpers
   All functions are static inline; no link-time dependency.
   ═══════════════════════════════════════════════════════ */

/* ── Portable integer types ─────────────────────────── */
#if LONG_MAX != INT_MAX
  #define vlong long
#else
  #define vlong long long
#endif

#define uchar  unsigned char
#define ushort unsigned short
#define uint   unsigned int
#define ulong  unsigned long
#define uvlong unsigned vlong
#define rune   uint
#define null   (( void * ) 0)

/* ── Array slice ───────────────────────────────────── */
typedef struct arrst {
	uvlong len;
	void  *x;
} arrst;

static arrst inline mkarrst(uvlong len, void *x) { return ( arrst ) {len, x}; }

#define arrlen(arr)  (sizeof(arr) / sizeof((arr) [0]))
#define toarrst(arr) mkarrst(arrlen(arr), (arr))
#define umarrst(a)   (a).len, (a).x

/* ── Bool for C17 ──────────────────────────────────── */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311l
  #ifndef true
	#define bool  _Bool
	#define true  1
	#define false 0
  #endif
#endif

/* ── 128-bit unsigned (portable struct) ────────────── */
typedef struct {
	uvlong lo, hi;
} u128;

/* ── Compiler intrinsic seam ─────────────────────────
   Public helpers below call this private layer.  Keep compiler #ifs here so
   the rest of the codebase sees one small portable atom vocabulary. */

static int inline atom_clz32_portable(uint x) {
	if (!x) return 32;
	int n = 0;
	if (!(x & 0xffff0000u)) n += 16, x <<= 16;
	if (!(x & 0xff000000u)) n += 8, x <<= 8;
	if (!(x & 0xf0000000u)) n += 4, x <<= 4;
	if (!(x & 0xc0000000u)) n += 2, x <<= 2;
	if (!(x & 0x80000000u)) n += 1;
	return n;
}

static int inline atom_clz64_portable(uvlong x) {
	if (!x) return 64;
	int n = 0;
	if (!(x & 0xffffffff00000000ull)) n += 32, x <<= 32;
	if (!(x & 0xffff000000000000ull)) n += 16, x <<= 16;
	if (!(x & 0xff00000000000000ull)) n += 8, x <<= 8;
	if (!(x & 0xf000000000000000ull)) n += 4, x <<= 4;
	if (!(x & 0xc000000000000000ull)) n += 2, x <<= 2;
	if (!(x & 0x8000000000000000ull)) n += 1;
	return n;
}

static int inline atom_ctz32_portable(uint x) {
	if (!x) return 32;
	int n = 0;
	if (!(x & 0x0000ffffu)) n += 16, x >>= 16;
	if (!(x & 0x000000ffu)) n += 8, x >>= 8;
	if (!(x & 0x0000000fu)) n += 4, x >>= 4;
	if (!(x & 0x00000003u)) n += 2, x >>= 2;
	if (!(x & 0x00000001u)) n += 1;
	return n;
}

static int inline atom_ctz64_portable(uvlong x) {
	if (!x) return 64;
	int n = 0;
	if (!(x & 0x00000000ffffffffull)) n += 32, x >>= 32;
	if (!(x & 0x000000000000ffffull)) n += 16, x >>= 16;
	if (!(x & 0x00000000000000ffull)) n += 8, x >>= 8;
	if (!(x & 0x000000000000000full)) n += 4, x >>= 4;
	if (!(x & 0x0000000000000003ull)) n += 2, x >>= 2;
	if (!(x & 0x0000000000000001ull)) n += 1;
	return n;
}

static int inline atom_popcnt32_portable(uint x) {
	x = x - ((x >> 1) & 0x55555555u);
	x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
	return ( int ) ((((x + (x >> 4)) & 0x0f0f0f0fu) * 0x01010101u) >> 24);
}

static int inline atom_popcnt64_portable(uvlong x) {
	x = x - ((x >> 1) & 0x5555555555555555ull);
	x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
	return ( int ) ((((x + (x >> 4)) & 0x0f0f0f0f0f0f0f0full) * 0x0101010101010101ull) >> 56);
}

static uint inline atom_bswap32_portable(uint x) {
	return (x >> 24) | ((x >> 8) & 0x0000ff00u) | ((x << 8) & 0x00ff0000u) | (x << 24);
}

static uvlong inline atom_bswap64_portable(uvlong x) {
	return (x >> 56)
	     | ((x >> 40) & 0x000000000000ff00ull)
	     | ((x >> 24) & 0x0000000000ff0000ull)
	     | ((x >> 8) & 0x00000000ff000000ull)
	     | ((x << 8) & 0x000000ff00000000ull)
	     | ((x << 24) & 0x0000ff0000000000ull)
	     | ((x << 40) & 0x00ff000000000000ull)
	     | (x << 56);
}

static uint inline atom_rotl32_portable(uint x, int k) {
	uint s = ( uint ) k & 31u;
	return s ? (x << s) | (x >> (32u - s)) : x;
}

static uint inline atom_rotr32_portable(uint x, int k) {
	uint s = ( uint ) k & 31u;
	return s ? (x >> s) | (x << (32u - s)) : x;
}

static uvlong inline atom_rotl64_portable(uvlong x, int k) {
	uint s = ( uint ) k & 63u;
	return s ? (x << s) | (x >> (64u - s)) : x;
}

static uvlong inline atom_rotr64_portable(uvlong x, int k) {
	uint s = ( uint ) k & 63u;
	return s ? (x >> s) | (x << (64u - s)) : x;
}

static int inline atom_clz32(uint x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return x ? __builtin_clz(x) : 32;
#elif defined(COETUA_ATOM_MSVC)
	unsigned long i;
	return _BitScanReverse(&i, ( unsigned long ) x) ? 31 - ( int ) i : 32;
#else
	return atom_clz32_portable(x);
#endif
}

static int inline atom_clz64(uvlong x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return x ? __builtin_clzll(( unsigned long long ) x) : 64;
#elif defined(COETUA_ATOM_MSVC) && defined(_M_X64)
	unsigned long i;
	return _BitScanReverse64(&i, ( unsigned __int64 ) x) ? 63 - ( int ) i : 64;
#elif defined(COETUA_ATOM_MSVC)
	uint hi = ( uint ) (x >> 32);
	return hi ? atom_clz32(hi) : 32 + atom_clz32(( uint ) x);
#else
	return atom_clz64_portable(x);
#endif
}

static int inline atom_ctz32(uint x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return x ? __builtin_ctz(x) : 32;
#elif defined(COETUA_ATOM_MSVC)
	unsigned long i;
	return _BitScanForward(&i, ( unsigned long ) x) ? ( int ) i : 32;
#else
	return atom_ctz32_portable(x);
#endif
}

static int inline atom_ctz64(uvlong x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return x ? __builtin_ctzll(( unsigned long long ) x) : 64;
#elif defined(COETUA_ATOM_MSVC) && defined(_M_X64)
	unsigned long i;
	return _BitScanForward64(&i, ( unsigned __int64 ) x) ? ( int ) i : 64;
#elif defined(COETUA_ATOM_MSVC)
	uint lo = ( uint ) x;
	return lo ? atom_ctz32(lo) : 32 + atom_ctz32(( uint ) (x >> 32));
#else
	return atom_ctz64_portable(x);
#endif
}

static int inline atom_popcnt32(uint x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return __builtin_popcount(x);
#elif defined(COETUA_ATOM_MSVC)
	return ( int ) __popcnt(( unsigned int ) x);
#else
	return atom_popcnt32_portable(x);
#endif
}

static int inline atom_popcnt64(uvlong x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return __builtin_popcountll(( unsigned long long ) x);
#elif defined(COETUA_ATOM_MSVC) && defined(_M_X64)
	return ( int ) __popcnt64(( unsigned __int64 ) x);
#elif defined(COETUA_ATOM_MSVC)
	return atom_popcnt32(( uint ) x) + atom_popcnt32(( uint ) (x >> 32));
#else
	return atom_popcnt64_portable(x);
#endif
}

static uint inline atom_bswap32(uint x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return __builtin_bswap32(x);
#elif defined(COETUA_ATOM_MSVC)
	return ( uint ) _byteswap_ulong(( unsigned long ) x);
#else
	return atom_bswap32_portable(x);
#endif
}

static uvlong inline atom_bswap64(uvlong x) {
#if defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)
	return ( uvlong ) __builtin_bswap64(( unsigned long long ) x);
#elif defined(COETUA_ATOM_MSVC)
	return ( uvlong ) _byteswap_uint64(( unsigned __int64 ) x);
#else
	return atom_bswap64_portable(x);
#endif
}

static uint inline atom_bitrev32(uint x) {
	x = ((x & 0x55555555u) << 1) | ((x >> 1) & 0x55555555u);
	x = ((x & 0x33333333u) << 2) | ((x >> 2) & 0x33333333u);
	x = ((x & 0x0f0f0f0fu) << 4) | ((x >> 4) & 0x0f0f0f0fu);
	return atom_bswap32(x);
}

static uvlong inline atom_bitrev64(uvlong x) {
	x = ((x & 0x5555555555555555ull) << 1) | ((x >> 1) & 0x5555555555555555ull);
	x = ((x & 0x3333333333333333ull) << 2) | ((x >> 2) & 0x3333333333333333ull);
	x = ((x & 0x0f0f0f0f0f0f0f0full) << 4) | ((x >> 4) & 0x0f0f0f0f0f0f0f0full);
	return atom_bswap64(x);
}

static uint inline atom_rotl32(uint x, int k) {
	uint s = ( uint ) k & 31u;
#if defined(COETUA_ATOM_CLANG) && __has_builtin(__builtin_rotateleft32)
	return __builtin_rotateleft32(x, s);
#elif defined(COETUA_ATOM_MSVC)
	return _rotl(x, ( int ) s);
#else
	return atom_rotl32_portable(x, ( int ) s);
#endif
}

static uint inline atom_rotr32(uint x, int k) {
	uint s = ( uint ) k & 31u;
#if defined(COETUA_ATOM_CLANG) && __has_builtin(__builtin_rotateright32)
	return __builtin_rotateright32(x, s);
#elif defined(COETUA_ATOM_MSVC)
	return _rotr(x, ( int ) s);
#else
	return atom_rotr32_portable(x, ( int ) s);
#endif
}

static uvlong inline atom_rotl64(uvlong x, int k) {
	uint s = ( uint ) k & 63u;
#if defined(COETUA_ATOM_CLANG) && __has_builtin(__builtin_rotateleft64)
	return ( uvlong ) __builtin_rotateleft64(( unsigned long long ) x, s);
#elif defined(COETUA_ATOM_MSVC)
	return ( uvlong ) _rotl64(( unsigned __int64 ) x, ( int ) s);
#else
	return atom_rotl64_portable(x, ( int ) s);
#endif
}

static uvlong inline atom_rotr64(uvlong x, int k) {
	uint s = ( uint ) k & 63u;
#if defined(COETUA_ATOM_CLANG) && __has_builtin(__builtin_rotateright64)
	return ( uvlong ) __builtin_rotateright64(( unsigned long long ) x, s);
#elif defined(COETUA_ATOM_MSVC)
	return ( uvlong ) _rotr64(( unsigned __int64 ) x, ( int ) s);
#else
	return atom_rotr64_portable(x, ( int ) s);
#endif
}

/* ── Count leading zeros ───────────────────────────── */
static int inline clz32(uint x) { return atom_clz32(x); }

static int inline clz64(uvlong x) { return atom_clz64(x); }

/* ── Count trailing zeros ──────────────────────────── */
static int inline ctz32(uint x) { return atom_ctz32(x); }

static int inline ctz64(uvlong x) { return atom_ctz64(x); }

/* ── Population count ──────────────────────────────── */
static int inline popcnt32(uint x) { return atom_popcnt32(x); }

static int inline popcnt64(uvlong x) { return atom_popcnt64(x); }

/* ── Byte swap ─────────────────────────────────────── */
static uint inline bswap32(uint x) { return atom_bswap32(x); }

static uvlong inline bswap64(uvlong x) { return atom_bswap64(x); }

/* ── Bit reverse ───────────────────────────────────── */
static uint inline bitrev32(uint x) { return atom_bitrev32(x); }

static uvlong inline bitrev64(uvlong x) { return atom_bitrev64(x); }

/* ── Rotate ────────────────────────────────────────── */
static uint inline rotl32(uint x, int k) { return atom_rotl32(x, k); }

static uint inline rotr32(uint x, int k) { return atom_rotr32(x, k); }

static uvlong inline rotl64(uvlong x, int k) { return atom_rotl64(x, k); }

static uvlong inline rotr64(uvlong x, int k) { return atom_rotr64(x, k); }

/* ── Power-of-2 / alignment ────────────────────────── */
static bool inline ispow2(uint x) { return x && !(x & (x - 1)); }

static uint inline nextpow2(uint x) { return x <= 1 ? 1 : x > 0x80000000u ? UINT_MAX : 1u << (32 - clz32(x - 1)); }

static uint inline alignup(uint x, uint a) { return (x + a - 1) & ~(a - 1); }

static uint inline aligndown(uint x, uint a) { return x & ~(a - 1); }

static bool inline ispow2_64(uvlong x) { return x && !(x & (x - 1)); }

static uvlong inline nextpow2_64(uvlong x) {
	return x <= 1 ? 1 : x > (( uvlong ) 1 << 63) ? ( uvlong ) -1 : ( uvlong ) 1 << (64 - clz64(x - 1));
}

static uvlong inline alignup_64(uvlong x, uvlong a) { return (x + a - 1) & ~(a - 1); }

static uvlong inline aligndown_64(uvlong x, uvlong a) { return x & ~(a - 1); }

/* ── Checked uvlong arithmetic ─────────────────────── */
static bool inline addok64(uvlong a, uvlong b, uvlong *out) {
	if (a > ( uvlong ) -1 - b) return false;
	*out = a + b;
	return true;
}

static bool inline mulok64(uvlong a, uvlong b, uvlong *out) {
	if (a != 0 && b > ( uvlong ) -1 / a) return false;
	*out = a * b;
	return true;
}

/* ── 128-bit helpers ───────────────────────────────── */
static u128 inline u128_make(uvlong lo, uvlong hi) { return ( u128 ) {lo, hi}; }

/* 64×64 → 128 widening multiply */
static u128 inline wmul64(uvlong a, uvlong b) {
#if (defined(COETUA_ATOM_CLANG) || defined(COETUA_ATOM_GNU)) && defined(__SIZEOF_INT128__)
	__uint128_t p = ( __uint128_t ) a * ( __uint128_t ) b;
	return ( u128 ) {( uvlong ) p, ( uvlong ) (p >> 64)};
#elif defined(COETUA_ATOM_MSVC) && defined(_M_X64)
	unsigned __int64 hi;
	unsigned __int64 lo = _umul128(( unsigned __int64 ) a, ( unsigned __int64 ) b, &hi);
	return ( u128 ) {( uvlong ) lo, ( uvlong ) hi};
#else
	uvlong a0  = ( uint ) a;
	uvlong a1  = a >> 32;
	uvlong b0  = ( uint ) b;
	uvlong b1  = b >> 32;
	uvlong p0  = a0 * b0;
	uvlong p1  = a0 * b1;
	uvlong p2  = a1 * b0;
	uvlong p3  = a1 * b1;
	uvlong mid = (p0 >> 32) + ( uint ) p1 + ( uint ) p2;
	return ( u128 ) {(p0 & 0xffffffffull) | (mid << 32), p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32)};
#endif
}

static u128 inline u128_add(u128 a, u128 b) {
	uvlong lo = a.lo + b.lo;
	return ( u128 ) {lo, a.hi + b.hi + (lo < a.lo)};
}
