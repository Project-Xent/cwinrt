#include "coetua.h"
#include <math.h>
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

static double pinf(void) {
	volatile double z = 0.0;
	return 1.0 / z;
}

static double qnan(void) {
	volatile double z = 0.0;
	return z / z;
}

static bool near_double(double a, double b) {
	double d = a - b;
	return d < 0 ? d > -1e-12 : d < 1e-12;
}

static bool has_id(uvlong *xs, uvlong n, uvlong id) {
	for (uvlong i = 0; i < n; i++)
		if (xs [i] == id) return true;
	return false;
}

static void descriptor_and_block(void) {
	printf("\n=== sparsplane: descriptor block ===\n");
	errmsg(null);
	rmsparsplane(123456);
	CHECK(!err(), "rmsparsplane bad descriptor is quiet");
	check_expected_error(spnpt(123456) == 0, "spnpt bad descriptor sets error");
	int sp = mksparsplane(0, 4.0);
	CHECK(sp >= 0, "mksparsplane creates descriptor");
	rmsparsplane(sp);
	int reused = mksparsplane(0, 8.0);
	CHECK(reused == sp, "mksparsplane reuses descriptor");
	rmsparsplane(reused);
	check_expected_error(mksparsplane(0, 0.0) < 0, "zero block rejected");
	check_expected_error(mksparsplane(0, -1.0) < 0, "negative block rejected");
	check_expected_error(mksparsplane(0, pinf()) < 0, "infinite block rejected");
	check_expected_error(mksparsplane(0, qnan()) < 0, "nan block rejected");

	int  ids [80];
	bool ok = true;
	for (uvlong i = 0; i < arrlen(ids); i++) {
		ids [i] = mksparsplane(0, 1.0);
		ok      = ok && ids [i] >= 0;
	}
	CHECK(ok, "sparsplane descriptor table grows");
	for (uvlong i = 0; i < arrlen(ids); i++) rmsparsplane(ids [i]);
}

static void points_phi_location_and_at(void) {
	printf("\n=== sparsplane: points phi location at ===\n");
	int sp = mksparsplane(0, 2.0);
	CHECK(sp >= 0, "mksparsplane for points");
	uvlong a = spput(sp, -0.0, 0.0, 10);
	uvlong b = spput(sp, pinf(), -pinf(), 20);
	CHECK(a != ( uvlong ) -1 && b != ( uvlong ) -1 && a != b, "spput creates stable points");
	CHECK(spnpt(sp) == 2 && spphi(sp, a) == 10 && spphi(sp, b) == 20, "spnpt and spphi");
	rspphi(sp, a, 11);
	CHECK(spphi(sp, a) == 11, "rspphi updates phi");
	double x = 99, y = 99;
	sploctn(sp, a, &x, &y);
	CHECK(near_double(x, 0.0) && near_double(y, 0.0), "sploctn canonicalizes negative zero");
	CHECK(spat(sp, 0.0, -0.0) == a, "spat finds canonical zero coordinate");
	CHECK(spat(sp, pinf(), -pinf()) == b, "spat finds infinite coordinate");
	check_expected_error(spat(sp, 123.0, 0.0) == ( uvlong ) -1, "spat missing point sets error");
	check_expected_error(spput(sp, qnan(), 0.0, 1) == ( uvlong ) -1, "spput rejects nan x");
	check_expected_error(spput(sp, 0.0, qnan(), 1) == ( uvlong ) -1, "spput rejects nan y");
	uvlong d0 = spput(sp, 7.0, 7.0, 70);
	uvlong d1 = spput(sp, 7.0, 7.0, 71);
	CHECK(d0 != ( uvlong ) -1 && d1 != ( uvlong ) -1, "duplicates can be inserted");
	check_expected_error(spat(sp, 7.0, 7.0) == ( uvlong ) -1, "spat duplicate coordinate sets error");
	CHECK(spmov(sp, a, 3.5, -4.5) && spat(sp, 3.5, -4.5) == a && spphi(sp, a) == 11, "spmov preserves id and phi");
	check_expected_error(!spmov(sp, a, qnan(), 1.0), "spmov rejects nan coordinate");
	CHECK(spat(sp, 3.5, -4.5) == a, "failed spmov leaves point queryable");
	CHECK(spdel(sp, b) && spnpt(sp) == 3, "spdel removes live point");
	check_expected_error(spphi(sp, b) == 0, "deleted point rejected");
	check_expected_error(!spdel(sp, b), "double delete rejected");
	rmsparsplane(sp);
}

static void soa_points(void) {
	printf("\n=== sparsplane: soa points ===\n");
	int sp = mksparsplane(0, 1.0);
	CHECK(sp >= 0, "mksparsplane for soa");
	uvlong a        = spput(sp, 1.0, 2.0, 10);
	uvlong b        = spput(sp, 3.0, 4.0, 20);
	uvlong ids [4]  = {99, 99, 99, 99};
	double xs [4]   = {99, 99, 99, 99};
	double ys [4]   = {99, 99, 99, 99};
	uvlong phis [4] = {99, 99, 99, 99};
	CHECK(sppts(sp, null, null, null, null, 0) == 2, "sppts all-null cap zero counts");
	CHECK(sppts(sp, ids, null, null, null, 1) == 2 && (ids [0] == a || ids [0] == b) && ids [1] == 99,
	      "sppts ids-only respects cap");
	CHECK(sppts(sp, null, xs, ys, phis, 4) == 2
	        && ((near_double(xs [0], 1.0) && near_double(ys [0], 2.0) && phis [0] == 10)
	            || (near_double(xs [1], 1.0) && near_double(ys [1], 2.0) && phis [1] == 10))
	        && ((near_double(xs [0], 3.0) && near_double(ys [0], 4.0) && phis [0] == 20)
	            || (near_double(xs [1], 3.0) && near_double(ys [1], 4.0) && phis [1] == 20)),
	      "sppts writes selected soa columns");
	check_expected_error(sppts(sp, null, null, null, null, 1) == 0, "sppts cap with no outputs sets error");
	CHECK(spdel(sp, a) && sppts(sp, ids, null, null, null, 4) == 1 && has_id(ids, 1, b), "sppts skips deleted points");
	rmsparsplane(sp);
}

static void box_and_circle_shape_queries(void) {
	printf("\n=== sparsplane: box circle shape queries ===\n");
	int sp = mksparsplane(0, 1.0);
	CHECK(sp >= 0, "mksparsplane for shape queries");
	uvlong a       = spput(sp, 0.0, 0.0, 10);
	uvlong b       = spput(sp, 1.0, 1.0, 20);
	uvlong c       = spput(sp, -2.0, 3.0, 30);
	uvlong d       = spput(sp, pinf(), 2.0, 40);
	uvlong e       = spput(sp, -pinf(), -pinf(), 50);
	uvlong ids [8] = {99, 99, 99, 99, 99, 99, 99, 99};
	uvlong n       = spbox(sp, -2.0, 1.0, 0.0, 3.0, ids, null, null, null, arrlen(ids));
	CHECK(n == 3 && has_id(ids, n, a) && has_id(ids, n, b) && has_id(ids, n, c), "spbox inclusive finite bounds");
	n = spbox(sp, -pinf(), pinf(), -pinf(), pinf(), ids, null, null, null, arrlen(ids));
	CHECK(n == 5 && has_id(ids, n, d) && has_id(ids, n, e), "spbox supports infinite bounds and points");
	n = spcirc(sp, 0.0, 0.0, 1.5, ids, null, null, null, arrlen(ids));
	CHECK(n == 2 && has_id(ids, n, a) && has_id(ids, n, b), "spcirc includes boundary finite points only");
	check_expected_error(spbox(sp, 2.0, 1.0, 0.0, 1.0, ids, null, null, null, arrlen(ids)) == 0,
	                     "spbox rejects reversed bounds");
	check_expected_error(spcirc(sp, pinf(), 0.0, 1.0, ids, null, null, null, arrlen(ids)) == 0,
	                     "spcirc rejects infinite center");
	CHECK(spmov(sp, c, 10.0, 10.0), "spmov reindexes point");
	n = spbox(sp, -2.0, 1.0, 0.0, 3.0, ids, null, null, null, arrlen(ids));
	CHECK(n == 2 && !has_id(ids, n, c), "spbox reflects moved point");
	CHECK(spdel(sp, b), "spdel removes from shape");
	n = spcirc(sp, 0.0, 0.0, 1.5, ids, null, null, null, arrlen(ids));
	CHECK(n == 1 && has_id(ids, n, a), "spcirc reflects deleted point");
	rmsparsplane(sp);
}

static void split_heavy_points(void) {
	printf("\n=== sparsplane: split heavy points ===\n");

	enum
	{
		N = 160,
	};

	int sp = mksparsplane(0, 0.5);
	CHECK(sp >= 0, "mksparsplane split heavy");
	uvlong ids [N];
	bool   ok = true;
	for (uvlong i = 0; i < N; i++) {
		double x = ( double ) (i % 40) * 0.5;
		double y = ( double ) (i / 40) * 0.5;
		ids [i]  = spput(sp, x, y, i + 1000);
		ok       = ok && ids [i] != ( uvlong ) -1;
	}
	ok = ok && spnpt(sp) == N;
	ok = ok && spbox(sp, 0.0, 19.5, 0.0, 1.5, null, null, null, null, 0) == N;
	for (uvlong i = 0; i < N; i += 3) ok = ok && spdel(sp, ids [i]);
	ok = ok && spnpt(sp) == N - (N + 2) / 3;
	ok = ok && spbox(sp, 0.0, 19.5, 0.0, 1.5, null, null, null, null, 0) == spnpt(sp);
	CHECK(ok, "split-heavy insert/delete stays queryable");
	rmsparsplane(sp);

	sp = mksparsplane(0, 10.0);
	CHECK(sp >= 0, "mksparsplane same-tile split case");
	ok = true;
	for (uvlong i = 0; i < 64; i++) ok = ok && spput(sp, 1.0, 1.0, i) != ( uvlong ) -1;
	uvlong far = spput(sp, 1000.0, -1000.0, 999);
	ok         = ok && far != ( uvlong ) -1 && spat(sp, 1000.0, -1000.0) == far;
	ok         = ok && spbox(sp, 999.0, 1001.0, -1001.0, -999.0, null, null, null, null, 0) == 1;
	CHECK(ok, "full same-tile bucket splits when new tile differs");
	rmsparsplane(sp);
}

static void batch_moves(void) {
	printf("\n=== sparsplane: batch moves ===\n");
	int sp = mksparsplane(0, 1.0);
	CHECK(sp >= 0, "mksparsplane batch moves");
	uvlong a = spput(sp, 0.0, 0.0, 10);
	uvlong b = spput(sp, 1.0, 0.0, 20);
	uvlong c = spput(sp, 4.0, 4.0, 30);
	uvlong d = spput(sp, pinf(), 0.0, 40);
	CHECK(spboxmove(sp, -0.5, 1.5, -0.5, 0.5, 10.0, 1.0) == 2, "spboxmove moves selected finite points");
	double x = 0, y = 0;
	sploctn(sp, a, &x, &y);
	bool ok = near_double(x, 10.0) && near_double(y, 1.0) && spphi(sp, a) == 10;
	sploctn(sp, b, &x, &y);
	ok = ok && near_double(x, 11.0) && near_double(y, 1.0) && spphi(sp, b) == 20;
	ok = ok && spbox(sp, 10.0, 11.0, 1.0, 1.0, null, null, null, null, 0) == 2;
	CHECK(ok, "spboxmove preserves ids phis and reindexes");
	CHECK(spcircmove(sp, 4.0, 4.0, 0.0, -1.0, -1.0) == 1 && spat(sp, 3.0, 3.0) == c, "spcircmove moves circle matches");
	CHECK(spboxmove(sp, pinf(), pinf(), -1.0, 1.0, 5.0, 0.0) == 1, "spboxmove can move infinite point by finite delta");
	sploctn(sp, d, &x, &y);
	CHECK(isinf(x) && x > 0 && near_double(y, 0.0), "infinite point remains infinite after finite move");
	check_expected_error(spboxmove(sp, -pinf(), pinf(), -pinf(), pinf(), pinf(), 0.0) == 0,
	                     "spboxmove rejects infinite delta");
	check_expected_error(spcircmove(sp, 0.0, 0.0, 1.0, 0.0, qnan()) == 0, "spcircmove rejects nan delta");
	rmsparsplane(sp);
}

static void nearest_queries(void) {
	printf("\n=== sparsplane: nearest ===\n");
	int sp = mksparsplane(0, 1.0);
	CHECK(sp >= 0, "mksparsplane nearest");
	uvlong inf   = spput(sp, pinf(), 0.0, 99);
	uvlong a     = spput(sp, 0.0, 0.0, 10);
	uvlong b     = spput(sp, 2.0, 0.0, 20);
	uvlong c     = spput(sp, -5.0, 0.0, 30);
	uvlong first = 99, second = 99;
	CHECK(spnear(sp, 0.25, 0.0, &first, null) == 1 && first == a, "spnear writes nearest point");
	CHECK(spnear(sp, 0.25, 0.0, &first, &second) == 2 && first == a && second == b, "spnear writes nearest two points");
	CHECK(spdel(sp, a) && spdel(sp, b) && spdel(sp, c), "remove finite points");
	check_expected_error(spnear(sp, 0.0, 0.0, &first, null) == 0, "spnear ignores infinite-only plane");
	CHECK(inf != ( uvlong ) -1 && spphi(sp, inf) == 99, "infinite point remains live");
	uvlong only = spput(sp, 4.0, 4.0, 40);
	CHECK(spnear(sp, 0.0, 0.0, &first, null) == 1 && first == only, "spnear allows optional second");
	check_expected_error(spnear(sp, 0.0, 0.0, &first, &second) == 1 && first == only,
	                     "spnear errors when requested second is absent");
	check_expected_error(spnear(sp, pinf(), 0.0, &first, null) == 0, "spnear rejects infinite query");
	check_expected_error(spnear(sp, 0.0, 0.0, null, null) == 0, "spnear requires first output");
	rmsparsplane(sp);
}

int main(void) {
	descriptor_and_block();
	points_phi_location_and_at();
	soa_points();
	box_and_circle_shape_queries();
	split_heavy_points();
	batch_moves();
	nearest_queries();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
