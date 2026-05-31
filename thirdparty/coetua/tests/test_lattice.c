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

static bool has_cell(ltcell *xs, uvlong n, vlong x, vlong y, uvlong phi) {
	for (uvlong i = 0; i < n; i++)
		if (xs [i].x == x && xs [i].y == y && xs [i].phi == phi) return true;
	return false;
}

static void creation_bounds_and_base(void) {
	printf("\n=== lattice: creation bounds and base ===\n");
	int lt = mklattice(0, -2, 1, 10, 11, 7);
	CHECK(lt >= 0, "mklattice");
	vlong xmin = 0, xmax = 0, ymin = 0, ymax = 0;
	ltbounds(lt, &xmin, &xmax, &ymin, &ymax);
	CHECK(xmin == -2 && xmax == 1 && ymin == 10 && ymax == 11, "ltbounds returns inclusive negative bounds");
	errmsg(null);
	ltbounds(lt, null, &xmax, null, &ymax);
	CHECK(!err() && xmax == 1 && ymax == 11, "ltbounds silently skips null outputs");
	CHECK(ltbase(lt) == 7, "ltbase returns constructor init");
	CHECK(ltphi(lt, -2, 10) == 7 && ltphi(lt, 1, 11) == 7, "all cells initialize to base phi");
	rmlattice(lt);
}

static void phi_set_clear_and_changed(void) {
	printf("\n=== lattice: phi set clear and changed cells ===\n");
	int lt = mklattice(0, -1, 2, -1, 1, 0);
	CHECK(lt >= 0, "mklattice for phi");
	rltphi(lt, -1, -1, 11);
	rltphi(lt, 2, 1, 22);
	rltphi(lt, 0, 0, 33);
	CHECK(ltphi(lt, -1, -1) == 11 && ltphi(lt, 2, 1) == 22 && ltphi(lt, 0, 0) == 33,
	      "rltphi updates non-square lattice cells");
	ltcell cells [4] = {{0}};
	CHECK(ltphis(lt, null, 0) == 3, "ltphis counts changed cells without buffer");
	CHECK(ltphis(lt, cells, 2) == 3, "ltphis returns full count with cap");
	uvlong n = ltphis(lt, cells, arrlen(cells));
	CHECK(n == 3 && has_cell(cells, n, -1, -1, 11) && has_cell(cells, n, 2, 1, 22) && has_cell(cells, n, 0, 0, 33),
	      "ltphis enumerates changed cells with coordinates and phi");
	rltphi(lt, 0, 0, 0);
	n = ltphis(lt, cells, arrlen(cells));
	CHECK(n == 2 && !has_cell(cells, n, 0, 0, 33), "ltphis excludes cells reset to base");
	ltclear(lt);
	CHECK(ltphi(lt, -1, -1) == 0 && ltphi(lt, 2, 1) == 0 && ltphis(lt, null, 0) == 0,
	      "ltclear resets all cells to base");
	rmlattice(lt);
}

static void neighbors(void) {
	printf("\n=== lattice: orthogonal and surrounding refs ===\n");
	int lt = mklattice(0, -1, 1, -1, 1, 0);
	CHECK(lt >= 0, "mklattice for neighbors");
	for (vlong y = -1; y <= 1; y++)
		for (vlong x = -1; x <= 1; x++) rltphi(lt, x, y, ( uvlong ) ((y + 1) * 3 + (x + 1) + 1));
	uvlong buf [8] = {99, 99, 99, 99, 99, 99, 99, 99};
	CHECK(ltorth(lt, 0, 0, buf, arrlen(buf)) == 4 && buf [0] == 6 && buf [1] == 8 && buf [2] == 4 && buf [3] == 2,
	      "ltorth returns E N W S");
	CHECK(ltsurr(lt, 0, 0, buf, arrlen(buf)) == 8
	        && buf [0] == 6
	        && buf [1] == 9
	        && buf [2] == 8
	        && buf [3] == 7
	        && buf [4] == 4
	        && buf [5] == 1
	        && buf [6] == 2
	        && buf [7] == 3,
	      "ltsurr returns counterclockwise neighbors from east");
	CHECK(ltorth(lt, 1, 1, buf, arrlen(buf)) == 2 && buf [0] == 8 && buf [1] == 6, "ltorth clips boundary");
	CHECK(ltsurr(lt, 1, 1, buf, arrlen(buf)) == 3 && buf [0] == 8 && buf [1] == 5 && buf [2] == 6,
	      "ltsurr clips boundary in order");
	CHECK(ltorth(lt, 0, 0, null, 0) == 4, "ltorth counts without buffer");
	CHECK(ltsurr(lt, 0, 0, buf, 3) == 8, "ltsurr returns full count with cap");
	rmlattice(lt);
}

static void thin_and_tall_rectangles(void) {
	printf("\n=== lattice: thin clipped Morton round trips ===\n");
	int lt = mklattice(0, 5, 5, -3, 4, 100);
	CHECK(lt >= 0, "mklattice thin vertical");
	for (vlong y = -3; y <= 4; y++) rltphi(lt, 5, y, ( uvlong ) (y + 20));
	bool ok = true;
	for (vlong y = -3; y <= 4; y++) ok = ok && ltphi(lt, 5, y) == ( uvlong ) (y + 20);
	CHECK(ok, "thin vertical lattice reads back all phi values");
	CHECK(ltphis(lt, null, 0) == 8, "thin vertical ltphis sees all changed cells");
	rmlattice(lt);
	lt = mklattice(0, -4, 3, 2, 2, 200);
	CHECK(lt >= 0, "mklattice thin horizontal");
	for (vlong x = -4; x <= 3; x++) rltphi(lt, x, 2, ( uvlong ) (x + 50));
	ok = true;
	for (vlong x = -4; x <= 3; x++) ok = ok && ltphi(lt, x, 2) == ( uvlong ) (x + 50);
	CHECK(ok, "thin horizontal lattice reads back all phi values");
	CHECK(ltphis(lt, null, 0) == 8, "thin horizontal ltphis sees all changed cells");
	rmlattice(lt);
}

static void invalid_inputs(void) {
	printf("\n=== lattice: invalid inputs ===\n");
	CHECK(mklattice(0, 1, 0, 0, 1, 0) == -1, "mklattice rejects empty x range");
	check_expected_error(true, "bad x range sets error");
	CHECK(mklattice(0, 0, 1, 2, 1, 0) == -1, "mklattice rejects empty y range");
	check_expected_error(true, "bad y range sets error");
	int lt = mklattice(0, 0, 1, 0, 1, 0);
	CHECK(lt >= 0, "mklattice for invalids");
	CHECK(ltbase(-1) == 0, "ltbase rejects invalid lattice");
	check_expected_error(true, "ltbase invalid lattice sets error");
	CHECK(ltphi(lt, 2, 0) == 0, "ltphi rejects out of bounds");
	check_expected_error(true, "ltphi out of bounds sets error");
	rltphi(lt, 0, 2, 1);
	check_expected_error(true, "rltphi out of bounds sets error");
	ltbounds(-1, null, null, null, null);
	check_expected_error(true, "ltbounds invalid lattice sets error");
	CHECK(ltorth(lt, 0, 0, null, 1) == 0, "ltorth rejects null buffer with cap");
	check_expected_error(true, "ltorth null buffer sets error");
	CHECK(ltsurr(lt, 9, 9, null, 0) == 0, "ltsurr rejects bad coordinate");
	check_expected_error(true, "ltsurr bad coordinate sets error");
	CHECK(ltphis(lt, null, 1) == 0, "ltphis rejects null buffer with cap");
	check_expected_error(true, "ltphis null buffer sets error");
	rmlattice(lt);
}

static void growth_and_reuse(void) {
	printf("\n=== lattice: descriptor growth and reuse ===\n");
	int  ids [80];
	bool ok = true;
	for (uint i = 0; i < arrlen(ids); i++) {
		ids [i] = mklattice(0, 0, 0, 0, 0, i);
		ok      = ok && ids [i] >= 0 && ltbase(ids [i]) == i;
	}
	CHECK(ok, "mklattice grows descriptor table");
	int first = ids [0];
	for (uint i = 0; i < arrlen(ids); i++) rmlattice(ids [i]);
	CHECK(ltbase(first) == 0, "removed lattice base query returns zero");
	check_expected_error(true, "removed lattice base query sets error");
	int reused = mklattice(0, 0, 0, 0, 0, 99);
	CHECK(reused == first, "mklattice reuses removed descriptor");
	rmlattice(reused);
}

int main(void) {
	creation_bounds_and_base();
	phi_set_clear_and_changed();
	neighbors();
	thin_and_tall_rectangles();
	invalid_inputs();
	growth_and_reuse();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
