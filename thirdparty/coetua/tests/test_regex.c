#include "coetua.h"
#include "regex9.h"
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

static void check_expected_error(bool ok, char *label) {
	CHECK(ok && err(), label);
	errmsg(null);
}

static void expect_match(char *pat, char *text, int want, char *label) {
	Reprog *re = regcomp9(0, pat);
	if (!re) {
		CHECK(false, label);
		return;
	}
	Resub m [16];
	memset(m, 0, sizeof(m));
	int got = regexec9(re, text, strlen(text), m, 16);
	if (got != want) {
		printf("FAIL: %s (got %d want %d)\n", label, got, want);
		failures++;
		return;
	}
	printf("  ok: %s\n", label);
	if (got > 0) {
		uvlong mlen = ( uvlong ) (m [0].e.ep - m [0].s.sp);
		printf("    match: '%.*s'\n", ( int ) mlen, m [0].s.sp);
	}
}

static void expect_whole(char *pat, char *text, char *want, char *label) {
	Reprog *re = regcomp9(0, pat);
	if (!re) {
		CHECK(false, label);
		return;
	}
	Resub m [16];
	memset(m, 0, sizeof(m));
	int got = regexec9(re, text, strlen(text), m, 16);
	if (got <= 0) {
		printf("FAIL: %s (no match)\n", label);
		failures++;
		return;
	}
	slitr gotv = mkslitr(m [0].s.sp, ( uvlong ) (m [0].e.ep - m [0].s.sp));
	if (!slitreq(gotv, slitr_of(want))) {
		printf("FAIL: %s (got '%.*s' want '%s')\n", label, ( int ) gotv.len, gotv.s, want);
		failures++;
		return;
	}
	printf("  ok: %s\n", label);
}

static void expect_cap(char *pat, char *text, int idx, char *want, char *label) {
	Reprog *re = regcomp9(0, pat);
	if (!re) {
		CHECK(false, label);
		return;
	}
	Resub m [16];
	memset(m, 0, sizeof(m));
	int got = regexec9(re, text, strlen(text), m, 16);
	if (got <= 0) {
		CHECK(false, label);
		return;
	}
	if (m [idx].kind != RSUB_TEXT || !m [idx].s.sp || !m [idx].e.ep) {
		printf("FAIL: %s (idx %d not text)\n", label, idx);
		failures++;
		return;
	}
	uvlong mlen = ( uvlong ) (m [idx].e.ep - m [idx].s.sp);
	slitr  gotv = mkslitr(m [idx].s.sp, mlen);
	slitr  w    = slitr_of(want);
	if (!slitreq(gotv, w)) {
		printf("FAIL: %s (\\%d got '%.*s' want '%s')\n", label, idx, ( int ) gotv.len, gotv.s, want);
		failures++;
		return;
	}
	printf("  ok: %s\n", label);
}

static void expect_qty(char *pat, char *text, int idx, uvlong want, char *label) {
	Reprog *re = regcomp9(0, pat);
	if (!re) {
		CHECK(false, label);
		return;
	}
	Resub m [16];
	memset(m, 0, sizeof(m));
	int got = regexec9(re, text, strlen(text), m, 16);
	if (got <= 0) {
		if (want == 0) {
			printf("  ok: %s (no match)\n", label);
			return;
		}
		printf("FAIL: %s (no match, wanted qty %llu)\n", label, want);
		failures++;
		return;
	}
	if (m [idx].kind != RSUB_QUANTITY || m [idx].s.q != want) {
		printf("FAIL: %s (\\%d qty got %llu want %llu)\n", label, idx, m [idx].s.q, want);
		failures++;
		return;
	}
	printf("  ok: %s\n", label);
}

static void error_edges(void) {
	printf("\n=== regex errors ===\n");
	check_expected_error(regcomp9(0, null) == null, "regcomp9 null pattern sets error");
	Reprog *re = regcomp9(0, "abc");
	CHECK(re != null, "regcomp9 for error edges");
	Resub m [1];
	CHECK(regexec9(re, "zzz", 3, m, 1) == 0 && !err(), "regexec9 no match is quiet");
	check_expected_error(regexec9(null, "abc", 3, m, 1) < 0, "regexec9 null program sets error");
	check_expected_error(regexec9(re, null, 0, m, 1) < 0, "regexec9 null text sets error");
	check_expected_error(regexec9(re, "abc", 3, null, 1) < 0, "regexec9 null match vector sets error");
}

int main(void) {
	printf("=== regex9 skeleton ===\n");
	expect_match("abc", "abc", 1, "literal match");
	expect_match("a|b", "b", 1, "alternation");
	expect_match("(ab)c", "abc", 1, "captures");
	expect_match("%d+", "123x", 1, "class plus");
	expect_match("^abc$", "abc", 1, "anchors");
	expect_whole("a|%d+", "a123", "a", "leftmost beats later longer match");
	expect_whole("%d+", "a1b22c333", "1", "leftmost digit run wins before later longer run");
	expect_whole("%d+|%a+", "abc12345", "abc", "leftmost word wins before later digits");

	printf("\n=== @ quantifiers ===\n");
	expect_match("a@3'", "aaa", 1, "@3' exact on aaa");
	expect_match("a@3'", "aa", 0, "@3' fails on aa");
	expect_match("a@3'", "aaaa", 1, "@3' matches prefix of aaaa");
	expect_whole("a@3'b", "aaaab", "aaab", "@ exact can be followed by literal");
	expect_whole("^a@3'$", "aaa", "aaa", "@ exact works with anchors");
	expect_match("a@2+", "aa", 1, "@2+ on aa");
	expect_match("a@2+", "aaaa", 1, "@2+ on aaaa");
	expect_whole("a@(2)+b", "aaaab", "aaaab", "captured at-least can be followed by literal");
	expect_match("a@2+", "a", 0, "@2+ fails on a");
	expect_match("a@3-", "aa", 1, "@3- on aa");
	expect_match("a@3-", "a", 1, "@3- on a");
	expect_match("a@3-", "aaaa", 1, "@3- matches prefix long");
	expect_match("a@2~4'", "aa", 1, "@2~4' on aa");
	expect_match("a@2~4'", "aaaa", 1, "@2~4' on aaaa");
	expect_match("a@2~4'", "a", 0, "@2~4' fails on a");
	expect_match("a@2^4;", "a", 1, "@2^4; on a (outside, fewer)");
	expect_match("a@2^4;", "aaaaa", 1, "@2^4; on aaaaa (outside, more)");
	/* @2^4; on "aaa": count=3 is inside range, so quantifier fails; longest outside prefix is "a" (count=1) */
	expect_match("a@2^4;", "aaa", 1, "@2^4; on aaa gets prefix outside");

	printf("\n=== captured @ quantities ===\n");
	expect_qty("a@()'", "aaa", 1, 3, "@()' count 3");
	expect_qty("a@()'", "a", 1, 1, "@()' count 1");
	expect_qty("a@(2)+", "aaa", 1, 3, "@(2)+ count 3");
	expect_qty("a@(2)+", "a", 0, 0, "@(2)+ fails on 1");
	expect_qty("a@(3)-", "aa", 1, 2, "@(3)- count 2");
	expect_qty("a@(3)-", "aaa", 1, 3, "@(3)- count 3");
	expect_qty("a@(2~4)'", "aaa", 1, 3, "@(2~4)' count 3");
	expect_qty("a@(2^4);", "aaaaa", 1, 5, "@(2^4); count 5");

	printf("\n=== mixed captures + @ ===\n");
	expect_cap("(a)@()'", "aaa", 1, "a", "(a)@()' group \\1");
	expect_qty("(a)@()'", "aaa", 2, 3, "(a)@()' qty \\2");
	expect_qty("a@()'(b)", "aab", 1, 2, "a@()'(b) qty \\1");
	expect_cap("a@()'(b)", "aab", 2, "b", "a@()'(b) group \\2");
	error_edges();

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
