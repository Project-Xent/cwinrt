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

static bool keyeq(uvlong *f, char *s) { return f [0] == strlen(s) && memcmp(( void * ) f [1], s, f [0]) == 0; }

int         main(void) {
	printf("=== iterator: linear read-only ===\n");
	int seq = mkseq(0);
	atch(seq, 10);
	atch(seq, 20);
	atch(seq, 30);
	int    it = mkiter(seq);
	uvlong v  = 0;
	CHECK(it >= 0, "mkiter sequence");
	CHECK(next(it, &v) && v == 10, "sequence iterator first");
	CHECK(next(it, &v) && v == 20, "sequence iterator second");
	CHECK(next(it, &v) && v == 30, "sequence iterator third");
	CHECK(!next(it, &v), "sequence iterator exhausted");
	CHECK(!err(), "sequence iterator exhaustion is quiet");
	CHECK(slen(seq) == 3, "read-only iterator preserves sequence");
	rmiter(it);
	rmseq(seq);

	printf("\n=== iterator: queue obliterating wrap ===\n");
	int q = mkqueue(0);
	for (uvlong i = 1; i <= 64; i++) enqueue(q, i);
	bool setup_ok = true;
	for (uvlong i = 1; i <= 60; i++) {
		if (dequeue(q) != i) {
			setup_ok = false;
			break;
		}
	}
	CHECK(setup_ok, "queue wrap setup dequeue prefix");
	for (uvlong i = 65; i <= 74; i++) enqueue(q, i);
	it = mkoter(q);
	CHECK(it >= 0, "mkoter wrapped queue");
	bool ok = true;
	for (uvlong want = 61; want <= 74; want++) {
		if (!next(it, &v) || v != want) {
			ok = false;
			break;
		}
	}
	CHECK(ok, "wrapped queue obliterating iterator yields logical order");
	CHECK(!next(it, &v), "wrapped queue obliterating iterator exhausted");
	CHECK(!next(it, &v), "wrapped queue exhausted stays exhausted");
	CHECK(cize(q) == 0, "wrapped queue obliterating iterator empties queue");
	rmiter(it);
	rmqueue(q);

	printf("\n=== iterator: hash layouts ===\n");
	int set = mkset(0);
	adds(set, "aa", 2);
	it                = mkiter(set);
	uvlong fields [4] = {0};
	CHECK(next(it, fields) && keyeq(fields, "aa"), "set iterator key layout");
	CHECK(!next(it, fields), "set iterator exhausted");
	rmiter(it);
	rmset(set);

	int map = mkmap(0);
	insert(map, "k", 1, "vv", 2);
	it = mkiter(map);
	memset(fields, 0, sizeof(fields));
	CHECK(next(it, fields) && keyeq(fields, "k") && fields [2] == 2 && memcmp(( void * ) fields [3], "vv", 2) == 0,
	      "map iterator key/value layout");
	rmiter(it);
	rmmap(map);

	int ms = mkmultiset(0);
	addms(ms, "x", 1);
	addms(ms, "x", 1);
	it = mkiter(ms);
	memset(fields, 0, sizeof(fields));
	CHECK(next(it, fields) && keyeq(fields, "x") && fields [2] == 2, "multiset iterator key/count layout");
	rmiter(it);
	rmmultiset(ms);

	printf("\n=== iterator: descriptor reuse and invalidation ===\n");
	ok = true;
	for (int i = 0; i < 400; i++) {
		seq = mkseq(0);
		if (seq < 0) {
			ok = false;
			break;
		}
		atch(seq, ( uvlong ) i);
		it = mkiter(seq);
		if (it < 0) {
			ok = false;
			rmseq(seq);
			break;
		}
		if (!next(it, &v) || v != ( uvlong ) i) ok = false;
		if (next(it, &v)) ok = false;
		rmiter(it);
		rmseq(seq);
	}
	CHECK(ok, "iterator descriptors reuse under repeated create/destroy");

	seq = mkseq(0);
	atch(seq, 1);
	atch(seq, 2);
	it = mkoter(seq);
	CHECK(it >= 0, "mkoter sequence");
	CHECK(next(it, &v) && v == 1, "obliterating iterator first element");
	CHECK(slen(seq) == 1, "obliterating iterator removes yielded element");
	rmiter(it);
	CHECK(slen(seq) == 1 && pick(seq, 0) == 2, "rmiter leaves remaining elements intact");
	rmseq(seq);

	seq = mkseq(0);
	atch(seq, 10);
	atch(seq, 20);
	atch(seq, 30);
	int hold = mkiter(seq);
	CHECK(hold >= 0 && next(hold, &v) && v == 10, "ordinary iterator can hold second position");
	it = mkoter(seq);
	CHECK(it >= 0 && next(it, &v) && v == 10, "obliterating iterator removes sequence head");
	CHECK(!next(hold, &v), "ordinary iterator invalidated by other obliterator");
	CHECK(!err(), "ordinary iterator invalidation is quiet");
	rmiter(hold);
	rmiter(it);
	rmseq(seq);

	printf("\n=== iterator: mutation invalidation ===\n");
	seq = mkseq(0);
	atch(seq, 1);
	atch(seq, 2);
	it = mkiter(seq);
	CHECK(it >= 0 && next(it, &v) && v == 1, "ordinary sequence iterator starts before mutation");
	atch(seq, 3);
	CHECK(!next(it, &v), "ordinary sequence iterator invalidated by append");
	rmiter(it);
	it = mkiter(seq);
	CHECK(it >= 0, "ordinary sequence iterator recreated after mutation");
	drop(seq, 0);
	CHECK(!next(it, &v), "ordinary sequence iterator invalidated by drop");
	rmiter(it);
	it = mkiter(seq);
	rmseq(seq);
	CHECK(!next(it, &v), "ordinary sequence iterator invalidated by destroy");
	rmiter(it);

	set = mkset(0);
	adds(set, "aa", 2);
	adds(set, "bb", 2);
	it = mkiter(set);
	CHECK(it >= 0 && next(it, fields), "ordinary set iterator starts before mutation");
	adds(set, "cc", 2);
	CHECK(!next(it, fields), "ordinary set iterator invalidated by add");
	rmiter(it);
	it = mkiter(set);
	CHECK(it >= 0 && next(it, fields), "ordinary set iterator recreated after add");
	dels(set, "aa", 2);
	CHECK(!next(it, fields), "ordinary set iterator invalidated by delete");
	rmiter(it);
	it = mkiter(set);
	compact(set);
	CHECK(!next(it, fields), "ordinary set iterator invalidated by compact");
	rmiter(it);
	it = mkiter(set);
	rmset(set);
	CHECK(!next(it, fields), "ordinary set iterator invalidated by destroy");
	rmiter(it);

	map = mkmap(0);
	insert(map, "ka", 2, "va", 2);
	insert(map, "kb", 2, "vb", 2);
	it = mkiter(map);
	CHECK(it >= 0 && next(it, fields), "ordinary map iterator starts before mutation");
	insert(map, "kc", 2, "vc", 2);
	CHECK(!next(it, fields), "ordinary map iterator invalidated by insert");
	rmiter(it);
	it = mkiter(map);
	CHECK(it >= 0 && next(it, fields), "ordinary map iterator recreated after insert");
	revamp(map, "ka", 2, "vA", 2);
	CHECK(!next(it, fields), "ordinary map iterator invalidated by revamp");
	rmiter(it);
	it = mkiter(map);
	oblit(map, "kb", 2);
	CHECK(!next(it, fields), "ordinary map iterator invalidated by oblit");
	rmiter(it);
	it = mkiter(map);
	compact(map);
	CHECK(!next(it, fields), "ordinary map iterator invalidated by compact");
	rmiter(it);
	it = mkiter(map);
	rmmap(map);
	CHECK(!next(it, fields), "ordinary map iterator invalidated by destroy");
	rmiter(it);

	ms = mkmultiset(0);
	addms(ms, "mx", 2);
	addms(ms, "my", 2);
	it = mkiter(ms);
	CHECK(it >= 0 && next(it, fields), "ordinary multiset iterator starts before mutation");
	addms(ms, "mz", 2);
	CHECK(!next(it, fields), "ordinary multiset iterator invalidated by add");
	rmiter(it);
	it = mkiter(ms);
	delms(ms, "mx", 2);
	CHECK(!next(it, fields), "ordinary multiset iterator invalidated by del");
	rmiter(it);
	it = mkiter(ms);
	prgms(ms, "my", 2);
	CHECK(!next(it, fields), "ordinary multiset iterator invalidated by purge");
	rmiter(it);
	it = mkiter(ms);
	rmmultiset(ms);
	CHECK(!next(it, fields), "ordinary multiset iterator invalidated by destroy");
	rmiter(it);

	set = mkset(0);
	adds(set, "x", 1);
	adds(set, "y", 1);
	it = mkoter(set);
	CHECK(it >= 0 && next(it, fields), "obliterating set iterator yields first after self-mutation");
	CHECK(next(it, fields), "obliterating set iterator continues after own deletion");
	CHECK(!next(it, fields) && carten(set) == 0, "obliterating set iterator exhausts and empties set");
	rmiter(it);
	rmset(set);

	rmiter(-1);
	check_expected_error(mkiter(-1) < 0, "mkiter rejects invalid descriptor");
	check_expected_error(mkoter(-1) < 0, "mkoter rejects invalid descriptor");
	check_expected_error(!next(-1, &v), "invalid iterator sets error");
	seq = mkseq(0);
	it  = mkiter(seq);
	CHECK(it >= 0, "mkiter for null output error");
	check_expected_error(!next(it, null), "next null output sets error");
	rmiter(it);
	rmseq(seq);

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
