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

static void check_expected_error(bool ok, char *label) {
	CHECK(ok && err(), label);
	errmsg(null);
}

static bool pair_key_eq(uvlong *fields, char *a, char *b) {
	uvlong *pair = ( uvlong * ) fields [1];
	return fields [0] == sizeof(uvlong) * 4
	    && pair [0] == strlen(a)
	    && pair [2] == strlen(b)
	    && memcmp(( void * ) pair [1], a, pair [0]) == 0
	    && memcmp(( void * ) pair [3], b, pair [2]) == 0;
}

static uvlong pair_count(int ms, char *a, char *b) {
	int    it         = mkiter(ms);
	uvlong fields [3] = {0};
	uvlong count      = 0;
	while (next(it, fields))
		if (pair_key_eq(fields, a, b)) {
			count = fields [2];
			break;
		}
	rmiter(it);
	return count;
}

int main(void) {
	printf("=== multiset: basic multiplicity ===\n");
	int ms = mkmultiset(0);
	CHECK(ms >= 0, "mkmultiset");
	CHECK(silotype_of(ms) == silo_multiset, "silotype multiset");

	addms(ms, "cat", 3);
	addms(ms, "cat", 3);
	addms(ms, "dog", 3);
	CHECK(memms(ms, "cat", 3), "memms cat");
	CHECK(cntms(ms, "cat", 3) == 2, "cat count 2");
	CHECK(cntms(ms, "dog", 3) == 1, "dog count 1");
	CHECK(cntms(ms, "eel", 3) == 0 && !err(), "cntms missing key is quiet");
	CHECK(carten(ms) == 2, "cardinality counts distinct keys");

	delms(ms, "cat", 3);
	CHECK(cntms(ms, "cat", 3) == 1, "delms decrements");
	delms(ms, "cat", 3);
	CHECK(!memms(ms, "cat", 3) && cntms(ms, "cat", 3) == 0, "delms removes at zero");
	prgms(ms, "dog", 3);
	CHECK(carten(ms) == 0, "prgms removes key");
	prgms(ms, "missing", 7);
	CHECK(!err(), "prgms missing key is quiet");
	addms(ms, null, 0);
	CHECK(memms(ms, null, 0) && cntms(ms, null, 0) == 1 && !err(), "multiset accepts null zero-length key");

	printf("\n=== multiset: algebra ===\n");
	int a = mkmultiset(0);
	int b = mkmultiset(0);
	addms(a, "x", 1);
	addms(a, "x", 1);
	addms(a, "y", 1);
	addms(b, "x", 1);
	addms(b, "z", 1);
	addms(b, "z", 1);
	addtums(a, b);
	CHECK(cntms(a, "x", 1) == 3, "addtums adds shared count");
	CHECK(cntms(a, "z", 1) == 2, "addtums adds new key");

	int u = mkmultiset(0);
	addms(u, "x", 1);
	unionms(u, a);
	CHECK(cntms(u, "x", 1) == 3 && cntms(u, "z", 1) == 2, "unionms max counts");

	int n = mkmultiset(0);
	addms(n, "x", 1);
	addms(n, "x", 1);
	addms(n, "z", 1);
	intxnms(u, n);
	CHECK(cntms(u, "x", 1) == 2, "intxnms min shared");
	CHECK(cntms(u, "z", 1) == 1, "intxnms min z");
	CHECK(cntms(u, "y", 1) == 0, "intxnms drops absent");

	diffms(a, n);
	CHECK(cntms(a, "x", 1) == 1, "diffms subtract x");
	CHECK(cntms(a, "z", 1) == 1, "diffms subtract z");
	CHECK(cntms(a, "y", 1) == 1, "diffms keeps y");

	int s1 = mkmultiset(0);
	int s2 = mkmultiset(0);
	addms(s1, "a", 1);
	addms(s1, "a", 1);
	addms(s1, "b", 1);
	addms(s2, "a", 1);
	addms(s2, "c", 1);
	symmdiffms(s1, s2);
	CHECK(cntms(s1, "a", 1) == 1, "symmdiffms abs diff shared");
	CHECK(cntms(s1, "b", 1) == 1, "symmdiffms keeps left-only");
	CHECK(cntms(s1, "c", 1) == 1, "symmdiffms adds right-only");
	CHECK(submultisets(s2, s1), "submultisets true");
	CHECK(!submultisets(s1, s2), "submultisets false");
	CHECK(simsubmss(s1, s2, -1, -1), "simsubmss wildcard accepts any excess");
	CHECK(simsubmss(s2, s1, 0, 0), "simsubmss exact submultiset accepts");
	CHECK(!simsubmss(s1, s2, 0, 0), "simsubmss exact rejects excess");
	CHECK(simsubmss(s1, s2, 1, 2), "simsubmss bounded excess accepts");
	CHECK(!simsubmss(s1, s2, 0, 2), "simsubmss per-key deviation rejects");
	CHECK(!simsubmss(s1, s2, 1, 0), "simsubmss total excess rejects");

	teem(s1);
	CHECK(carten(s1) == 0 && cntms(s1, "a", 1) == 0, "teem multiset");

	printf("\n=== multiset: cartesian product ===\n");
	int pa = mkmultiset(0);
	int pb = mkmultiset(0);
	addms(pa, "a", 1);
	addms(pa, "a", 1);
	addms(pa, "b", 1);
	addms(pb, "x", 1);
	addms(pb, "y", 1);
	addms(pb, "y", 1);
	int prod = cartprodms(0, pa, pb);
	CHECK(prod >= 0 && silotype_of(prod) == silo_multiset, "cartprodms returns multiset");
	CHECK(pair_count(prod, "a", "x") == 2, "cartprodms multiplies ax count");
	CHECK(pair_count(prod, "a", "y") == 4, "cartprodms multiplies ay count");
	CHECK(pair_count(prod, "b", "x") == 1, "cartprodms multiplies bx count");
	CHECK(pair_count(prod, "b", "y") == 2, "cartprodms multiplies by count");
	CHECK(carten(prod) == 4, "cartprodms distinct pair count");

	printf("\n=== multiset: errors ===\n");
	check_expected_error(cntms(-1, "x", 1) == 0, "cntms invalid descriptor sets error");
	check_expected_error(!memms(-1, "x", 1), "memms invalid descriptor sets error");
	addms(-1, "x", 1);
	check_expected_error(true, "addms invalid descriptor sets error");
	addms(ms, null, 1);
	check_expected_error(true, "addms null nonzero key sets error");
	delms(ms, null, 1);
	check_expected_error(true, "delms null nonzero key sets error");
	prgms(ms, null, 1);
	check_expected_error(true, "prgms null nonzero key sets error");
	check_expected_error(!submultisets(-1, ms), "submultisets bad operand sets error");
	check_expected_error(!simsubmss(ms, -1, 0, 0), "simsubmss bad operand sets error");
	check_expected_error(cartprodms(0, ms, -1) < 0, "cartprodms bad operand sets error");

	rmmultiset(ms);
	rmmultiset(a);
	rmmultiset(b);
	rmmultiset(u);
	rmmultiset(n);
	rmmultiset(s1);
	rmmultiset(s2);
	rmmultiset(pa);
	rmmultiset(pb);
	rmmultiset(prod);

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
