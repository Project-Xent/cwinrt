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

static bool field_eq(uvlong *f, char *s) { return f [0] == strlen(s) && memcmp(( void * ) f [1], s, f [0]) == 0; }

int         main(void) {
	printf("=== common silo: encoded descriptor taxonomy ===\n");
	int stk = mkstack(0);
	int que = mkqueue(0);
	int seq = mkseq(0);
	int set = mkset(0);
	int map = mkmap(0);
	int ms  = mkmultiset(0);
	CHECK(stk >= 0 && que >= 0 && seq >= 0 && set >= 0 && map >= 0 && ms >= 0, "construct mixed descriptors");
	CHECK(silotype_of(stk) == silo_stack, "stack descriptor carries stack type");
	CHECK(silotype_of(que) == silo_queue, "queue descriptor carries queue type");
	CHECK(silotype_of(seq) == silo_seq, "sequence descriptor carries sequence type");
	CHECK(silotype_of(set) == silo_set, "set descriptor carries set type");
	CHECK(silotype_of(map) == silo_map, "map descriptor carries map type");
	CHECK(silotype_of(ms) == silo_multiset, "multiset descriptor carries multiset type");
	push(stk, 7);
	adds(set, "x", 1);
	CHECK(peep(stk) == 7 && mems(set, "x", 1), "linear and hash tables operate independently");
	adds(stk, "bad", 3);
	check_expected_error(true, "adds rejects wrong-family descriptor");
	push(set, 99);
	check_expected_error(true, "push rejects wrong-family descriptor");
	CHECK(peep(stk) == 7 && !mems(set, "bad", 3), "wrong-family handlers reject encoded descriptors");
	rmstack(stk);
	rmqueue(que);
	rmseq(seq);
	rmset(set);
	rmmap(map);
	rmmultiset(ms);

	printf("\n=== common silo: teem/carten/replica ===\n");
	set = mkset(0);
	adds(set, "a", 1);
	adds(set, "b", 1);
	CHECK(carten(set) == 2, "set carten before teem");
	int setcopy = replica(0, set);
	CHECK(setcopy >= 0 && silotype_of(setcopy) == silo_set, "replica set preserves type");
	CHECK(mems(setcopy, "a", 1) && mems(setcopy, "b", 1), "replica set preserves keys");
	teem(set);
	CHECK(carten(set) == 0 && !mems(set, "a", 1), "teem set clears keys");
	rmset(set);
	rmset(setcopy);

	map = mkmap(0);
	insert(map, "k", 1, "v", 1);
	int    mapcopy = replica(0, map);
	char   buf [16];
	uvlong vlen = sizeof(buf);
	CHECK(mapcopy >= 0 && silotype_of(mapcopy) == silo_map, "replica map preserves type");
	CHECK(lookup(mapcopy, "k", 1, buf, &vlen) && vlen == 1 && memcmp(buf, "v", 1) == 0, "replica map preserves value");
	rmmap(map);
	rmmap(mapcopy);

	ms = mkmultiset(0);
	addms(ms, "x", 1);
	addms(ms, "x", 1);
	addms(ms, "y", 1);
	int mscopy = replica(0, ms);
	CHECK(mscopy >= 0 && silotype_of(mscopy) == silo_multiset, "replica multiset preserves type");
	CHECK(cntms(mscopy, "x", 1) == 2 && cntms(mscopy, "y", 1) == 1, "replica multiset preserves counts");
	teem(ms);
	CHECK(carten(ms) == 0 && cntms(ms, "x", 1) == 0, "teem multiset clears counts");
	rmmultiset(ms);
	rmmultiset(mscopy);

	printf("\n=== common silo: set symmetric difference ===\n");
	int a = mkset(0);
	int b = mkset(0);
	adds(a, "a", 1);
	adds(a, "b", 1);
	adds(b, "b", 1);
	adds(b, "c", 1);
	symmdiffs(a, b);
	CHECK(mems(a, "a", 1), "symmdiffs keeps left-only");
	CHECK(!mems(a, "b", 1), "symmdiffs removes intersection");
	CHECK(mems(a, "c", 1), "symmdiffs adds right-only");
	rmset(a);
	rmset(b);

	printf("\n=== common silo: spew hash layout ===\n");
	set = mkset(0);
	adds(set, "left", 4);
	uvlong fields [2] = {0};
	CHECK(spew(set, fields, 1) == 1, "spew set takes one");
	CHECK(field_eq(fields, "left"), "spew set returns key layout");
	CHECK(carten(set) == 0, "spew set removes element");
	rmset(set);

	printf("\n=== common silo: borrowed pointer contracts ===\n");
	int seqc = mkseq(0);
	atch(seqc, 11);
	atch(seqc, 22);
	atch(seqc, 33);
	uvlong *span = null;
	CHECK(swath(seqc, 0, 1, &span) == 2 && span && span [0] == 11 && span [1] == 22,
	      "swath returns borrowed contiguous span");
	place(seqc, 1, 99);
	CHECK(pick(seqc, 1) == 99, "sequence mutation after swath is visible through silo API");
	rmseq(seqc);

	set = mkset(0);
	adds(set, "stable", 6);
	int it = mkiter(set);
	memset(fields, 0, sizeof(fields));
	CHECK(next(it, fields) && field_eq(fields, "stable"), "iterator exposes borrowed set key before mutation");
	rmiter(it);
	adds(set, "other", 5);
	CHECK(mems(set, "stable", 6) && mems(set, "other", 5), "mutating set preserves logical membership");
	compact(set);
	CHECK(mems(set, "stable", 6) && mems(set, "other", 5),
	      "compact preserves logical membership while invalidating borrowed keys");
	rmset(set);

	int pa = mkset(0);
	int pb = mkset(0);
	adds(pa, "a", 1);
	adds(pb, "b", 1);
	int prod           = cartesprod(0, pa, pb);
	it                 = mkiter(prod);
	uvlong pairkey [2] = {0};
	memset(fields, 0, sizeof(fields));
	CHECK(next(it, fields), "cartesprod yields one pair key");
	memcpy(pairkey, ( void * ) fields [1], sizeof(pairkey));
	CHECK(pairkey [0] == 1 && memcmp(( void * ) pairkey [1], "a", 1) == 0,
	      "cartesprod pair borrows left source key pointer");
	rmiter(it);
	rmset(prod);
	rmset(pa);
	rmset(pb);

	printf("\n=== common silo: dynamic descriptor tables ===\n");
	int  live [COETUA_SILO_TABLE_SEED + 20];
	bool live_ok = true;
	for (uvlong i = 0; i < arrlen(live); i++) {
		live [i] = mkstack(0);
		if (live [i] < 0) {
			live_ok = false;
			break;
		}
		push(live [i], i);
	}
	for (uvlong i = 0; i < arrlen(live); i++) {
		if (live [i] >= 0) {
			if (peep(live [i]) != i) live_ok = false;
			rmstack(live [i]);
		}
	}
	CHECK(live_ok, "linear descriptors grow past configured seed cap");

	int hds [COETUA_SILO_TABLE_SEED + 20];
	live_ok = true;
	for (uvlong i = 0; i < arrlen(hds); i++) {
		hds [i] = mkset(0);
		if (hds [i] < 0) {
			live_ok = false;
			break;
		}
		adds(hds [i], &i, sizeof(i));
	}
	for (uvlong i = 0; i < arrlen(hds); i++) {
		if (hds [i] >= 0) {
			if (!mems(hds [i], &i, sizeof(i))) live_ok = false;
			rmset(hds [i]);
		}
	}
	CHECK(live_ok, "hash descriptors grow past configured seed cap");

	int iter_src = mkseq(0);
	atch(iter_src, 7);
	int its [COETUA_ITER_TABLE_SEED + 20];
	live_ok = true;
	for (uvlong i = 0; i < arrlen(its); i++) {
		its [i] = mkiter(iter_src);
		if (its [i] < 0) {
			live_ok = false;
			break;
		}
	}
	for (uvlong i = 0; i < arrlen(its); i++)
		if (its [i] >= 0) rmiter(its [i]);
	rmseq(iter_src);
	CHECK(live_ok, "iterator descriptors grow past configured seed cap");

	printf("\n=== common silo: invalid descriptor errors ===\n");
	check_expected_error(cize(-1) == 0, "cize invalid descriptor sets error");
	check_expected_error(carten(-1) == 0, "carten invalid descriptor sets error");
	teem(-1);
	check_expected_error(true, "teem invalid descriptor sets error");
	efflate(-1, 32);
	check_expected_error(true, "efflate invalid descriptor sets error");
	tamp(-1);
	check_expected_error(true, "tamp invalid descriptor sets error");
	check_expected_error(replica(0, -1) < 0, "replica invalid descriptor fails");
	check_expected_error(silotype_of(-1) == 0, "silotype_of invalid descriptor sets error");

	printf("\n=== common silo: swop/cram/spew helpers ===\n");
	int    lin      = mkseq(0);
	uvlong batch [] = {1, 2, 3};
	cram(lin, batch, arrlen(batch));
	CHECK(slen(lin) == 3 && pick(lin, 0) == 1 && pick(lin, 2) == 3, "cram sequence appends batch");
	uvlong exchange = 9;
	swop(lin, &exchange);
	CHECK(exchange == 3 && pick(lin, 2) == 9, "swop sequence exchanges with last element");
	uvlong out [4] = {0};
	CHECK(spew(lin, out, 2) == 2 && out [0] == 1 && out [1] == 2, "spew sequence drains from front");
	CHECK(slen(lin) == 1 && pick(lin, 0) == 9, "spew sequence removes drained values");
	CHECK(spew(lin, out, 8) == 1 && !err(), "spew short linear source is quiet");
	rmseq(lin);

	set                = mkset(0);
	uvlong setbatch [] = {1, ( uvlong ) "a", 1, ( uvlong ) "b"};
	cram(set, setbatch, 2);
	CHECK(carten(set) == 2 && mems(set, "a", 1) && mems(set, "b", 1), "cram set accepts key field pairs");
	uvlong keyfields [2] = {1, ( uvlong ) "c"};
	swop(set, keyfields);
	CHECK(carten(set) == 2 && mems(set, "c", 1), "swop set inserts replacement key");
	CHECK(field_eq(keyfields, "a") || field_eq(keyfields, "b"), "swop set returns borrowed removed key");
	memset(fields, 0, sizeof(fields));
	CHECK(spew(set, fields, 1) == 1 && fields [0] == 1 && fields [1] != 0, "spew set returns one key field pair");
	CHECK(carten(set) == 1, "spew set removes one element");
	rmset(set);

	map                = mkmap(0);
	uvlong mapbatch [] = {1, ( uvlong ) "k", 1, ( uvlong ) "v"};
	cram(map, mapbatch, 1);
	vlen = sizeof(buf);
	CHECK(lookup(map, "k", 1, buf, &vlen) && vlen == 1 && memcmp(buf, "v", 1) == 0,
	      "cram map accepts key/value field quads");
	uvlong mapfields [4] = {2, ( uvlong ) "k2", 2, ( uvlong ) "v2"};
	swop(map, mapfields);
	vlen = sizeof(buf);
	CHECK(lookup(map, "k2", 2, buf, &vlen) && vlen == 2 && memcmp(buf, "v2", 2) == 0,
	      "swop map inserts replacement pair");
	CHECK(mapfields [0] == 1
	        && memcmp(( void * ) mapfields [1], "k", 1) == 0
	        && mapfields [2] == 1
	        && memcmp(( void * ) mapfields [3], "v", 1) == 0,
	      "swop map returns borrowed removed pair");
	uvlong mapout [4] = {0};
	CHECK(spew(map, mapout, 1) == 1 && mapout [0] == 2 && mapout [2] == 2, "spew map returns one key/value quad");
	CHECK(carten(map) == 0, "spew map removes returned pair");
	rmmap(map);

	printf("\n=== common silo: helper errors ===\n");
	stk = mkstack(0);
	que = mkqueue(0);
	check_expected_error(pop(stk) == 0, "pop empty stack sets error");
	check_expected_error(peep(stk) == 0, "peep empty stack sets error");
	check_expected_error(dequeue(que) == 0, "dequeue empty queue sets error");
	check_expected_error(peekq(que) == 0, "peekq empty queue sets error");
	push(-1, 1);
	check_expected_error(true, "push invalid descriptor sets error");
	enqueue(-1, 1);
	check_expected_error(true, "enqueue invalid descriptor sets error");
	swop(stk, null);
	check_expected_error(true, "swop null data sets error");
	cram(stk, null, 1);
	check_expected_error(true, "cram null nonzero data sets error");
	CHECK(!err() && spew(stk, null, 0) == 0, "spew null zero buffer is quiet");
	check_expected_error(spew(stk, null, 1) == 0, "spew null nonzero buffer sets error");
	rmstack(stk);
	rmqueue(que);

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
