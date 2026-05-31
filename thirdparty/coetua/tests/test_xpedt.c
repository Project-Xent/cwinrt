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

static int seq_of(uvlong *xs, uvlong n) {
	int s = mkseq(0);
	for (uvlong i = 0; i < n; i++) atch(s, xs [i]);
	return s;
}

static void mul(pair sd, void *arg) {
	uvlong k             = *( uvlong * ) arg;
	*( uvlong * ) sd.b.x = *( uvlong * ) sd.a.x * k;
}

static bool over(arrst i, void *arg) { return *( uvlong * ) i.x > *( uvlong * ) arg; }

static bool under(arrst i, void *arg) { return *( uvlong * ) i.x < *( uvlong * ) arg; }

static void to_pair(pair sd, void *arg) {
	( void ) arg;
	uvlong u = *( uvlong * ) sd.a.x;
	pair  *p = ( pair * ) sd.b.x;
	p->a     = mkarrst(sizeof(u), sd.a.x);
	p->b     = mkarrst(sizeof(u), sd.a.x);
}

static void to_mod_pair(pair sd, void *arg) {
	uvlong  mod = *( uvlong * ) arg;
	uvlong  u   = *( uvlong * ) sd.a.x;
	uvlong *k   = ( uvlong * ) den(sizeof(*k));
	pair   *p   = ( pair * ) sd.b.x;
	*k          = u % mod;
	p->a        = mkarrst(sizeof(*k), k);
	p->b        = mkarrst(sizeof(u), sd.a.x);
}

static int mod_class(arrst i, void *arg) {
	uvlong mod = *( uvlong * ) arg;
	return ( int ) (*( uvlong * ) i.x % mod);
}

static void sum_resolve(arrst k, arrst va, arrst vb, arrst rv, void *arg) {
	( void ) k;
	( void ) arg;
	*( uvlong * ) rv.x = *( uvlong * ) va.x + *( uvlong * ) vb.x;
}

static void add_zip(arrst a, arrst b, arrst dst, void *arg) {
	uvlong bias         = *( uvlong * ) arg;
	*( uvlong * ) dst.x = *( uvlong * ) a.x + *( uvlong * ) b.x + bias;
}

static void sum_red(arrst acc, arrst i, arrst dst, void *arg) {
	uvlong bias         = *( uvlong * ) arg;
	*( uvlong * ) dst.x = *( uvlong * ) acc.x + *( uvlong * ) i.x + bias;
}

static void count_elem(xprod *out, void *arg) {
	uvlong *sum  = ( uvlong * ) arg;
	*sum        += out->u;
}

static void count_arrst_len(xprod *out, void *arg) {
	uvlong *sum  = ( uvlong * ) arg;
	*sum        += out->a.len;
}

static void save_result(xprod *out, void *arg) { *( int * ) arg = out->d; }

static int  cmp_uvlong(void *a, void *b, void *arg) {
	int    dir = arg ? *( int * ) arg : 1;
	uvlong x   = *( uvlong * ) a;
	uvlong y   = *( uvlong * ) b;
	return dir * ((x > y) - (x < y));
}

static void yield_neighbors(arrst i, void (*yield)(arrst), void *arg) {
	uvlong step = *( uvlong * ) arg;
	uvlong u    = *( uvlong * ) i.x;
	yield(mkarrst(sizeof(u), &u));
	u += step;
	yield(mkarrst(sizeof(u), &u));
}

static void basic_map_filter_mold(void) {
	printf("\n=== xpedt: map filter mold ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt");
	uvlong factor = 3;
	uvlong min    = 8;
	int    a      = fmap(xp, 1, xrtuvlong, mul, &factor);
	int    b      = filt(xp, a, over, &min);
	int    c      = mold(xp, b, silo_seq);
	CHECK(a < 0 && b < 0 && c < 0, "xfunctions return negative sources");
	uvlong xs [] = {1, 2, 3, 4};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(out.d >= 0 && silotype_of(out.d) == silo_seq, "xact molds current leaf to sequence");
	CHECK(slen(out.d) == 2 && pick(out.d, 0) == 9 && pick(out.d, 1) == 12, "map/filter result values");
	rmxpedt(xp);
}

static void positive_binding_order(void) {
	printf("\n=== xpedt: positive binding order ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for binding order");
	uvlong factor = 10;
	int    a      = fmap(xp, 7, xrtuvlong, mul, &factor);
	int    b      = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "positive source can be sparse");
	uvlong xs [] = {2, 5};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 2 && pick(out.d, 0) == 20 && pick(out.d, 1) == 50,
	      "single sparse positive source binds from varargs");
	rmxpedt(xp);
}

static void pair_to_map(void) {
	printf("\n=== xpedt: pair source molds to map ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for pair map");
	int a = fmap(xp, 1, xrtpair, to_pair, null);
	int b = mold(xp, a, silo_map);
	CHECK(a < 0 && b < 0, "pair-producing fmap and mold");
	uvlong xs [] = {4, 9};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	uvlong got = 0;
	uvlong len = sizeof(got);
	CHECK(lookup(out.d, &xs [0], sizeof(xs [0]), &got, &len) && len == sizeof(got) && got == 4,
	      "mold map stores first pair");
	got = 0;
	len = sizeof(got);
	CHECK(lookup(out.d, &xs [1], sizeof(xs [1]), &got, &len) && len == sizeof(got) && got == 9,
	      "mold map stores second pair");
	rmxpedt(xp);
}

static void take_drop_mold(void) {
	printf("\n=== xpedt: take drop mold ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for take/drop");
	int a = xdrop(xp, 1, 1);
	int b = xtake(xp, a, 3);
	int c = mold(xp, b, silo_seq);
	CHECK(a < 0 && b < 0 && c < 0, "xtake/xdrop return negative sources");
	uvlong xs [] = {10, 20, 30, 40, 50};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 3 && pick(out.d, 0) == 20 && pick(out.d, 1) == 30 && pick(out.d, 2) == 40,
	      "xdrop then xtake preserves order and type");
	rmxpedt(xp);
}

static void take_drop_edges(void) {
	printf("\n=== xpedt: take drop edges ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for take/drop edges");
	mold(xp, xdrop(xp, xtake(xp, 1, 99), 99), silo_seq);
	uvlong xs [] = {1, 2, 3};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 0, "dropping past end yields empty stream");
	rmxpedt(xp);
}

static void query_scalars(void) {
	printf("\n=== xpedt: any all scalars ===\n");
	uvlong xs [] = {2, 4, 6};
	int    in    = seq_of(xs, arrlen(xs));
	uvlong limit = 5;
	xprod  out;

	int    xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xany");
	CHECK(xany(xp, 1, over, &limit) < 0, "xany returns negative source");
	xact(xp, &out, (xprod) {.d = in});
	CHECK(out.u == 1, "xany returns true as uvlong");
	rmxpedt(xp);

	xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xall");
	CHECK(xall(xp, 1, under, &limit) < 0, "xall returns negative source");
	xact(xp, &out, (xprod) {.d = in});
	CHECK(out.u == 0, "xall returns false as uvlong");
	rmxpedt(xp);
}

static void query_find(void) {
	printf("\n=== xpedt: find ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xfind");
	uvlong limit = 25;
	int    a     = xfind(xp, xdrop(xp, 1, 1), over, &limit);
	CHECK(a < 0, "xfind returns negative source");
	uvlong xs [] = {10, 20, 30, 40};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(out.u == 30, "xfind returns first matching element");
	rmxpedt(xp);
}

static void query_find_none(void) {
	printf("\n=== xpedt: find none ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xfind none");
	uvlong limit = 99;
	xfind(xp, 1, over, &limit);
	uvlong xs [] = {1, 2, 3};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out   = {.u = 777};
	xact(xp, &out, (xprod) {.d = in});
	CHECK(out.u == 0, "xfind without match returns zeroed product");
	rmxpedt(xp);
}

static void binary_catena_ascending_bind(void) {
	printf("\n=== xpedt: catena ascending bind ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for catena");
	int a = catena(xp, 9, 2);
	int b = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "catena returns negative source");
	uvlong low []  = {1, 2};
	uvlong high [] = {9, 10};
	int    slow    = seq_of(low, arrlen(low));
	int    shigh   = seq_of(high, arrlen(high));
	xprod  out;
	xact(xp, &out, (xprod) {.d = slow}, (xprod) {.d = shigh});
	CHECK(slen(out.d) == 4 && pick(out.d, 0) == 9 && pick(out.d, 1) == 10 && pick(out.d, 2) == 1 && pick(out.d, 3) == 2,
	      "bindings follow ascending positive sd, not dependency order");
	rmxpedt(xp);
}

static void binary_immix(void) {
	printf("\n=== xpedt: immix ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for immix");
	mold(xp, ximmix(xp, 1, 2), silo_seq);
	uvlong a [] = {1, 3, 5};
	uvlong b [] = {2, 4};
	int    sa   = seq_of(a, arrlen(a));
	int    sb   = seq_of(b, arrlen(b));
	xprod  out;
	xact(xp, &out, (xprod) {.d = sa}, (xprod) {.d = sb});
	CHECK(slen(out.d) == 5
	        && pick(out.d, 0) == 1
	        && pick(out.d, 1) == 2
	        && pick(out.d, 2) == 3
	        && pick(out.d, 3) == 4
	        && pick(out.d, 4) == 5,
	      "ximmix interleaves and appends tail");
	rmxpedt(xp);
}

static void binary_connex(void) {
	printf("\n=== xpedt: connex ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for connex");
	uvlong bias = 100;
	int    a    = connex(xp, 5, 1, xrtuvlong, add_zip, &bias);
	int    b    = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "connex returns negative source");
	uvlong one []  = {1, 2, 3, 4};
	uvlong five [] = {10, 20};
	int    sone    = seq_of(one, arrlen(one));
	int    sfive   = seq_of(five, arrlen(five));
	xprod  out;
	xact(xp, &out, (xprod) {.d = sone}, (xprod) {.d = sfive});
	CHECK(slen(out.d) == 2 && pick(out.d, 0) == 111 && pick(out.d, 1) == 122,
	      "connex zips to shorter source and binds positives ascending");
	rmxpedt(xp);
}

static void sort_values(void) {
	printf("\n=== xpedt: sort ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for sort");
	int dir = 1;
	int a   = sort(xp, 1, cmp_uvlong, &dir);
	int b   = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "sort returns negative source");
	uvlong xs [] = {5, 1, 3, 1, 4};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 5
	        && pick(out.d, 0) == 1
	        && pick(out.d, 1) == 1
	        && pick(out.d, 2) == 3
	        && pick(out.d, 3) == 4
	        && pick(out.d, 4) == 5,
	      "sort orders source with comparator");
	rmxpedt(xp);
}

static void merge_values(void) {
	printf("\n=== xpedt: merge ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for merge");
	int dir = 1;
	int a   = xmerge(xp, 7, 2, cmp_uvlong, &dir);
	int b   = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "xmerge returns negative source");
	uvlong low []  = {1, 3, 8};
	uvlong high [] = {2, 3, 4, 9};
	int    slow    = seq_of(low, arrlen(low));
	int    shigh   = seq_of(high, arrlen(high));
	xprod  out;
	xact(xp, &out, (xprod) {.d = shigh}, (xprod) {.d = slow});
	CHECK(slen(out.d) == 7
	        && pick(out.d, 0) == 1
	        && pick(out.d, 1) == 2
	        && pick(out.d, 2) == 3
	        && pick(out.d, 3) == 3
	        && pick(out.d, 4) == 4
	        && pick(out.d, 5) == 8
	        && pick(out.d, 6) == 9,
	      "xmerge merges sorted sources and binds positives ascending");
	rmxpedt(xp);
}

static void recur_take_values(void) {
	printf("\n=== xpedt: recur take ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xrecur");
	int a = xtake(xp, xrecur(xp, 1), 7);
	int b = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "xrecur can be bounded by xtake");
	uvlong xs [] = {4, 5, 6};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 7
	        && pick(out.d, 0) == 4
	        && pick(out.d, 1) == 5
	        && pick(out.d, 2) == 6
	        && pick(out.d, 3) == 4
	        && pick(out.d, 4) == 5
	        && pick(out.d, 5) == 6
	        && pick(out.d, 6) == 4,
	      "xtake from xrecur repeats source in order");
	rmxpedt(xp);
}

static void aggroup_values(void) {
	printf("\n=== xpedt: aggroup ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for aggroup");
	uvlong mod = 2;
	int    a   = aggroup(xp, 1, mod_class, &mod);
	int    b   = mold(xp, a, silo_map);
	CHECK(a < 0 && b < 0, "aggroup returns pair source");
	uvlong xs [] = {10, 21};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	uvlong key = 0;
	uvlong got = 0;
	uvlong len = sizeof(got);
	CHECK(lookup(out.d, &key, sizeof(key), &got, &len) && got == 10, "aggroup stores class zero member");
	key = 1;
	got = 0;
	len = sizeof(got);
	CHECK(lookup(out.d, &key, sizeof(key), &got, &len) && got == 21, "aggroup stores class one member");
	rmxpedt(xp);
}

static void uniq_pairs(void) {
	printf("\n=== xpedt: uniq ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xuniq");
	uvlong mod = 2;
	int    a   = fmap(xp, 1, xrtpair, to_mod_pair, &mod);
	int    b   = xuniq(xp, a, sum_resolve, null);
	int    c   = mold(xp, b, silo_map);
	CHECK(a < 0 && b < 0 && c < 0, "xuniq resolves duplicate pair keys");
	uvlong xs [] = {2, 4, 3};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	uvlong key = 0;
	uvlong got = 0;
	uvlong len = sizeof(got);
	CHECK(lookup(out.d, &key, sizeof(key), &got, &len) && got == 6, "xuniq resolver combines duplicate key values");
	key = 1;
	got = 0;
	len = sizeof(got);
	CHECK(lookup(out.d, &key, sizeof(key), &got, &len) && got == 3, "xuniq preserves unique key values");
	rmxpedt(xp);
}

static void orama_windows(void) {
	printf("\n=== xpedt: orama ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xorama");
	int a = xorama(xp, 1, 3, 2);
	CHECK(a < 0, "xorama returns negative source");
	uvlong xs [] = {10, 20, 30, 40, 50};
	int    in    = seq_of(xs, arrlen(xs));
	uvlong got   = 0;
	xcalleach(xp, count_arrst_len, &got, (xprod) {.d = in});
	xrun(xp);
	CHECK(got == sizeof(uvlong) * 6, "xorama yields two window arrsts of three uvlongs");
	rmxpedt(xp);
}

static void fold_pare(void) {
	printf("\n=== xpedt: pare ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for pare");
	uvlong init = 10;
	uvlong bias = 1;
	int    a    = pare(xp, 1, xrtuvlong, &init, sum_red, &bias);
	CHECK(a < 0, "pare returns negative source");
	uvlong xs [] = {2, 3, 4};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(out.u == 22, "pare returns final accumulator");
	rmxpedt(xp);
}

static void fold_peruse(void) {
	printf("\n=== xpedt: peruse ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for peruse");
	uvlong init = 0;
	uvlong bias = 0;
	int    a    = peruse(xp, 1, xrtuvlong, &init, sum_red, &bias);
	int    b    = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "peruse returns intermediate source");
	uvlong xs [] = {2, 3, 4};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 3 && pick(out.d, 0) == 2 && pick(out.d, 1) == 5 && pick(out.d, 2) == 9,
	      "peruse yields all intermediate accumulators");
	rmxpedt(xp);
}

static void diffuse_values(void) {
	printf("\n=== xpedt: diffuse ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for diffuse");
	int a = diffuse(xp, xdrop(xp, 1, 1), xrtuvlong);
	int b = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "diffuse returns negative source");
	uvlong xs [] = {7, 8, 9, 10};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 3 && pick(out.d, 0) == 8 && pick(out.d, 1) == 8 && pick(out.d, 2) == 8,
	      "diffuse broadcasts first remaining value across source length");
	rmxpedt(xp);
}

static void diffuse_empty(void) {
	printf("\n=== xpedt: diffuse empty ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for diffuse empty");
	mold(xp, diffuse(xp, xdrop(xp, 1, 99), xrtuvlong), silo_seq);
	uvlong xs [] = {1, 2};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 0, "diffuse over empty source stays empty");
	rmxpedt(xp);
}

static void flatten_silos(void) {
	printf("\n=== xpedt: flatten ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for flatten");
	int a = flatten(xp, 1);
	int b = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "flatten returns negative source");
	uvlong xs [] = {1, 2};
	uvlong ys [] = {3, 4, 5};
	int    sx    = seq_of(xs, arrlen(xs));
	int    sy    = seq_of(ys, arrlen(ys));
	uvlong ds [] = {( uvlong ) sx, ( uvlong ) sy};
	int    in    = seq_of(ds, arrlen(ds));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(slen(out.d) == 5
	        && pick(out.d, 0) == 1
	        && pick(out.d, 1) == 2
	        && pick(out.d, 2) == 3
	        && pick(out.d, 3) == 4
	        && pick(out.d, 4) == 5,
	      "flatten concatenates source silos");
	rmxpedt(xp);
}

static void flatmap_values(void) {
	printf("\n=== xpedt: flatmap ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for flatmap");
	uvlong step = 10;
	int    a    = flatmap(xp, 1, xrtuvlong, yield_neighbors, &step);
	int    b    = mold(xp, a, silo_seq);
	CHECK(a < 0 && b < 0, "flatmap returns negative source");
	uvlong xs [] = {2, 5};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out;
	xact(xp, &out, (xprod) {.d = in});
	CHECK(
	  slen(out.d) == 4 && pick(out.d, 0) == 2 && pick(out.d, 1) == 12 && pick(out.d, 2) == 5 && pick(out.d, 3) == 15,
	  "flatmap appends every yielded value");
	rmxpedt(xp);
}

static void registered_jobs(void) {
	printf("\n=== xpedt: registered jobs ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xpass/xrun");
	uvlong factor = 2;
	mold(xp, fmap(xp, 1, xrtuvlong, mul, &factor), silo_seq);
	uvlong xs [] = {3, 6};
	int    in    = seq_of(xs, arrlen(xs));
	xprod  out   = {0};
	xpass(xp, &out, (xprod) {.d = in});
	CHECK(out.d == 0, "xpass does not run immediately");
	xrun(xp);
	CHECK(out.d >= 0 && slen(out.d) == 2 && pick(out.d, 0) == 6 && pick(out.d, 1) == 12,
	      "xrun executes registered pass job");
	rmxpedt(xp);
}

static void registered_call(void) {
	printf("\n=== xpedt: registered call ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xcall/xrun");
	uvlong factor = 5;
	mold(xp, fmap(xp, 1, xrtuvlong, mul, &factor), silo_seq);
	uvlong xs [] = {2, 4};
	int    in    = seq_of(xs, arrlen(xs));
	int    got   = 0;
	xcall(xp, save_result, &got, (xprod) {.d = in});
	CHECK(got == 0, "xcall does not run immediately");
	xrun(xp);
	CHECK(got >= 0 && slen(got) == 2 && pick(got, 0) == 10 && pick(got, 1) == 20,
	      "xcall invokes callback once with result");
	rmxpedt(xp);
}

static void registered_calleach(void) {
	printf("\n=== xpedt: registered calleach ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for xcalleach/xrun");
	uvlong factor = 2;
	fmap(xp, 1, xrtuvlong, mul, &factor);
	uvlong xs [] = {1, 4, 7};
	int    in    = seq_of(xs, arrlen(xs));
	uvlong sum   = 0;
	xcalleach(xp, count_elem, &sum, (xprod) {.d = in});
	CHECK(sum == 0, "xcalleach does not run immediately");
	xrun(xp);
	CHECK(sum == 24, "xcalleach invokes callback for each element");
	rmxpedt(xp);
}

static void invalid_inputs(void) {
	printf("\n=== xpedt: invalid inputs ===\n");
	int xp = mkxpedt(0);
	CHECK(xp >= 0, "mkxpedt for invalids");
	uvlong factor = 2;
	CHECK(fmap(xp, 0, xrtuvlong, mul, &factor) == 0, "fmap rejects zero source");
	check_expected_error(true, "fmap zero source sets error");
	CHECK(fmap(xp, 1, xrtuvlong, null, null) == 0, "fmap rejects null function");
	check_expected_error(true, "fmap null fn sets error");
	int a = fmap(xp, 1, xrtuvlong, mul, &factor);
	mold(xp, a, silo_seq);
	xcall(xp, null, null, (xprod) {.d = seq_of(&factor, 1)});
	check_expected_error(true, "xcall rejects null callback");
	xprod out = {0};
	xact(xp, &out, (xprod) {.d = -123});
	check_expected_error(true, "xact rejects bad input silo descriptor");
	xrun(-1);
	check_expected_error(true, "xrun bad descriptor sets error");
	rmxpedt(xp);
}

int main(void) {
	basic_map_filter_mold();
	positive_binding_order();
	pair_to_map();
	take_drop_mold();
	take_drop_edges();
	query_scalars();
	query_find();
	query_find_none();
	binary_catena_ascending_bind();
	binary_immix();
	binary_connex();
	sort_values();
	merge_values();
	recur_take_values();
	aggroup_values();
	uniq_pairs();
	orama_windows();
	fold_pare();
	fold_peruse();
	diffuse_values();
	diffuse_empty();
	flatten_silos();
	flatmap_values();
	registered_jobs();
	registered_call();
	registered_calleach();
	invalid_inputs();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
