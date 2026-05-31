#include "atom.h"
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg)                    \
	do {                                    \
		if (!(cond)) {                      \
			printf("FAIL: %s\n", msg);      \
			failures++;                     \
		}                                   \
		else { printf("  ok: %s\n", msg); } \
	}                                       \
	while (0)

int main(void) {
	printf("=== atom: bit helpers ===\n");
	CHECK(clz32(0) == 32 && clz32(1) == 31, "clz32 handles zero and low bit");
	CHECK(clz32(0x80000000u) == 0 && clz64(( uvlong ) 1 << 63) == 0, "clz handles high bits");
	CHECK(ctz32(0) == 32 && ctz32(8) == 3, "ctz32 handles zero and set bit");
	CHECK(ctz64(( uvlong ) 1 << 40) == 40, "ctz64 handles high set bit");
	CHECK(popcnt32(0xff) == 8 && popcnt64(0xffffffffffffffffull) == 64, "popcnt counts set bits");
	CHECK(bswap32(0x12345678) == 0x78563412, "bswap32 reverses byte order");
	CHECK(bswap64(0x0123456789abcdefull) == 0xefcdab8967452301ull, "bswap64 reverses byte order");
	CHECK(bitrev32(0x00000005u) == 0xa0000000u, "bitrev32 reverses bit order");
	CHECK(bitrev64(0x0000000000000005ull) == 0xa000000000000000ull, "bitrev64 reverses bit order");
	CHECK(rotl32(1, 1) == 2 && rotr32(2, 1) == 1, "32-bit rotates");
	CHECK(rotl32(0x12345678u, 0) == 0x12345678u && rotr32(0x12345678u, 32) == 0x12345678u,
	      "32-bit rotates mask zero/full shifts");
	CHECK(rotl64(1, 40) == (( uvlong ) 1 << 40) && rotr64(( uvlong ) 1 << 40, 40) == 1,
	      "64-bit rotates handle large shifts");
	CHECK(rotl64(0x0123456789abcdefull, 64) == 0x0123456789abcdefull, "64-bit rotates mask full-width shifts");

	printf("\n=== atom: alignment and powers ===\n");
	CHECK(ispow2(8) && !ispow2(7), "ispow2 distinguishes powers of two");
	CHECK(nextpow2(0) == 1 && nextpow2(5) == 8 && nextpow2(1) == 1, "nextpow2 rounds up");
	CHECK(nextpow2(0x80000001u) == UINT_MAX, "nextpow2 saturates 32-bit overflow");
	CHECK(alignup(5, 8) == 8 && aligndown(15, 8) == 8, "alignment helpers");
	CHECK(nextpow2_64((( uvlong ) 1 << 40) - 1) == (( uvlong ) 1 << 40), "nextpow2_64 rounds large values");
	CHECK(nextpow2_64((( uvlong ) 1 << 63) + 1) == ( uvlong ) -1, "nextpow2_64 saturates overflow");

	printf("\n=== atom: checked arithmetic and u128 ===\n");
	uvlong out = 0;
	CHECK(addok64(10, 20, &out) && out == 30, "addok64 accepts ordinary sum");
	CHECK(!addok64(( uvlong ) -1, 1, &out), "addok64 rejects overflow");
	CHECK(mulok64(12, 13, &out) && out == 156, "mulok64 accepts ordinary product");
	CHECK(!mulok64((( uvlong ) -1 / 2) + 1, 2, &out), "mulok64 rejects overflow");
	u128 p = wmul64(0x100000000ull, 0x100000000ull);
	CHECK(p.lo == 0 && p.hi == 1, "wmul64 widens 64x64 product");
	p = wmul64(( uvlong ) -1, ( uvlong ) -1);
	CHECK(p.lo == 1 && p.hi == ( uvlong ) -2, "wmul64 handles max product");
	p = wmul64(0x123456789abcdef0ull, 0x0fedcba987654321ull);
	CHECK(p.lo == 0x2236d88fe5618cf0ull && p.hi == 0x0121fa00ad77d742ull, "wmul64 matches known mixed product");
	u128 s = u128_add(u128_make(( uvlong ) -1, 0), u128_make(1, 0));
	CHECK(s.lo == 0 && s.hi == 1, "u128_add carries");

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
