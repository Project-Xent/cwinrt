#include "coetua.h"
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

static void check_expected_error(bool ok, char *label) {
	CHECK(ok && err(), label);
	errmsg(null);
}

static int cmp_uv(uvlong key, uvlong chet, void *arg) {
	( void ) arg;
	return key < chet ? -1 : key > chet ? 1 : 0;
}

static bool has_ent(uvlong *xs, uvlong n, uvlong e) {
	for (uvlong i = 0; i < n; i++)
		if (xs [i] == e) return true;
	return false;
}

static void insertion_and_refs(void) {
	printf("\n=== amtrie: insertion refs duplicates ===\n");
	int t = mkamtrie(0);
	CHECK(t >= 0, "mkamtrie");
	uvlong a = amput(t, 0x11, 1, 10);
	uvlong b = amput(t, 0x11, 1, 11);
	uvlong c = amput(t, 0x51, 5, 50);
	CHECK(a != ( uvlong ) -1 && b != ( uvlong ) -1 && c != ( uvlong ) -1, "amput returns entries");
	CHECK(a != b && b != c, "amput always creates entries");
	CHECK(amhash(t, c) == 0x51 && amkey(t, c) == 5 && amref(t, b) == 11 && amnentry(t) == 3, "entry query APIs");
	ramref(t, b, 110);
	CHECK(amref(t, b) == 110, "ramref updates ref");
	rmamtrie(t);
}

static void find_matches_and_delete(void) {
	printf("\n=== amtrie: find matches delete ===\n");
	int t = mkamtrie(0);
	CHECK(t >= 0, "mkamtrie for find");
	uvlong a   = amput(t, 0xabc, 7, 1);
	uvlong b   = amput(t, 0xabc, 7, 2);
	uvlong c   = amput(t, 0xabc, 8, 3);
	uvlong d   = amput(t, 0xdef, 7, 4);
	uvlong out = 99;
	CHECK(amfind(t, 0xabc, 7, cmp_uv, null, &out) && out == a, "amfind returns first matching bucket entry");
	CHECK(amfind(t, 0xabc, 7, cmp_uv, null, null), "amfind accepts null output");
	CHECK(!amfind(t, 0xabc, 99, cmp_uv, null, &out) && !err(), "amfind miss is quiet");
	uvlong buf [8] = {0};
	CHECK(ammatches(t, 0xabc, 7, cmp_uv, null, buf, 1) == 2 && buf [0] == a && buf [1] == 0,
	      "ammatches counts beyond cap");
	CHECK(ammatches(t, 0xabc, 7, cmp_uv, null, null, 0) == 2, "ammatches null count");
	CHECK(amdels(t, 0xabc, 7, cmp_uv, null) == 2, "amdels deletes all equal in hash bucket");
	check_expected_error(amkey(t, a) == 0, "amdels deleted first");
	check_expected_error(amkey(t, b) == 0, "amdels deleted second");
	CHECK(amkey(t, c) == 8 && amkey(t, d) == 7 && amnentry(t) == 2, "amdels leaves other hash and key");
	CHECK(amdels(t, 0xabc, 7, cmp_uv, null) == 0 && !err(), "amdels absent is quiet");
	rmamtrie(t);
}

static void enumeration_and_prefix(void) {
	printf("\n=== amtrie: enumeration prefix ===\n");
	int t = mkamtrie(0);
	CHECK(t >= 0, "mkamtrie for prefix");
	uvlong e0 = amput(t, 0x0000, 0, 0);
	uvlong e1 = amput(t, 0x0041, 1, 1);
	uvlong e2 = amput(t, 0x1041, 2, 2);
	uvlong e3 = amput(t, 0x1081, 3, 3);
	uvlong e4 = amput(t, ( uvlong ) -1, 4, 4);
	uvlong buf [8];
	for (uvlong i = 0; i < arrlen(buf); i++) buf [i] = 99;
	CHECK(amentries(t, null, 0) == 5, "amentries counts all");
	CHECK(amentries(t, buf, 3) == 5 && has_ent(buf, 3, e0) && buf [3] == 99, "amentries respects cap");
	CHECK(amprefix(t, 0, 0, null, 0) == 5, "amprefix zero bits enumerates all");
	uvlong n = amprefix(t, 0x41, 6, buf, arrlen(buf));
	CHECK(n == 3 && has_ent(buf, n, e1) && has_ent(buf, n, e2) && has_ent(buf, n, e3), "amprefix uses low bits");
	n = amprefix(t, 0x1041, 13, buf, arrlen(buf));
	CHECK(n == 1 && buf [0] == e2, "amprefix handles partial fragment bits");
	CHECK(amprefix(t, ( uvlong ) -1, 64, buf, arrlen(buf)) == 1 && buf [0] == e4, "amprefix handles full hash");
	CHECK(amdel(t, e1) && amprefix(t, 0x41, 6, buf, arrlen(buf)) == 2 && has_ent(buf, 2, e2) && has_ent(buf, 2, e3),
	      "amdel prunes without losing siblings");
	check_expected_error(amprefix(t, 0, 65, null, 0) == 0, "amprefix rejects too many bits");
	rmamtrie(t);
}

static void deep_split_cases(void) {
	printf("\n=== amtrie: deep split cases ===\n");
	int t = mkamtrie(0);
	CHECK(t >= 0, "mkamtrie deep split");
	uvlong base    = 0x15ull | (0x2aull << 6);
	uvlong a       = amput(t, base | (0x01ull << 12), 1, 1);
	uvlong b       = amput(t, base | (0x02ull << 12), 2, 2);
	uvlong c       = amput(t, base | (0x03ull << 12), 3, 3);
	uvlong d       = amput(t, base | (0x02ull << 12) | (0x10ull << 18), 4, 4);
	uvlong buf [8] = {0};
	bool   ok      = a != ( uvlong ) -1 && b != ( uvlong ) -1 && c != ( uvlong ) -1 && d != ( uvlong ) -1;
	ok             = ok && amprefix(t, base, 12, buf, arrlen(buf)) == 4;
	ok             = ok
	              && amprefix(t, base | (0x02ull << 12), 18, buf, arrlen(buf)) == 2
	              && has_ent(buf, 2, b)
	              && has_ent(buf, 2, d);
	ok = ok && amprefix(t, base | (0x02ull << 12) | (0x10ull << 18), 24, buf, arrlen(buf)) == 1 && buf [0] == d;
	ok = ok && amprefix(t, base | (0x20ull << 12), 18, null, 0) == 0 && !err();
	ok = ok && amfind(t, amhash(t, d), 4, cmp_uv, null, null);
	ok = ok && amdel(t, d) && amprefix(t, base | (0x02ull << 12), 18, buf, arrlen(buf)) == 1 && buf [0] == b;
	CHECK(ok, "hashes sharing early fragments split at later levels");
	rmamtrie(t);

	t = mkamtrie(0);
	CHECK(t >= 0, "mkamtrie same hash bucket");
	uvlong ids [96];
	ok = true;
	for (uvlong i = 0; i < arrlen(ids); i++)
		ids [i] = amput(t, 0xfeedface, i, i + 100), ok = ok && ids [i] != ( uvlong ) -1;
	ok = ok && amprefix(t, 0xfeedface, 64, null, 0) == arrlen(ids);
	ok = ok && ammatches(t, 0xfeedface, 42, cmp_uv, null, buf, arrlen(buf)) == 1 && buf [0] == ids [42];
	for (uvlong i = 0; i < arrlen(ids); i += 2) ok = ok && amdel(t, ids [i]);
	ok = ok && amprefix(t, 0xfeedface, 64, null, 0) == arrlen(ids) / 2;
	for (uvlong i = 1; i < arrlen(ids); i += 2) ok = ok && amkey(t, ids [i]) == i;
	CHECK(ok, "identical hashes stay in bucket with stable entries");
	rmamtrie(t);
}

static void growth_and_pruning(void) {
	printf("\n=== amtrie: growth pruning ===\n");

	enum
	{
		N = 512,
	};

	int t = mkamtrie(0);
	CHECK(t >= 0, "mkamtrie growth");
	uvlong ids [N];
	bool   ok = true;
	for (uvlong i = 0; i < N; i++) {
		uvlong h = (i * 0x9e3779b97f4a7c15ull) ^ (i << 6);
		ids [i]  = amput(t, h, i, i + 1000);
		ok       = ok && ids [i] != ( uvlong ) -1;
	}
	ok = ok && amnentry(t) == N && amentries(t, null, 0) == N;
	for (uvlong i = 0; i < N; i += 2) ok = ok && amdel(t, ids [i]);
	ok = ok && amnentry(t) == N / 2 && amentries(t, null, 0) == N / 2;
	for (uvlong i = 1; i < N; i += 2) ok = ok && amkey(t, ids [i]) == i;
	for (uvlong i = 1; i < N; i += 2) ok = ok && amdel(t, ids [i]);
	CHECK(ok && amnentry(t) == 0 && amentries(t, null, 0) == 0, "many inserts and deletes prune to empty");
	rmamtrie(t);
}

static void invalids_and_reuse(void) {
	printf("\n=== amtrie: invalids reuse ===\n");
	int t = mkamtrie(0);
	CHECK(t >= 0, "mkamtrie invalids");
	check_expected_error(amput(-1, 1, 1, 1) == ( uvlong ) -1, "amput invalid descriptor sets error");
	uvlong e = amput(t, 1, 1, 1);
	CHECK(e != ( uvlong ) -1, "sample entry");
	check_expected_error(amref(t, 99) == 0, "amref bad entry sets error");
	check_expected_error(!amdel(t, 99), "amdel bad entry sets error");
	check_expected_error(!amfind(t, 1, 1, null, null, null), "amfind null comparator sets error");
	check_expected_error(ammatches(t, 1, 1, cmp_uv, null, null, 1) == 0, "ammatches bad buffer sets error");
	check_expected_error(amentries(t, null, 1) == 0, "amentries bad buffer sets error");
	CHECK(amdel(t, e), "delete sample");
	check_expected_error(amhash(t, e) == 0, "deleted entry rejected");
	rmamtrie(t);
	CHECK(amnentry(t) == 0, "removed amtrie count is zero");
	int reused = mkamtrie(0);
	CHECK(reused == t, "mkamtrie reuses descriptor");
	rmamtrie(reused);

	int  ids [80];
	bool ok = true;
	for (uvlong i = 0; i < arrlen(ids); i++) {
		ids [i] = mkamtrie(0);
		ok      = ok && ids [i] >= 0;
	}
	CHECK(ok, "amtrie descriptor table grows");
	for (uvlong i = 0; i < arrlen(ids); i++) rmamtrie(ids [i]);
}

int main(void) {
	insertion_and_refs();
	find_matches_and_delete();
	enumeration_and_prefix();
	deep_split_cases();
	growth_and_pruning();
	invalids_and_reuse();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
