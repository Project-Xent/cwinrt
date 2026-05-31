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

static void finds_at(char *hay, char *needle, uvlong off, char *label) {
	CHECK(utfutf(hay, needle) == hay + off, label);
}

static void misses(char *hay, char *needle, char *label) { CHECK(utfutf(hay, needle) == null, label); }

static void basic_spec(void) {
	printf("=== utfutf basic ===\n");
	char *hw = "hello world";
	CHECK(utfutf(hw, "wor") == strstr(hw, "wor"), "find agrees with strstr");
	misses(hw, "xyz", "missing needle returns null");
	CHECK(utfutf("hello", "") != null, "empty needle matches");
	misses("", "x", "needle longer than haystack misses");
	finds_at("x", "x", 0, "single-byte exact match");
	finds_at("abc", "abc", 0, "exact match returns haystack pointer");
}

static void position_spec(void) {
	printf("\n=== utfutf positions ===\n");
	finds_at("abcdef", "abc", 0, "match at start");
	finds_at("abcdef", "def", 3, "match at end");
	finds_at("ab", "ab", 0, "full haystack match needs no suffix");
	finds_at("abcabc", "abc", 0, "first of repeated matches wins");
}

static void repeated_spec(void) {
	printf("\n=== utfutf repeated patterns ===\n");
	char *hay    = "aaaaaaaaab";
	char *needle = "aaaab";
	CHECK(utfutf(hay, needle) == strstr(hay, needle), "overlapping prefix match agrees with strstr");
	misses("aaaaaaaaaa", "aaaaab", "repeated haystack near-miss returns null");
	finds_at("abcabcabcX", "abcabc", 0, "periodic pattern matches at start");
}

static void nul_spec(void) {
	printf("\n=== utfutf NUL boundary ===\n");
	CHECK(utfutf("ab\0cd", "b\0c") != null, "needle stops at NUL and finds b");
	misses("ab\0cd", "cd", "haystack stops at NUL");
}

int main(void) {
	basic_spec();
	position_spec();
	repeated_spec();
	nul_spec();

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
