#include "coetua.h"
#include <stdio.h>
#include <string.h>

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

static void heading(char *name) { printf("\n=== hash: %s ===\n", name); }

static int  hexval(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static bool unhex(uchar *dst, uvlong n, char *hex) {
	if (strlen(hex) != n * 2) return false;
	for (uvlong i = 0; i < n; i++) {
		int hi = hexval(hex [i * 2]);
		int lo = hexval(hex [i * 2 + 1]);
		if (hi < 0 || lo < 0) return false;
		dst [i] = ( uchar ) ((hi << 4) | lo);
	}
	return true;
}

static void fill_blake3_input(uchar *buf, uvlong n) {
	for (uvlong i = 0; i < n; i++) buf [i] = ( uchar ) (i % 251);
}

static void blake3_vectors(void) {
	struct {
		uvlong len;
		char  *hex;
	} vec [] = {
	  {0,    "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262"},
	  {1,    "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213"},
	  {3,    "e1be4d7a8ab5560aa4199eea339849ba8e293d55ca0a81006726d184519e647f"},
	  {63,   "e9bc37a594daad83be9470df7f7b3798297c3d834ce80ba85d6e207627b7db7b"},
	  {64,   "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98"},
	  {65,   "de1e5fa0be70df6d2be8fffd0e99ceaa8eb6e8c93a63f2d8d1c30ecb6b263dee"},
	  {1023, "10108970eeda3eb932baac1428c7a2163b0e924c9a9e25b35bba72b28f70bd11"},
	  {1024, "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7"},
	  {1025, "d00278ae47eb27b34faecf67b4fe263f82d5412916c1ffd97c8cb7fb814b8444"},
	  {2048, "e776b6028c7cd22a4d0ba182a8bf62205d2ef576467e838ed6f2529b85fba24a"},
	  {2049, "5f4d72f40d7a5f82b15ca2b2e44b1de3c2ef86c426c95c1af0b6879522563030"},
	  {4096, "015094013f57a5277b59d8475c0501042c0b642e531b0a1c8f58d2163229e969"},
	  {4097, "9b4052b38f1c5fc8b1f9ff7ac7b27cd242487b3d890d15c96a1c25b8aa0fb995"},
	};

	uchar input [4097];

	heading("blake3 official-derived vectors");
	fill_blake3_input(input, sizeof(input));
	for (uvlong i = 0; i < arrlen(vec); i++) {
		uchar want [CHASHLEN];
		char  label [80];
		snprintf(label, sizeof(label), "blake3 vector length %llu", vec [i].len);
		if (!unhex(want, sizeof(want), vec [i].hex)) {
			CHECK(false, "valid blake3 vector hex");
			continue;
		}
		chash got = blake3(input, vec [i].len);
		CHECK(memcmp(got.b, want, CHASHLEN) == 0, label);
	}
}

static void blake3_convenience(void) {
	heading("blake3 convenience");
	uchar input [1025];
	fill_blake3_input(input, sizeof(input));
	arrst data = {.x = input, .len = sizeof(input)};
	chash a    = blake3a(data);
	chash b    = blake3(input, sizeof(input));
	CHECK(memcmp(a.b, b.b, CHASHLEN) == 0, "blake3a matches blake3");
}

static void other_hashes(void) {
	heading("other hashes");
	uchar seq [15];
	for (uvlong i = 0; i < sizeof(seq); i++) seq [i] = ( uchar ) i;
	CHECK(siphash("", 0) == 0x726fdb47dd0e0e31ull, "siphash empty known vector");
	CHECK(siphash(seq, sizeof(seq)) == 0xa129ca6149be45e5ull, "siphash 15-byte known vector");
	CHECK(xxhash64("", 0) == 0xef46db3751d8e999ull, "xxhash64 empty known vector");
	CHECK(xxhash64("abc", 3) == 0x44bc2cf5ad770999ull, "xxhash64 abc known vector");
}

int main(void) {
	heading("hash module");
	blake3_vectors();
	blake3_convenience();
	other_hashes();

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
