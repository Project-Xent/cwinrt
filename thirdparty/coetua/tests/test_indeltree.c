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

static int cmp_uv(uvlong ref, uvlong chet, void *arg) {
	( void ) arg;
	return ref < chet ? -1 : ref > chet ? 1 : 0;
}

static bool refs_are(int t, uvlong *want, uvlong n) {
	uvlong ks [64] = {0};
	if (inknods(t, ks, arrlen(ks)) != n) return false;
	for (uvlong i = 0; i < n; i++)
		if (inref(t, ks [i]) != want [i]) return false;
	return true;
}

static bool refs_match_live(int t, bool *live, uvlong n) {
	uvlong ks [160] = {0};
	uvlong wantn    = 0;
	for (uvlong i = 1; i <= n; i++)
		if (live [i]) wantn++;
	if (inknods(t, ks, arrlen(ks)) != wantn) return false;
	uvlong at = 0;
	for (uvlong i = 1; i <= n; i++) {
		if (!live [i]) continue;
		if (at >= wantn || inref(t, ks [at]) != i) return false;
		if (at && (!inprev(t, ks [at], null) || !innext(t, ks [at - 1], null))) return false;
		at++;
	}
	return at == wantn && innknod(t) == wantn;
}

static void creation_order_and_duplicates(void) {
	printf("\n=== indeltree: creation order and duplicates ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree");
	uvlong k5a = inplace(t, 5, cmp_uv, null);
	uvlong k1  = inplace(t, 1, cmp_uv, null);
	uvlong k3  = inplace(t, 3, cmp_uv, null);
	uvlong k5b = inplace(t, 5, cmp_uv, null);
	uvlong k5c = inplace(t, 5, cmp_uv, null);
	CHECK(k5a && k1 && k3 && k5b && k5c, "inplace returns knod ids");
	uvlong want [] = {1, 3, 5, 5, 5};
	CHECK(refs_are(t, want, arrlen(want)), "inplace keeps sorted order and duplicates");
	CHECK(inref(t, k3) == 3 && innknod(t) == 5, "inref and innknod report live knods");
	CHECK(infirst(t) == k1 && inlast(t) == k5c, "infirst and inlast return ordered ends");
	rmindeltree(t);
}

static void insertion_patterns(void) {
	printf("\n=== indeltree: insertion patterns ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for insertion patterns");
	bool ok = true;
	for (uvlong i = 64; i > 0; i--) {
		uvlong k = inplace(t, i, cmp_uv, null);
		ok       = ok && k;
	}
	CHECK(ok && inref(t, infirst(t)) == 1 && inref(t, inlast(t)) == 64, "reverse insertion stays valid");
	rmindeltree(t);

	t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for duplicate-heavy insertion");
	uvlong ks [48];
	ok = true;
	for (uint i = 0; i < arrlen(ks); i++) {
		ks [i] = inplace(t, i % 3, cmp_uv, null);
		ok     = ok && ks [i];
	}
	uvlong pins [48];
	ok = ok && inknods(t, pins, arrlen(pins)) == arrlen(ks);
	for (uint i = 1; i < arrlen(pins); i++) ok = ok && inref(t, pins [i - 1]) <= inref(t, pins [i]);
	CHECK(ok, "duplicate-heavy insertion stays sorted and valid");
	rmindeltree(t);
}

static void find_bounds_and_adjacency(void) {
	printf("\n=== indeltree: find bounds and adjacency ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for queries");
	uvlong k2  = inplace(t, 2, cmp_uv, null);
	uvlong k4  = inplace(t, 4, cmp_uv, null);
	uvlong k6  = inplace(t, 6, cmp_uv, null);
	uvlong k8  = inplace(t, 8, cmp_uv, null);
	uvlong out = 99;
	CHECK(infind(t, 4, cmp_uv, null, &out) && out == k4, "infind returns equal knod");
	CHECK(infind(t, 4, cmp_uv, null, null), "infind accepts null output");
	CHECK(!infind(t, 5, cmp_uv, null, &out) && out == k4 && !err(), "infind miss is quiet false");
	CHECK(inlower(t, 5, cmp_uv, null) == k6, "inlower returns first greater when no equal");
	CHECK(inupper(t, 6, cmp_uv, null) == k8, "inupper returns first strictly greater");
	check_expected_error(inlower(t, 9, cmp_uv, null) == 0, "inlower no result sets error");
	check_expected_error(inupper(t, 8, cmp_uv, null) == 0, "inupper no result sets error");
	CHECK(innext(t, k2, &out) && out == k4, "innext returns next knod");
	CHECK(inprev(t, k8, &out) && out == k6, "inprev returns previous knod");
	CHECK(!inprev(t, k2, &out) && !err(), "inprev at first is quiet false");
	CHECK(!innext(t, k8, null) && !err(), "innext at last is quiet false with null out");
	rmindeltree(t);
}

static void enumeration_and_deletion(void) {
	printf("\n=== indeltree: enumeration and deletion ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for deletion");
	uvlong k1      = inplace(t, 1, cmp_uv, null);
	uvlong k2a     = inplace(t, 2, cmp_uv, null);
	uvlong k2b     = inplace(t, 2, cmp_uv, null);
	uvlong k2c     = inplace(t, 2, cmp_uv, null);
	uvlong k3      = inplace(t, 3, cmp_uv, null);
	uvlong buf [8] = {99, 99, 99, 99, 99, 99, 99, 99};
	CHECK(inknods(t, null, 0) == 5, "inknods counts without buffer");
	CHECK(inknods(t, buf, 2) == 5 && buf [0] == k1 && buf [1] == k2a && buf [2] == 99, "inknods respects cap");
	CHECK(indrop(t, k2a), "indrop deletes one knod by identity");
	check_expected_error(inref(t, k2a) == 0, "inref rejects dead knod");
	CHECK(inref(t, k2b) == 2 && inref(t, k2c) == 2, "other duplicate knods remain live");
	uvlong want_after_drop [] = {1, 2, 2, 3};
	CHECK(refs_are(t, want_after_drop, arrlen(want_after_drop)) && innknod(t) == 4, "indrop updates live order");
	CHECK(indels(t, 2, 1, cmp_uv, null) == 1, "indels deletes equals beyond exceed");
	uvlong want_after_dels [] = {1, 2, 3};
	CHECK(refs_are(t, want_after_dels, arrlen(want_after_dels)) && infirst(t) == k1 && inlast(t) == k3,
	      "indels keeps requested equal prefix");
	CHECK(indels(t, 9, 0, cmp_uv, null) == 0 && !err(), "indels absent key is quiet zero");
	check_expected_error(!indrop(t, k2a), "indrop rejects dead knod");
	rmindeltree(t);
}

static void identity_deletion_cases(void) {
	printf("\n=== indeltree: identity deletion cases ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for identity deletion");
	uvlong k4 = inplace(t, 4, cmp_uv, null);
	uvlong k2 = inplace(t, 2, cmp_uv, null);
	uvlong k6 = inplace(t, 6, cmp_uv, null);
	uvlong k1 = inplace(t, 1, cmp_uv, null);
	uvlong k3 = inplace(t, 3, cmp_uv, null);
	uvlong k5 = inplace(t, 5, cmp_uv, null);
	uvlong k7 = inplace(t, 7, cmp_uv, null);
	CHECK(k1 && k2 && k3 && k4 && k5 && k6 && k7, "inplace builds deletion sample");
	CHECK(indrop(t, k4), "indrop deletes two-child target");
	check_expected_error(inref(t, k4) == 0, "deleted two-child handle is dead");
	CHECK(inref(t, k5) == 5, "successor handle remains live after replacement");
	uvlong after_two_child [] = {1, 2, 3, 5, 6, 7};
	CHECK(refs_are(t, after_two_child, arrlen(after_two_child)), "two-child deletion preserves sorted shape");
	CHECK(indrop(t, k1), "indrop deletes leaf target");
	CHECK(indrop(t, k6), "indrop deletes remaining internal target");
	uvlong after_more [] = {2, 3, 5, 7};
	CHECK(refs_are(t, after_more, arrlen(after_more)), "mixed identity deletions preserve order");
	rmindeltree(t);
}

static void duplicate_batch_deletion_identity(void) {
	printf("\n=== indeltree: duplicate batch deletion identity ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for duplicate batch");
	uvlong before = inplace(t, 1, cmp_uv, null);
	uvlong ks [6];
	for (uint i = 0; i < arrlen(ks); i++) ks [i] = inplace(t, 7, cmp_uv, null);
	uvlong after = inplace(t, 9, cmp_uv, null);
	CHECK(before && after, "surrounding duplicate keys inserted");
	CHECK(indels(t, 7, 2, cmp_uv, null) == 4, "indels deletes duplicate suffix");
	CHECK(inref(t, ks [0]) == 7 && inref(t, ks [1]) == 7, "duplicate prefix handles remain live");
	for (uint i = 2; i < arrlen(ks); i++)
		check_expected_error(inref(t, ks [i]) == 0, "duplicate suffix handle is dead");
	uvlong want [] = {1, 7, 7, 9};
	CHECK(refs_are(t, want, arrlen(want)) && infirst(t) == before && inlast(t) == after,
	      "duplicate batch deletion leaves no visible tombstones");
	rmindeltree(t);
}

static void mixed_delete_stress(void) {
	printf("\n=== indeltree: mixed delete stress ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for mixed stress");

	enum
	{
		N = 120,
	};

	uvlong ids [N + 1]  = {0};
	bool   live [N + 1] = {0};
	bool   ok           = true;
	for (uvlong i = 1; i <= N; i++) {
		ids [i]  = inplace(t, i, cmp_uv, null);
		live [i] = true;
		ok       = ok && ids [i];
	}
	CHECK(ok && refs_match_live(t, live, N), "stress insertion sorted");
	for (uvlong step = 0; step < N; step++) {
		uvlong i = ((step * 37) % N) + 1;
		ok       = ok && indrop(t, ids [i]);
		live [i] = false;
		ok       = ok && refs_match_live(t, live, N);
	}
	CHECK(ok && innknod(t) == 0, "mixed deletion keeps order until empty");
	check_expected_error(infirst(t) == 0, "stress tree empty first sets error");
	rmindeltree(t);
}

static bool fill_range(int t, uvlong *ids, uvlong n) {
	bool ok = true;
	for (uvlong i = 1; i <= n; i++) {
		ids [i] = inplace(t, i, cmp_uv, null);
		ok      = ok && ids [i];
	}
	return ok;
}

static void delete_order_patterns(void) {
	printf("\n=== indeltree: delete order patterns ===\n");

	enum
	{
		N = 63,
	};

	uvlong ids [N + 1]  = {0};
	bool   live [N + 1] = {0};
	bool   ok           = true;

	int    t            = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for ascending delete");
	ok = fill_range(t, ids, N);
	for (uvlong i = 1; i <= N; i++) live [i] = true;
	for (uvlong i = 1; i <= N; i++) {
		ok       = ok && indrop(t, ids [i]);
		live [i] = false;
		ok       = ok && refs_match_live(t, live, N);
	}
	CHECK(ok && innknod(t) == 0, "ascending identity deletes stay valid");
	rmindeltree(t);

	t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for descending delete");
	memset(live, 0, sizeof(live));
	ok = fill_range(t, ids, N);
	for (uvlong i = 1; i <= N; i++) live [i] = true;
	for (uvlong i = N; i >= 1; i--) {
		ok       = ok && indrop(t, ids [i]);
		live [i] = false;
		ok       = ok && refs_match_live(t, live, N);
		if (i == 1) break;
	}
	CHECK(ok && innknod(t) == 0, "descending identity deletes stay valid");
	rmindeltree(t);
}

static void growth_and_reuse(void) {
	printf("\n=== indeltree: growth and reuse ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for growth");
	bool ok = true;
	for (uvlong i = 200; i > 0; i--) {
		uvlong k = inplace(t, i, cmp_uv, null);
		ok       = ok && k != 0;
	}
	CHECK(ok && innknod(t) == 200 && inref(t, infirst(t)) == 1 && inref(t, inlast(t)) == 200,
	      "indeltree handles many insertions");
	CHECK(indels(t, 100, 0, cmp_uv, null) == 1 && innknod(t) == 199, "indels removes matching singleton");
	rmindeltree(t);
	CHECK(innknod(t) == 0, "removed indeltree count is zero");
	int reused = mkindeltree(0);
	CHECK(reused == t, "mkindeltree reuses removed descriptor");
	rmindeltree(reused);

	int ids [80];
	ok = true;
	for (uint i = 0; i < arrlen(ids); i++) {
		ids [i] = mkindeltree(0);
		ok      = ok && ids [i] >= 0;
	}
	CHECK(ok, "indeltree descriptor table grows");
	for (uint i = 0; i < arrlen(ids); i++) rmindeltree(ids [i]);
}

static void invalid_inputs(void) {
	printf("\n=== indeltree: invalid inputs ===\n");
	int t = mkindeltree(0);
	CHECK(t >= 0, "mkindeltree for invalids");
	check_expected_error(infirst(t) == 0, "infirst empty sets error");
	check_expected_error(inlast(t) == 0, "inlast empty sets error");
	check_expected_error(inplace(-1, 1, cmp_uv, null) == 0, "inplace invalid descriptor sets error");
	check_expected_error(inplace(t, 1, null, null) == 0, "inplace null comparator sets error");
	uvlong k = inplace(t, 1, cmp_uv, null);
	CHECK(k != 0, "inplace sample knod");
	check_expected_error(inref(-1, k) == 0, "inref invalid descriptor sets error");
	check_expected_error(inknods(t, null, 1) == 0, "inknods null nonzero buffer sets error");
	check_expected_error(!infind(t, 1, null, null, null), "infind null comparator sets error");
	check_expected_error(inlower(-1, 1, cmp_uv, null) == 0, "inlower invalid descriptor sets error");
	check_expected_error(inupper(t, 1, null, null) == 0, "inupper null comparator sets error");
	check_expected_error(indels(t, 1, 0, null, null) == 0, "indels null comparator sets error");
	check_expected_error(!innext(t, 0, null), "innext rejects null knod");
	check_expected_error(!inprev(t, 0, null), "inprev rejects null knod");
	rmindeltree(t);
}

int main(void) {
	creation_order_and_duplicates();
	insertion_patterns();
	find_bounds_and_adjacency();
	enumeration_and_deletion();
	identity_deletion_cases();
	duplicate_batch_deletion_identity();
	mixed_delete_stress();
	delete_order_patterns();
	growth_and_reuse();
	invalid_inputs();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
