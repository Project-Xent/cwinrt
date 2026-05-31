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

static bool keys_are(int t, uvlong *want, uvlong n) {
	uvlong es [256] = {0};
	if (rxentries(t, es, arrlen(es)) != n) return false;
	for (uvlong i = 0; i < n; i++)
		if (rxkey(t, es [i]) != want [i]) return false;
	return true;
}

static bool sorted_walk_count(int t, uvlong wantn) {
	if (rxnentry(t) != wantn || rxentries(t, null, 0) != wantn) return false;
	if (!wantn) return true;
	uvlong cur = rxfirst(t);
	if (cur == ( uvlong ) -1) return false;
	uvlong prev = 0;
	for (uvlong n = 1;; n++) {
		if (n > 1) {
			if (rxkey(t, prev) > rxkey(t, cur)) return false;
			uvlong back = 0;
			if (!rxprev(t, cur, &back) || back != prev) return false;
		}
		uvlong next = 0;
		if (!rxnext(t, cur, &next)) return n == wantn && cur == rxlast(t);
		prev = cur;
		cur  = next;
	}
}

static bool keys_match_live(int t, bool *live, uvlong n) {
	uvlong es [256] = {0};
	uvlong wantn    = 0;
	for (uvlong i = 1; i <= n; i++)
		if (live [i]) wantn++;
	if (rxentries(t, es, arrlen(es)) != wantn) return false;
	uvlong at = 0;
	for (uvlong i = 1; i <= n; i++) {
		if (!live [i]) continue;
		if (at >= wantn || rxkey(t, es [at]) != i) return false;
		if (at && (!rxprev(t, es [at], null) || !rxnext(t, es [at - 1], null))) return false;
		at++;
	}
	return at == wantn && rxnentry(t) == wantn;
}

static void creation_order_duplicates(void) {
	printf("\n=== retrxtree: creation order duplicates ===\n");
	int t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree");
	uvlong e5a = rxput(t, 5, 50, cmp_uv, null);
	uvlong e1  = rxput(t, 1, 10, cmp_uv, null);
	uvlong e3  = rxput(t, 3, 30, cmp_uv, null);
	uvlong e5b = rxput(t, 5, 51, cmp_uv, null);
	uvlong e5c = rxput(t, 5, 52, cmp_uv, null);
	CHECK(e5a != ( uvlong ) -1
	        && e1 != ( uvlong ) -1
	        && e3 != ( uvlong ) -1
	        && e5b != ( uvlong ) -1
	        && e5c != ( uvlong ) -1,
	      "rxput returns entries");
	uvlong want [] = {1, 3, 5, 5, 5};
	CHECK(keys_are(t, want, arrlen(want)) && sorted_walk_count(t, arrlen(want)),
	      "rxput keeps sorted order and duplicates after equals");
	CHECK(rxkey(t, e3) == 3 && rxref(t, e5b) == 51 && rxnentry(t) == 5, "rxkey rxref rxnentry");
	rrxref(t, e5b, 510);
	CHECK(rxref(t, e5b) == 510, "rrxref updates ref");
	CHECK(rxfirst(t) == e1 && rxlast(t) == e5c, "rxfirst and rxlast");
	rmrxtree(t);
}

static void find_bounds_range(void) {
	printf("\n=== retrxtree: find bounds range ===\n");
	int t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree for range");
	uvlong ids [16];
	for (uvlong i = 0; i < arrlen(ids); i++) ids [i] = rxput(t, i / 2, i + 100, cmp_uv, null);
	CHECK(sorted_walk_count(t, arrlen(ids)), "range tree keeps sorted leaf order");
	uvlong out = 99;
	CHECK(rxfind(t, 3, cmp_uv, null, &out) && rxkey(t, out) == 3, "rxfind returns first equal");
	CHECK(rxfind(t, 3, cmp_uv, null, null), "rxfind accepts null output");
	CHECK(!rxfind(t, 99, cmp_uv, null, &out) && !err(), "rxfind miss is quiet");
	CHECK(rxlower(t, 4, cmp_uv, null) == ids [8], "rxlower returns first equal");
	CHECK(rxupper(t, 4, cmp_uv, null) == ids [10], "rxupper returns first greater");
	check_expected_error(rxlower(t, 99, cmp_uv, null) == ( uvlong ) -1, "rxlower no result sets error");
	check_expected_error(rxupper(t, 7, cmp_uv, null) == ( uvlong ) -1, "rxupper no result sets error");

	uvlong buf [16];
	for (uvlong i = 0; i < arrlen(buf); i++) buf [i] = 99;
	CHECK(rxrange(t, 2, 5, cmp_uv, null, null, 0) == 6, "rxrange counts [lo,hi)");
	CHECK(rxrange(t, 2, 5, cmp_uv, null, buf, 4) == 6 && buf [0] == ids [4] && buf [3] == ids [7] && buf [4] == 99,
	      "rxrange respects cap");
	CHECK(rxrange(t, 8, 9, cmp_uv, null, null, 0) == 0 && !err(), "rxrange empty is quiet");
	check_expected_error(rxrange(t, 0, 1, cmp_uv, null, null, 1) == 0, "rxrange bad buffer sets error");
	rmrxtree(t);
}

static void next_prev_and_delete(void) {
	printf("\n=== retrxtree: next prev delete ===\n");
	int t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree for delete");
	uvlong ids [40];
	for (uvlong i = 0; i < arrlen(ids); i++) ids [i] = rxput(t, i + 1, i, cmp_uv, null);
	CHECK(sorted_walk_count(t, arrlen(ids)), "delete tree keeps sorted leaf order after insert");
	uvlong n = 0;
	CHECK(rxnext(t, ids [0], &n) && n == ids [1], "rxnext crosses leaf order");
	CHECK(rxprev(t, ids [39], &n) && n == ids [38], "rxprev crosses leaf order");
	CHECK(!rxprev(t, ids [0], null) && !err(), "rxprev at first quiet false");
	CHECK(!rxnext(t, ids [39], null) && !err(), "rxnext at last quiet false");
	CHECK(rxdel(t, ids [10]) && sorted_walk_count(t, arrlen(ids) - 1), "rxdel deletes one entry");
	check_expected_error(rxkey(t, ids [10]) == 0, "rxkey rejects deleted entry");
	CHECK(rxkey(t, ids [11]) == 12, "neighbor remains live after delete");
	rmrxtree(t);
}

static void rxdels_cases(void) {
	printf("\n=== retrxtree: rxdels ===\n");
	int t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree for rxdels");
	uvlong before = rxput(t, 1, 1, cmp_uv, null);
	uvlong eq [6];
	for (uvlong i = 0; i < arrlen(eq); i++) eq [i] = rxput(t, 7, i, cmp_uv, null);
	uvlong after = rxput(t, 9, 9, cmp_uv, null);
	CHECK(rxdels(t, eq [2], cmp_uv, null) == 4 && sorted_walk_count(t, 4),
	      "rxdels deletes forward equal run from entry");
	CHECK(rxref(t, eq [0]) == 0 && rxref(t, eq [1]) == 1, "rxdels keeps previous equals");
	for (uvlong i = 2; i < arrlen(eq); i++) check_expected_error(rxkey(t, eq [i]) == 0, "rxdels rejects deleted entry");
	uvlong want [] = {1, 7, 7, 9};
	CHECK(keys_are(t, want, arrlen(want)) && rxfirst(t) == before && rxlast(t) == after,
	      "rxdels leaves surrounding order");
	rmrxtree(t);
}

static void duplicate_leaf_spans(void) {
	printf("\n=== retrxtree: duplicate leaf spans ===\n");

	enum
	{
		N = 180,
	};

	int t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree duplicate spans");
	uvlong ids [N];
	bool   ok = true;
	for (uvlong i = 0; i < N; i++) ids [i] = rxput(t, 42, i, cmp_uv, null);
	ok           = ok && sorted_walk_count(t, N);
	uvlong first = 0;
	ok           = ok && rxfind(t, 42, cmp_uv, null, &first) && first == ids [0];
	ok           = ok && rxrange(t, 42, 43, cmp_uv, null, null, 0) == N;
	ok           = ok && rxdels(t, ids [77], cmp_uv, null) == N - 77;
	ok           = ok && sorted_walk_count(t, 77);
	ok           = ok && rxlast(t) == ids [76];
	for (uvlong i = 77; i < N; i++) {
		ok = ok && !rxdel(t, ids [i]) && err();
		errmsg(null);
	}
	CHECK(ok, "duplicates can span leaves and delete forward run");
	rmrxtree(t);
}

static void deletion_patterns(void) {
	printf("\n=== retrxtree: deletion patterns ===\n");

	enum
	{
		N = 96,
	};

	uvlong ids [N + 1]  = {0};
	bool   live [N + 1] = {0};
	bool   ok           = true;

	int    t            = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree ascending delete");
	for (uvlong i = 1; i <= N; i++) {
		ids [i]  = rxput(t, i, i, cmp_uv, null);
		live [i] = true;
		ok       = ok && ids [i] != ( uvlong ) -1;
	}
	for (uvlong i = 1; i <= N; i++) {
		ok       = ok && rxdel(t, ids [i]);
		live [i] = false;
		ok       = ok && keys_match_live(t, live, N);
	}
	CHECK(ok && rxnentry(t) == 0, "ascending deletes rebalance to empty");
	rmrxtree(t);

	t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree descending delete");
	memset(live, 0, sizeof(live));
	ok = true;
	for (uvlong i = 1; i <= N; i++) {
		ids [i]  = rxput(t, i, i, cmp_uv, null);
		live [i] = true;
		ok       = ok && ids [i] != ( uvlong ) -1;
	}
	for (uvlong i = N; i >= 1; i--) {
		ok       = ok && rxdel(t, ids [i]);
		live [i] = false;
		ok       = ok && keys_match_live(t, live, N);
		if (i == 1) break;
	}
	CHECK(ok && rxnentry(t) == 0, "descending deletes rebalance to empty");
	rmrxtree(t);

	t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree mixed delete");
	memset(live, 0, sizeof(live));
	ok = true;
	for (uvlong i = 1; i <= N; i++) {
		ids [i]  = rxput(t, i, i, cmp_uv, null);
		live [i] = true;
	}
	for (uvlong step = 0; step < N; step++) {
		uvlong i = ((step * 37) % N) + 1;
		ok       = ok && rxdel(t, ids [i]);
		live [i] = false;
		ok       = ok && keys_match_live(t, live, N);
	}
	CHECK(ok && rxnentry(t) == 0, "mixed deletes rebalance to empty");
	rmrxtree(t);
}

static void split_heavy_public_order(void) {
	printf("\n=== retrxtree: split heavy public order ===\n");

	enum
	{
		N = 2304,
	};

	int t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree split heavy");
	uvlong ids [N];
	bool   ok = true;
	for (uvlong i = 0; i < N; i++) {
		uvlong key = (i * 7919) % N;
		ids [i]    = rxput(t, key, i, cmp_uv, null);
		ok         = ok && ids [i] != ( uvlong ) -1;
		if ((i & 63) == 63) ok = ok && sorted_walk_count(t, i + 1);
	}
	ok = ok && sorted_walk_count(t, N);
	CHECK(ok, "split-heavy insertion preserves public order");
	ok = true;
	for (uvlong i = 0; i < N; i += 3) {
		ok = ok && rxdel(t, ids [i]);
		if ((i & 255) == 0) ok = ok && sorted_walk_count(t, rxnentry(t));
	}
	for (uvlong i = 1; i < N; i += 3) {
		ok = ok && rxdel(t, ids [i]);
		if ((i & 255) == 1) ok = ok && sorted_walk_count(t, rxnentry(t));
	}
	for (uvlong i = 2; i < N; i += 3) {
		ok = ok && rxdel(t, ids [i]);
		if ((i & 255) == 2) ok = ok && sorted_walk_count(t, rxnentry(t));
	}
	CHECK(ok && sorted_walk_count(t, 0), "split-heavy deletion preserves public order");
	rmrxtree(t);
}

static void invalids_and_reuse(void) {
	printf("\n=== retrxtree: invalids and reuse ===\n");
	int t = mkrxtree(0);
	CHECK(t >= 0, "mkrxtree for invalids");
	check_expected_error(rxfirst(t) == ( uvlong ) -1, "rxfirst empty sets error");
	check_expected_error(rxlast(t) == ( uvlong ) -1, "rxlast empty sets error");
	check_expected_error(rxput(-1, 1, 1, cmp_uv, null) == ( uvlong ) -1, "rxput invalid descriptor sets error");
	check_expected_error(rxput(t, 1, 1, null, null) == ( uvlong ) -1, "rxput null comparator sets error");
	uvlong e = rxput(t, 1, 1, cmp_uv, null);
	CHECK(e != ( uvlong ) -1, "rxput sample entry");
	check_expected_error(rxref(t, 99) == 0, "rxref bad entry sets error");
	check_expected_error(!rxnext(t, 99, null), "rxnext bad entry sets error");
	check_expected_error(!rxdels(t, 99, cmp_uv, null), "rxdels bad entry sets error");
	check_expected_error(!rxfind(t, 1, null, null, null), "rxfind null comparator sets error");
	check_expected_error(rxentries(t, null, 1) == 0, "rxentries bad buffer sets error");
	rmrxtree(t);
	CHECK(rxnentry(t) == 0, "removed retrxtree count is zero");
	int reused = mkrxtree(0);
	CHECK(reused == t, "mkrxtree reuses descriptor");
	rmrxtree(reused);

	int  ids [80];
	bool ok = true;
	for (uvlong i = 0; i < arrlen(ids); i++) {
		ids [i] = mkrxtree(0);
		ok      = ok && ids [i] >= 0;
	}
	CHECK(ok, "retrxtree descriptor table grows");
	for (uvlong i = 0; i < arrlen(ids); i++) rmrxtree(ids [i]);
}

int main(void) {
	creation_order_duplicates();
	find_bounds_range();
	next_prev_and_delete();
	rxdels_cases();
	duplicate_leaf_spans();
	deletion_patterns();
	split_heavy_public_order();
	invalids_and_reuse();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
