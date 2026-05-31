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

static int cmp_uv(uvlong yod, uvlong chet, void *arg) {
	( void ) arg;
	return yod < chet ? -1 : yod > chet ? 1 : 0;
}

static bool yods_are(int sk, uvlong *want, uvlong n) {
	uvlong pins [32] = {0};
	if (skpins(sk, pins, arrlen(pins)) != n) return false;
	for (uvlong i = 0; i < n; i++)
		if (skyod(sk, pins [i]) != want [i]) return false;
	return true;
}

static void creation_order_and_duplicates(void) {
	printf("\n=== skiplist: creation order and duplicates ===\n");
	int sk = mkskiplist(0);
	CHECK(sk >= 0, "mkskiplist");
	uvlong p5a = skput(sk, 5, cmp_uv, null);
	uvlong p1  = skput(sk, 1, cmp_uv, null);
	uvlong p3  = skput(sk, 3, cmp_uv, null);
	uvlong p5b = skput(sk, 5, cmp_uv, null);
	uvlong p5c = skput(sk, 5, cmp_uv, null);
	CHECK(p5a != ( uvlong ) -1
	        && p1 != ( uvlong ) -1
	        && p3 != ( uvlong ) -1
	        && p5b != ( uvlong ) -1
	        && p5c != ( uvlong ) -1,
	      "skput returns pins");
	uvlong want [] = {1, 3, 5, 5, 5};
	CHECK(yods_are(sk, want, arrlen(want)), "skput keeps sorted order and inserts duplicates after equals");
	CHECK(skyod(sk, p3) == 3 && sknpin(sk) == 5, "skyod and sknpin report live pins");
	CHECK(skfirst(sk) == p1 && sklast(sk) == p5c, "skfirst and sklast return ordered ends");
	rmskiplist(sk);
}

static void find_bounds_and_adjacency(void) {
	printf("\n=== skiplist: find bounds and adjacency ===\n");
	int sk = mkskiplist(0);
	CHECK(sk >= 0, "mkskiplist for queries");
	uvlong p2    = skput(sk, 2, cmp_uv, null);
	uvlong p4    = skput(sk, 4, cmp_uv, null);
	uvlong p6    = skput(sk, 6, cmp_uv, null);
	uvlong p8    = skput(sk, 8, cmp_uv, null);
	uvlong found = 99;
	CHECK(skfind(sk, 4, cmp_uv, null, &found) && found == p4, "skfind returns equal pin");
	CHECK(skfind(sk, 4, cmp_uv, null, null), "skfind accepts null output as existence check");
	CHECK(sklower(sk, 5, cmp_uv, null) == p6, "sklower returns first greater when no equal");
	CHECK(skupper(sk, 6, cmp_uv, null) == p8, "skupper returns first strictly greater");
	CHECK(!skfind(sk, 5, cmp_uv, null, &found) && found == p4 && !err(), "skfind miss is quiet false");
	check_expected_error(sklower(sk, 9, cmp_uv, null) == ( uvlong ) -1, "sklower no result sets error");
	check_expected_error(skupper(sk, 8, cmp_uv, null) == ( uvlong ) -1, "skupper no result sets error");
	uvlong out = 99;
	CHECK(sknext(sk, p2, &out) && out == p4, "sknext returns next pin");
	CHECK(skprev(sk, p8, &out) && out == p6, "skprev returns previous pin");
	CHECK(!skprev(sk, p2, &out) && !err(), "skprev at first is quiet false");
	CHECK(!sknext(sk, p8, null) && !err(), "sknext at last is quiet false with null out");
	rmskiplist(sk);
}

static void enumeration_and_deletion(void) {
	printf("\n=== skiplist: enumeration and deletion ===\n");
	int sk = mkskiplist(0);
	CHECK(sk >= 0, "mkskiplist for deletion");
	uvlong p1      = skput(sk, 1, cmp_uv, null);
	uvlong p2a     = skput(sk, 2, cmp_uv, null);
	uvlong p2b     = skput(sk, 2, cmp_uv, null);
	uvlong p2c     = skput(sk, 2, cmp_uv, null);
	uvlong p3      = skput(sk, 3, cmp_uv, null);
	uvlong buf [8] = {99, 99, 99, 99, 99, 99, 99, 99};
	CHECK(skpins(sk, null, 0) == 5, "skpins counts without buffer");
	CHECK(skpins(sk, buf, 2) == 5 && buf [0] == p1 && buf [1] == p2a && buf [2] == 99, "skpins respects cap");
	CHECK(skdel(sk, p2a), "skdel deletes one pin");
	CHECK(skyod(sk, p2c) == 2, "third duplicate remains live before batch delete");
	uvlong want_after_del [] = {1, 2, 2, 3};
	CHECK(yods_are(sk, want_after_del, arrlen(want_after_del)) && sknpin(sk) == 4, "skdel updates ordered list");
	CHECK(skdels(sk, p2b, cmp_uv, null) == 2, "skdels deletes forward equal run from pin");
	uvlong want_after_dels [] = {1, 3};
	CHECK(yods_are(sk, want_after_dels, arrlen(want_after_dels)) && skfirst(sk) == p1 && sklast(sk) == p3,
	      "skdels leaves surrounding pins");
	check_expected_error(!skdel(sk, p2b), "skdel rejects dead pin");
	rmskiplist(sk);
}

static void growth_and_reuse(void) {
	printf("\n=== skiplist: growth and reuse ===\n");
	int sk = mkskiplist(0);
	CHECK(sk >= 0, "mkskiplist for growth");
	bool ok = true;
	for (uvlong i = 200; i > 0; i--) {
		uvlong p = skput(sk, i, cmp_uv, null);
		ok       = ok && p != ( uvlong ) -1;
	}
	CHECK(ok && sknpin(sk) == 200 && skyod(sk, skfirst(sk)) == 1 && skyod(sk, sklast(sk)) == 200,
	      "skiplist handles many insertions");
	rmskiplist(sk);
	CHECK(sknpin(sk) == 0, "removed skiplist count is zero");
	int reused = mkskiplist(0);
	CHECK(reused == sk, "mkskiplist reuses removed descriptor");
	rmskiplist(reused);

	int ids [80];
	ok = true;
	for (uint i = 0; i < arrlen(ids); i++) {
		ids [i] = mkskiplist(0);
		ok      = ok && ids [i] >= 0;
	}
	CHECK(ok, "skiplist descriptor table grows");
	for (uint i = 0; i < arrlen(ids); i++) rmskiplist(ids [i]);
}

static void invalid_inputs(void) {
	printf("\n=== skiplist: invalid inputs ===\n");
	int sk = mkskiplist(0);
	CHECK(sk >= 0, "mkskiplist for invalids");
	check_expected_error(skfirst(sk) == ( uvlong ) -1, "skfirst empty sets error");
	check_expected_error(sklast(sk) == ( uvlong ) -1, "sklast empty sets error");
	check_expected_error(skput(-1, 1, cmp_uv, null) == ( uvlong ) -1, "skput invalid descriptor sets error");
	check_expected_error(skput(sk, 1, null, null) == ( uvlong ) -1, "skput null comparator sets error");
	uvlong p = skput(sk, 1, cmp_uv, null);
	CHECK(p != ( uvlong ) -1, "skput sample pin");
	check_expected_error(skyod(sk, 99) == 0, "skyod bad pin sets error");
	check_expected_error(!sknext(sk, 99, null), "sknext bad pin sets error");
	check_expected_error(!skprev(sk, 99, null), "skprev bad pin sets error");
	check_expected_error(skpins(sk, null, 1) == 0, "skpins null nonzero buffer sets error");
	check_expected_error(!skfind(sk, 1, null, null, null), "skfind null comparator sets error");
	check_expected_error(sklower(-1, 1, cmp_uv, null) == ( uvlong ) -1, "sklower invalid descriptor sets error");
	check_expected_error(skupper(sk, 1, null, null) == ( uvlong ) -1, "skupper null comparator sets error");
	check_expected_error(skdels(sk, 99, cmp_uv, null) == 0, "skdels bad pin sets error");
	rmskiplist(sk);
}

int main(void) {
	creation_order_and_duplicates();
	find_bounds_and_adjacency();
	enumeration_and_deletion();
	growth_and_reuse();
	invalid_inputs();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
