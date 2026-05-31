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

static bool yods_eq(uvlong pre, uvlong k, uvlong *want, uvlong n) {
	uvlong cur = k;
	for (uvlong i = 0; i < n; i++) {
		if (!cur || lsyod(cur) != want [i]) return false;
		uvlong next = lsstep(pre, cur);
		pre         = cur;
		cur         = next;
	}
	return cur == 0;
}

static void creation_and_yod(void) {
	printf("\n=== list: creation and yod ===\n");
	uvlong ys [] = {10, 20, 30};
	bead   b     = mklist(0, ys, arrlen(ys));
	CHECK(b.pre == 0 && b.fst != 0 && b.lst != 0, "mklist creates independent bead");
	CHECK(yods_eq(0, b.fst, ys, arrlen(ys)), "mklist preserves yod order");
	uvlong rev [] = {30, 20, 10};
	CHECK(yods_eq(0, b.lst, rev, arrlen(rev)), "opposite entrance reverses traversal");
	CHECK(lsyod(b.fst) == 10, "lsyod reads yod");
	rlsyod(b.fst, 11);
	CHECK(lsyod(b.fst) == 11, "rlsyod updates yod");
	CHECK(lsstep(0, 0) == 0 && lsyod(0) == 0, "zero knot is quiet null");
}

static void walks_and_buffers(void) {
	printf("\n=== list: walks and buffers ===\n");
	uvlong ys [] = {1, 2, 3, 4};
	bead   b     = mklist(0, ys, arrlen(ys));
	uvlong pre   = 99;
	uvlong end   = lsend(0, b.fst, &pre);
	CHECK(end == b.lst && pre != 0 && lsyod(pre) == 3, "lsend returns end and end predecessor");
	CHECK(lsend(0, 0, &pre) == 0 && pre == 0, "lsend handles empty walk");
	CHECK(lslen(0, b.fst) == 4 && lslen(0, 0) == 0, "lslen counts directional walk");
	uvlong buf [4] = {99, 99, 99, 99};
	CHECK(lsknots(0, b.fst, null, 0) == 4, "lsknots counts without buffer");
	CHECK(lsknots(0, b.fst, buf, 2) == 4 && buf [0] == b.fst && buf [2] == 99, "lsknots respects cap");
	CHECK(lsknots(0, b.fst, null, 1) == 0, "lsknots rejects null nonzero buffer");
	check_expected_error(true, "lsknots null buffer sets error");
}

static void put_cat_and_cut(void) {
	printf("\n=== list: put cat and cut ===\n");
	uvlong baseys [] = {1, 2, 3};
	bead   base      = mklist(0, baseys, arrlen(baseys));
	uvlong k1        = base.fst;
	uvlong k2        = lsstep(0, k1);
	uvlong k3        = lsstep(k1, k2);

	uvlong insys []  = {7, 8};
	bead   ins       = mklist(0, insys, arrlen(insys));
	lsput(0, k1, ins);
	uvlong want_put [] = {1, 7, 8, 2, 3};
	CHECK(yods_eq(0, k1, want_put, arrlen(want_put)), "lsput inserts bead after directional knot");

	uvlong tailys [] = {9};
	bead   tail      = mklist(0, tailys, arrlen(tailys));
	lscat(0, k1, tail);
	uvlong want_cat [] = {1, 7, 8, 2, 3, 9};
	CHECK(yods_eq(0, k1, want_cat, arrlen(want_cat)), "lscat appends at directional end");

	bead   cut          = lscut((bead) {.pre = k1, .fst = ins.fst, .lst = ins.lst});
	uvlong want_base [] = {1, 2, 3, 9};
	CHECK(cut.pre == 0 && cut.fst == ins.fst && cut.lst == ins.lst, "lscut returns independent bead");
	CHECK(yods_eq(0, k1, want_base, arrlen(want_base)), "lscut reconnects surrounding component");
	CHECK(yods_eq(0, cut.fst, insys, arrlen(insys)), "lscut preserves cut bead order");
	CHECK(k2 != 0 && k3 != 0, "sample keeps middle knots addressable");

	uvlong singleys []         = {4, 5, 6};
	bead   singleb             = mklist(0, singleys, arrlen(singleys));
	uvlong s1                  = singleb.fst;
	uvlong s2                  = lsstep(0, s1);
	uvlong s3                  = lsstep(s1, s2);
	bead   singlecut           = lscut((bead) {.pre = s1, .fst = s2, .lst = s2});
	uvlong want_single_base [] = {4, 6};
	uvlong want_single_cut []  = {5};
	CHECK(yods_eq(0, s1, want_single_base, arrlen(want_single_base)), "lscut reconnects around single knot");
	CHECK(yods_eq(0, singlecut.fst, want_single_cut, arrlen(want_single_cut)), "lscut returns single knot bead");
	CHECK(s3 != 0, "single cut sample keeps following knot addressable");

	uvlong revbaseys [] = {1, 2, 3};
	bead   revbase      = mklist(0, revbaseys, arrlen(revbaseys));
	uvlong revtailys [] = {9};
	bead   revtail      = mklist(0, revtailys, arrlen(revtailys));
	lscat(0, revbase.lst, revtail);
	uvlong want_revcat [] = {3, 2, 1, 9};
	CHECK(yods_eq(0, revbase.lst, want_revcat, arrlen(want_revcat)), "lscat appends along reverse entrance");
}

static void splice_and_empty(void) {
	printf("\n=== list: splice and empty bead ===\n");
	bead empty = mklist(0, null, 0);
	CHECK(empty.pre == 0 && empty.fst == 0 && empty.lst == 0 && !err(), "mklist empty returns identity bead");

	uvlong ys [] = {1, 2, 3, 4};
	bead   b     = mklist(0, ys, arrlen(ys));
	uvlong k1    = b.fst;
	uvlong k2    = lsstep(0, k1);
	uvlong k3    = lsstep(k1, k2);
	lscut(empty);
	lsput(0, k1, empty);
	lscat(0, k1, empty);
	lssplice(0, k1, empty);
	CHECK(yods_eq(0, k1, ys, arrlen(ys)), "empty bead operations are no-ops");

	lssplice(0, k1, (bead) {.pre = k2, .fst = k3, .lst = k3});
	uvlong want [] = {1, 3, 2, 4};
	CHECK(yods_eq(0, k1, want, arrlen(want)), "lssplice cuts then puts bead at destination");

	uvlong manyys [] = {1, 2, 3, 4, 5};
	bead   many      = mklist(0, manyys, arrlen(manyys));
	uvlong m1        = many.fst;
	uvlong m2        = lsstep(0, m1);
	uvlong m3        = lsstep(m1, m2);
	uvlong m4        = lsstep(m2, m3);
	uvlong m5        = lsstep(m3, m4);
	lssplice(m4, m5, (bead) {.pre = m1, .fst = m2, .lst = m3});
	uvlong want_many [] = {1, 4, 5, 2, 3};
	CHECK(yods_eq(0, m1, want_many, arrlen(want_many)), "lssplice moves multi-knot bead");
}

static void remove_component(void) {
	printf("\n=== list: remove component ===\n");
	uvlong ys [] = {5, 6, 7, 8};
	bead   b     = mklist(0, ys, arrlen(ys));
	uvlong k1    = b.fst;
	uvlong k2    = lsstep(0, k1);
	uvlong k3    = lsstep(k1, k2);
	uvlong k4    = lsstep(k2, k3);
	rmlist(0, k2, k3);
	CHECK(lsyod(k1) == 0 && lsyod(k2) == 0 && lsyod(k3) == 0 && lsyod(k4) == 0,
	      "rmlist clears component from middle adjacent pair");
	CHECK(lsstep(0, k1) == 0 && lsstep(0, k4) == 0, "rmlist clears adjacency fields");

	uvlong one [] = {42};
	b             = mklist(0, one, arrlen(one));
	rmlist(0, 0, b.fst);
	CHECK(lsyod(b.fst) == 0 && lsstep(0, b.fst) == 0, "rmlist accepts 0,k entrance");
	b = mklist(0, one, arrlen(one));
	rmlist(0, b.fst, 0);
	CHECK(lsyod(b.fst) == 0 && lsstep(0, b.fst) == 0, "rmlist accepts k,0 entrance");
}

static void errors(void) {
	printf("\n=== list: errors ===\n");
	uvlong y = 1;
	CHECK(mklist(0, null, 1).fst == 0, "mklist rejects null nonempty yods");
	check_expected_error(true, "mklist null yods sets error");
	CHECK(mklist(-1, &y, 1).fst == 0, "mklist rejects invalid arena");
	check_expected_error(true, "mklist invalid arena sets error");
	lsput(0, 0, mklist(0, &y, 1));
	check_expected_error(true, "lsput rejects nonempty bead without target");
	bead b = mklist(0, &y, 1);
	CHECK(lscut((bead) {.pre = 0, .fst = 0, .lst = b.fst}).fst == 0, "lscut rejects half-empty bead");
	check_expected_error(true, "lscut half-empty bead sets error");
	lsput(0, b.fst, (bead) {.pre = 0, .fst = b.fst, .lst = 0});
	check_expected_error(true, "lsput half-empty bead sets error");
}

int main(void) {
	creation_and_yod();
	walks_and_buffers();
	put_cat_and_cut();
	splice_and_empty();
	remove_component();
	errors();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
