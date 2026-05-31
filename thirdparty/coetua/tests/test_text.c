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

static void slitr_is(slitr got, char *want, char *label) { CHECK(slitreq(got, slitr_of(want)), label); }

static void strand_is(int strand, char *want, char *label) { slitr_is(obslitr(strand), want, label); }

static void check_expected_error(bool ok, char *label) {
	CHECK(ok && err(), label);
	errmsg(null);
}

static void rune_seq_is(int seq, rune *want, uvlong n, char *label) {
	bool ok = seq >= 0 && slen(seq) == n;
	for (uvlong i = 0; ok && i < n; i++) ok = pick(seq, ( vlong ) i) == want [i];
	CHECK(ok, label);
}

static void roundtrip_rune(rune r, int width, char *label) {
	char buf [8];
	rune got = 0;
	int  n   = runetochar(buf, &r);
	CHECK(n == width && chartorune(&got, buf) == width && got == r, label);
}

static void decodes_error(char *s, char *label) {
	rune got = 0;
	CHECK(chartorune(&got, s) == 1 && got == Runeerror, label);
}

static void slitr_spec(void) {
	printf("=== slitr ===\n");
	slitr a = slitr_of("hello");
	CHECK(a.len == 5 && memcmp(a.s, "hello", 5) == 0, "slitr_of captures pointer and length");
	CHECK(slitreq(a, slitr_of("hello")), "slitreq equal");
	CHECK(!slitreq(a, slitr_of("world")), "slitreq different");
	CHECK(slitrcmp(slitr_of("abc"), slitr_of("abd")) < 0, "slitrcmp less");
	CHECK(slitrcmp(slitr_of("abd"), slitr_of("abc")) > 0, "slitrcmp greater");
	CHECK(slitrcmp(slitr_of("abc"), slitr_of("abc")) == 0, "slitrcmp equal");
	CHECK(slitrcmp(slitr_of("ab"), slitr_of("abc")) < 0, "slitrcmp prefix");

	slitr_is(subsitr(slitr_of("abcdef"), 1, 3), "bcd", "subsitr positive range");
	slitr_is(subsitr(slitr_of("abcdef"), -3, -1), "def", "subsitr negative range");
	CHECK(subsitr(slitr_of("ab"), 5, 10).len == 0, "subsitr out of bounds is empty");

	slitr dup = slitrdup(slitr_of("copy me"), 0);
	CHECK(dup.len == 7 && dup.s != null && memcmp(dup.s, "copy me", 7) == 0 && dup.s [7] == '\0',
	      "slitrdup copies and terminates");
}

static void strand_spec(void) {
	printf("\n=== strand ===\n");
	int st = mkstrand(0);
	CHECK(st >= 0, "mkstrand returns descriptor");

	concat(st, "hello");
	strand_is(st, "hello", "concat appends C string");
	concat(st, " world");
	strand_is(st, "hello world", "concat extends existing text");
	concats(st, slitr_of("!"));
	strand_is(st, "hello world!", "concats appends slitr");

	putstr(st, 5, " cruel");
	strand_is(st, "hello cruel world!", "putstr inserts at byte position");
	dropstr(st, 6, 6);
	strand_is(st, "hello world!", "dropstr removes forward span");
	putstr(st, -1, "?");
	strand_is(st, "hello world?!", "putstr negative index counts from end");
	dropstr(st, -2, -6);
	strand_is(st, "hello?!", "dropstr negative count removes before position");

	rmstrand(st);
	int reused = mkstrand(0);
	CHECK(obslitr(reused).len == 0, "mkstrand reuse starts empty");
	repeat(reused, 100);
	CHECK(obslitr(reused).len == 0, "repeat on empty strand is empty");
	concat(reused, "x");
	repeat(reused, 5);
	strand_is(reused, "xxxxx", "repeat duplicates content");
	concatr(reused, '!');
	strand_is(reused, "xxxxx!", "concatr appends rune");
	rmstrand(reused);

	int  live [COETUA_STRAND_TABLE_SEED + 20];
	bool live_ok = true;
	for (int i = 0; i < COETUA_STRAND_TABLE_SEED + 20; i++) {
		live [i] = mkstrand(0);
		if (live [i] < 0) {
			live_ok = false;
			break;
		}
		concatr(live [i], ( rune ) ('a' + i % 26));
	}
	for (int i = 0; i < COETUA_STRAND_TABLE_SEED + 20 && live_ok; i++) {
		slitr s = obslitr(live [i]);
		if (s.len != 1 || s.s [0] != ( char ) ('a' + i % 26)) live_ok = false;
	}
	CHECK(live_ok, "strand descriptors grow past configured seed cap");
	for (int i = 0; i < COETUA_STRAND_TABLE_SEED + 20; i++)
		if (live [i] >= 0) rmstrand(live [i]);
}

static void strand_error_spec(void) {
	printf("\n=== strand errors ===\n");
	check_expected_error(obslitr(-1).len == 0, "obslitr bad strand sets error");
	concats(-1, slitr_of("x"));
	check_expected_error(true, "concats bad strand sets error");
	putstr(-1, 0, "x");
	check_expected_error(true, "putstr bad strand sets error");
	dropstr(-1, 0, 1);
	check_expected_error(true, "dropstr bad strand sets error");
	repeat(-1, 2);
	check_expected_error(true, "repeat bad strand sets error");
	concat(-1, null);
	CHECK(!err(), "concat null remains quiet");

	int st = mkstrand(0);
	CHECK(st >= 0, "mkstrand for quiet edge cases");
	putstr(st, 99, "x");
	dropstr(st, 99, 1);
	concats(st, slitr_empty());
	CHECK(obslitr(st).len == 0 && !err(), "empty/out-of-range strand operations stay quiet");
	rmstrand(st);
}

static void strand_helper_spec(void) {
	printf("\n=== strand helpers ===\n");
	char *mixed_utf = "A\xe4\xb8\x96\xf0\x9f\x98\x80";
	rune  mixed []  = {'A', 0x4e16, 0x1f600, 0};
	rune  badutf [] = {Runeerror, Runeerror, 0};
	rune  empty []  = {0};
	int   st        = catstr(0, "one", " ", "two", "", " three", null);
	CHECK(st >= 0, "catstr returns strand descriptor");
	strand_is(st, "one two three", "catstr concatenates C strings until null sentinel");
	rmstrand(st);

	st = runetostr(0, mixed);
	CHECK(st >= 0, "runetostr returns strand descriptor");
	strand_is(st, mixed_utf, "runetostr encodes zero-terminated rune array");
	rmstrand(st);

	rune badrs [] = {0xd800, 0};
	st            = runetostr(0, badrs);
	CHECK(st >= 0 && slitreq(obslitr(st), slitr_of("\xef\xbf\xbd")), "runetostr maps invalid rune to Runeerror bytes");
	rmstrand(st);

	st = catstr(0, null);
	CHECK(st >= 0 && obslitr(st).len == 0, "catstr with only sentinel returns empty strand");
	rmstrand(st);

	st = runetostr(0, null);
	CHECK(st >= 0 && obslitr(st).len == 0, "runetostr null input returns empty strand");
	rmstrand(st);

	int seq = strtorune(0, mixed_utf);
	CHECK(seq >= 0, "strtorune returns sequence descriptor");
	rune_seq_is(seq, mixed, arrlen(mixed), "strtorune decodes UTF-8 into zero-terminated rune sequence");
	rmseq(seq);

	seq = strtorune(0, "\xc0\x80");
	rune_seq_is(seq, badutf, arrlen(badutf), "strtorune preserves malformed bytes as Runeerror runes");
	rmseq(seq);

	seq = strtorune(0, null);
	rune_seq_is(seq, empty, arrlen(empty), "strtorune null input returns terminator-only sequence");
	rmseq(seq);
}

static void rune_spec(void) {
	printf("\n=== rune / UTF-8 ===\n");
	CHECK(runelen('A') == 1, "runelen ASCII");
	CHECK(runelen(0xc0) == 2, "runelen 2-byte");
	CHECK(runelen(0x4e16) == 3, "runelen 3-byte");
	CHECK(runelen(0x1f600) == 4, "runelen 4-byte");
	CHECK(runelen(0xd800) == 3 && runelen(0x110000) == 3, "invalid runes encode as Runeerror width");
	CHECK(chkrune(0x4e16) && !chkrune(0xd800) && !chkrune(0x110000), "chkrune rejects surrogates and above max");

	roundtrip_rune('A', 1, "ASCII rune roundtrip");
	roundtrip_rune(0x4e16, 3, "3-byte rune roundtrip");
	roundtrip_rune(0x1f600, 4, "4-byte rune roundtrip");

	rune bad = 0xd800;
	char buf [UTFmax];
	rune got = 0;
	CHECK(runetochar(buf, &bad) == 3 && chartorune(&got, buf) == 3 && got == Runeerror,
	      "runetochar maps invalid rune to Runeerror");

	rune rs [] = {'A', 0x4e16, 0x1f600};
	CHECK(runenlen(rs, 3) == 8, "runenlen sums encoded widths");
}

static void utf_boundary_spec(void) {
	printf("\n=== UTF-8 boundaries ===\n");
	rune r;
	char two []        = {( char ) 0xc2u, ( char ) 0xa2u};
	char three []      = {( char ) 0xe4u, ( char ) 0xb8u, ( char ) 0x96u};
	char four []       = {( char ) 0xf0u, ( char ) 0x9fu, ( char ) 0x98u, ( char ) 0x80u};
	char cont_start [] = {( char ) 0x80u};
	char over2 []      = {( char ) 0xc0u, ( char ) 0x80u};
	char over3 []      = {( char ) 0xe0u, ( char ) 0x80u, ( char ) 0x80u};
	char over4 []      = {( char ) 0xf0u, ( char ) 0x80u, ( char ) 0x80u, ( char ) 0x80u};
	char surrogate []  = {( char ) 0xedu, ( char ) 0xa0u, ( char ) 0x80u};
	char maxr []       = {( char ) 0xf4u, ( char ) 0x8fu, ( char ) 0xbfu, ( char ) 0xbfu};
	char above_max []  = {( char ) 0xf4u, ( char ) 0x90u, ( char ) 0x80u, ( char ) 0x80u};
	char high_lead []  = {( char ) 0xf8u};

	CHECK(fullrune(two, 1) == 0 && fullrune(two, 2), "fullrune 2-byte incomplete/complete");
	CHECK(fullrune(three, 1) == 0 && fullrune(three, 2) == 0 && fullrune(three, 3),
	      "fullrune 3-byte incomplete/complete");
	CHECK(fullrune(four, 1) == 0 && fullrune(four, 2) == 0 && fullrune(four, 3) == 0 && fullrune(four, 4),
	      "fullrune 4-byte incomplete/complete");

	CHECK(fullrune(cont_start, sizeof(cont_start)) == 1, "fullrune has enough bytes for bad continuation start");
	decodes_error(cont_start, "continuation start decodes Runeerror");
	decodes_error(over2, "overlong 2-byte decodes Runeerror");
	decodes_error(over3, "overlong 3-byte decodes Runeerror");
	decodes_error(over4, "overlong 4-byte decodes Runeerror");
	decodes_error(surrogate, "surrogate decodes Runeerror");
	decodes_error(above_max, "above max decodes Runeerror");
	decodes_error(high_lead, "impossible leading byte decodes Runeerror");
	CHECK(fullrune(maxr, sizeof(maxr)) && chartorune(&r, maxr) == 4 && r == 0x10ffff, "accept max rune");
}

static void search_spec(void) {
	printf("\n=== UTF search ===\n");
	CHECK(utflen("hello") == 5, "utflen ASCII");
	CHECK(utflen("\xe4\xb8\x96\xe7\x95\x8c") == 2, "utflen UTF-8");
	CHECK(utflen("a\xc0\x80z") == 4, "utflen counts malformed bytes as Runeerror runes");
	CHECK(utfnlen("\xe4\xb8\x96\xe7\x95\x8c", 5) == 1, "utfnlen does not count partial final rune");

	char *hw      = "hello world";
	char *first_o = utfrune(hw, 'o');
	char *last_o  = utfrrune(hw, 'o');
	char *abc     = "abc";
	CHECK(first_o == hw + 4, "utfrune finds first rune");
	CHECK(last_o == hw + 7, "utfrrune finds last rune");
	CHECK(utfrune(abc, 'x') == null, "utfrune returns null when missing");
	CHECK(utfrune(abc, '\0') == &abc [3], "utfrune finds terminal NUL");

	CHECK(utfutf(hw, "wor") == hw + 6, "utfutf finds substring");
	CHECK(utfutf(hw, "xyz") == null, "utfutf returns null when missing");
	CHECK(utfutf("hello", "") != null, "utfutf accepts empty needle");

	char  dst [8];
	char *end = utfecpy(dst, dst + sizeof(dst), "ab\xe4\xb8\x96z");
	CHECK(strcmp(dst, "ab\xe4\xb8\x96z") == 0 && end == dst + strlen(dst),
	      "utfecpy copies complete source when it fits");
	end = utfecpy(dst, dst + 3, "\xe4\xb8\x96");
	CHECK(strcmp(dst, "") == 0 && end == dst, "utfecpy does not write partial first rune");
}

static void rune_type_spec(void) {
	printf("\n=== rune type / case ===\n");
	CHECK(isalpharune('A') && isupperrune('A') && istitlerune('A'), "ASCII upper alpha/title");
	CHECK(isalpharune('z') && islowerrune('z') && !isupperrune('z'), "ASCII lower alpha");
	CHECK(isspacerune(' ') && isspacerune('\n'), "ASCII spaces");
	CHECK(toupperrune('q') == 'Q' && tolowerrune('Q') == 'q', "ASCII case maps");
	CHECK(totitlerune('q') == 'Q', "ASCII title map");
	CHECK(isalpharune(0x03bb) && islowerrune(0x03bb), "Greek lambda is lower alpha");
	CHECK(toupperrune(0x03bb) == 0x039b && tolowerrune(0x039b) == 0x03bb, "Greek lambda case maps");
	CHECK(totitlerune(0x01c6) == 0x01c5 && istitlerune(0x01c5), "titlecase digraph maps");
	CHECK(tolowerrune(0x2603) == 0x2603 && toupperrune(0x2603) == 0x2603, "unmapped rune case is unchanged");
	CHECK(!isalpharune(0x110000), "out-of-range rune is not classified");
}

static void c_string_spec(void) {
	printf("\n=== C string helpers ===\n");
	char d [16];
	strecpy(d, d + sizeof(d), "hello");
	CHECK(strcmp(d, "hello") == 0, "strecpy copies");
	strecat(d, d + sizeof(d), " world");
	CHECK(strcmp(d, "hello world") == 0, "strecat appends");
	strecpy(d, d + 5, "too long for buffer");
	CHECK(strlen(d) <= 4, "strecpy truncates and terminates");
}

static void token_spec(void) {
	printf("\n=== tokens ===\n");
	char  buf [128];
	char *tok [8];

	strcpy(buf, "  alpha beta\tgamma\n");
	CHECK(tknize(buf, tok, 8) == 3
	        && strcmp(tok [0], "alpha") == 0
	        && strcmp(tok [1], "beta") == 0
	        && strcmp(tok [2], "gamma") == 0,
	      "tknize splits ASCII whitespace");

	strcpy(buf, "cmd 'two words' tail");
	CHECK(tknize(buf, tok, 8) == 3
	        && strcmp(tok [0], "cmd") == 0
	        && strcmp(tok [1], "two words") == 0
	        && strcmp(tok [2], "tail") == 0,
	      "tknize keeps quoted whitespace inside one token");

	strcpy(buf, "'' '''' 'a''b'");
	CHECK(
	  tknize(buf, tok, 8) == 3 && strcmp(tok [0], "") == 0 && strcmp(tok [1], "'") == 0 && strcmp(tok [2], "a'b") == 0,
	  "tknize uses doubled quotes for literal quotes");

	strcpy(buf, "one two three");
	CHECK(tknize(buf, tok, 2) == 3 && strcmp(tok [0], "one") == 0 && strcmp(tok [1], "two") == 0,
	      "tknize returns total tokens while storing only capacity");

	strcpy(buf, "one two");
	CHECK(tknize(buf, null, 8) == 2, "tknize accepts null output for count-only use");

	strcpy(buf, "one two");
	tok [0] = ( char * ) "sentinel";
	CHECK(tknize(buf, tok, 0) == 2 && strcmp(tok [0], "sentinel") == 0,
	      "tknize max zero counts without storing tokens");

	strcpy(buf, "   ");
	CHECK(tknize(buf, tok, 8) == 0, "tknize ignores all-whitespace input");

	CHECK(tknize(null, tok, 8) == 0, "tknize null input is empty");
}

int main(void) {
	slitr_spec();
	strand_spec();
	strand_error_spec();
	strand_helper_spec();
	rune_spec();
	utf_boundary_spec();
	search_spec();
	rune_type_spec();
	c_string_spec();
	token_spec();

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
