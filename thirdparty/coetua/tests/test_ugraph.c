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

static bool has_uv(uvlong *xs, uvlong n, uvlong x) {
	for (uvlong i = 0; i < n; i++)
		if (xs [i] == x) return true;
	return false;
}

static uvlong count_uv(uvlong *xs, uvlong n, uvlong x) {
	uvlong c = 0;
	for (uvlong i = 0; i < n; i++)
		if (xs [i] == x) c++;
	return c;
}

static void dot_refs_and_all_enum(void) {
	printf("\n=== ugraph: dot refs and all-dot enum ===\n");
	int g = mkugraph(0);
	CHECK(g >= 0, "mkugraph");
	uvlong a = ugdot(g, 100);
	uvlong b = ugdot(g, 200);
	uvlong c = ugdot(g, 300);
	CHECK(a != ( uvlong ) -1 && b != ( uvlong ) -1 && c != ( uvlong ) -1, "ugdot creates opaque ids");
	CHECK(ugndot(g) == 3, "ugndot counts live dots");
	CHECK(ugdotref(g, b) == 200, "ugdotref returns dot ref");
	rugdotref(g, b, 250);
	CHECK(ugdotref(g, b) == 250, "rugdotref updates dot ref");
	uvlong buf [4] = {99, 99, 99, 99};
	CHECK(ugdots(g, null, 0) == 3, "ugdots counts without buffer");
	CHECK(ugdots(g, buf, 2) == 3, "ugdots returns full count with cap");
	CHECK(buf [2] == 99 && buf [3] == 99, "ugdots respects cap");
	CHECK(ugdeldot(g, b) == 0, "ugdeldot removes isolated dot");
	CHECK(ugndot(g) == 2, "ugndot excludes deleted dot");
	uvlong n = ugdots(g, buf, 4);
	CHECK(n == 2 && has_uv(buf, n, a) && has_uv(buf, n, c) && !has_uv(buf, n, b), "ugdots skips deleted dots");
	check_expected_error(ugdotref(g, b) == 0, "dead dot ref is an error");
	rmugraph(g);
}

static void arc_refs_parallel_and_self_loop(void) {
	printf("\n=== ugraph: arcs parallel and self-loop ===\n");
	int g = mkugraph(0);
	CHECK(g >= 0, "mkugraph for arcs");
	uvlong a   = ugdot(g, 1);
	uvlong b   = ugdot(g, 2);
	uvlong ab0 = ugarc(g, a, b, 10);
	uvlong ab1 = ugarc(g, b, a, 20);
	uvlong aa  = ugarc(g, a, a, 30);
	CHECK(ab0 != ab1 && ab1 != aa, "ugarc always creates a new arc");
	CHECK(ugnarc(g) == 3, "ugnarc counts live arcs");
	CHECK(uglinked(g, a, b) == 2, "uglinked returns parallel arc count");
	CHECK(uglinked(g, a, a) == 1, "uglinked counts self-loop arcs");
	CHECK(ugdeg(g, a) == 4, "ugdeg counts self-loop twice");
	CHECK(ugarcref(g, ab1) == 20, "ugarcref returns arc ref");
	rugarcref(g, ab1, 25);
	CHECK(ugarcref(g, ab1) == 25, "rugarcref updates arc ref");
	uvlong ea = 99, eb = 99;
	CHECK(ugends(g, ab0, &ea, &eb) && ea == a && eb == b, "ugends returns arc endpoints");
	uvlong dots [6] = {99, 99, 99, 99, 99, 99};
	uvlong nd       = ugadots(g, a, dots, arrlen(dots));
	CHECK(nd == 4 && count_uv(dots, nd, b) == 2 && count_uv(dots, nd, a) == 2,
	      "ugadots preserves multiplicity and self-loop appears twice");
	uvlong arcs [4] = {99, 99, 99, 99};
	uvlong na       = ugaarcs(g, a, arcs, arrlen(arcs));
	CHECK(na == 3 && has_uv(arcs, na, ab0) && has_uv(arcs, na, ab1) && has_uv(arcs, na, aa),
	      "ugaarcs enumerates incident arcs once");
	CHECK(ugarcs(g, null, 0) == 3, "ugarcs counts all live arcs");
	rmugraph(g);
}

static void deletion_cascades_and_tombstones(void) {
	printf("\n=== ugraph: deletion cascades and tombstones ===\n");
	int g = mkugraph(0);
	CHECK(g >= 0, "mkugraph for deletion");
	uvlong a   = ugdot(g, 1);
	uvlong b   = ugdot(g, 2);
	uvlong c   = ugdot(g, 3);
	uvlong ab0 = ugarc(g, a, b, 10);
	uvlong ab1 = ugarc(g, a, b, 20);
	uvlong ac  = ugarc(g, a, c, 30);
	uvlong bc  = ugarc(g, b, c, 40);
	CHECK(ugdelarc(g, ab0), "ugdelarc deletes one arc");
	CHECK(ugnarc(g) == 3 && uglinked(g, a, b) == 1, "ugdelarc updates live arc state");
	check_expected_error(!ugdelarc(g, ab0), "deleting dead arc is an error");
	CHECK(ugdeldot(g, a) == 2, "ugdeldot cascades remaining incident arcs");
	CHECK(ugndot(g) == 2 && ugnarc(g) == 1, "cascade leaves unrelated arc live");
	CHECK(uglinked(g, b, c) == 1, "unrelated live arc remains linked");
	uvlong arcs [4] = {99, 99, 99, 99};
	uvlong n        = ugarcs(g, arcs, arrlen(arcs));
	CHECK(n == 1 && arcs [0] == bc && !has_uv(arcs, n, ab1) && !has_uv(arcs, n, ac),
	      "ugarcs skips cascade-deleted arcs");
	check_expected_error(ugarcref(g, ab1) == 0, "dead arc ref is an error");
	check_expected_error(uglinked(g, a, b) == 0, "dead dot in uglinked is an error");
	rmugraph(g);
}

static void connectivity_and_component(void) {
	printf("\n=== ugraph: connectivity and component ===\n");
	int g = mkugraph(0);
	CHECK(g >= 0, "mkugraph for connectivity");
	uvlong a = ugdot(g, 1);
	uvlong b = ugdot(g, 2);
	uvlong c = ugdot(g, 3);
	uvlong d = ugdot(g, 4);
	ugarc(g, a, b, 10);
	ugarc(g, b, c, 20);
	CHECK(ugreaches(g, a, c), "ugreaches sees transitive undirected path");
	CHECK(ugreaches(g, a, a), "ugreaches is reflexive for live dot");
	CHECK(!ugreaches(g, a, d), "ugreaches rejects disconnected dot");
	CHECK(!err(), "ugreaches false disconnected paths are quiet");
	uvlong buf [4] = {99, 99, 99, 99};
	uvlong n       = ugcomp(g, a, buf, 2);
	CHECK(n == 3 && buf [2] == 99 && buf [3] == 99, "ugcomp returns full count with cap");
	n = ugcomp(g, a, buf, 4);
	CHECK(n == 3 && has_uv(buf, n, a) && has_uv(buf, n, b) && has_uv(buf, n, c) && !has_uv(buf, n, d),
	      "ugcomp enumerates connected component");
	CHECK(ugdeldot(g, b) == 2, "deleting bridge cascades component arcs");
	CHECK(!ugreaches(g, a, c), "deleted bridge disconnects component");
	rmugraph(g);
}

static void invalid_inputs(void) {
	printf("\n=== ugraph: invalid inputs ===\n");
	int g = mkugraph(0);
	CHECK(g >= 0, "mkugraph for invalids");
	uvlong a  = ugdot(g, 1);
	uvlong b  = ugdot(g, 2);
	uvlong ab = ugarc(g, a, b, 10);
	CHECK(ugdot(-1, 1) == ( uvlong ) -1, "ugdot rejects invalid graph");
	check_expected_error(true, "ugdot invalid graph sets error");
	CHECK(ugarc(g, a, 99, 1) == ( uvlong ) -1, "ugarc rejects bad dot");
	check_expected_error(true, "ugarc bad dot sets error");
	CHECK(ugdots(g, null, 1) == 0, "ugdots rejects null buffer with cap");
	check_expected_error(true, "ugdots null buffer sets error");
	CHECK(ugarcs(-1, null, 0) == 0, "ugarcs rejects invalid graph");
	check_expected_error(true, "ugarcs invalid graph sets error");
	uvlong x;
	CHECK(!ugends(g, ab, &x, null), "ugends rejects null endpoint output");
	check_expected_error(true, "ugends null output sets error");
	CHECK(ugadots(g, 99, null, 0) == 0, "ugadots rejects bad dot");
	check_expected_error(true, "ugadots bad dot sets error");
	CHECK(ugaarcs(g, a, null, 1) == 0, "ugaarcs rejects null buffer with cap");
	check_expected_error(true, "ugaarcs null buffer sets error");
	CHECK(ugdeg(g, 99) == 0, "ugdeg rejects bad dot");
	check_expected_error(true, "ugdeg bad dot sets error");
	CHECK(!ugreaches(g, a, 99), "ugreaches rejects bad dot");
	check_expected_error(true, "ugreaches bad dot sets error");
	CHECK(ugcomp(g, 99, null, 0) == 0, "ugcomp rejects bad dot");
	check_expected_error(true, "ugcomp bad dot sets error");
	rmugraph(g);
}

static void growth_and_reuse(void) {
	printf("\n=== ugraph: growth and descriptor reuse ===\n");
	int g = mkugraph(0);
	CHECK(g >= 0, "mkugraph for growth");
	uvlong ids [100];
	bool   dots = true;
	for (uvlong i = 0; i < arrlen(ids); i++) {
		ids [i] = ugdot(g, i + 1000);
		dots    = dots && ids [i] != ( uvlong ) -1;
	}
	CHECK(dots && ugndot(g) == arrlen(ids), "ugdot grows dot storage");
	bool arcs = true;
	for (uvlong i = 0; i + 1 < arrlen(ids); i++) arcs = arcs && ugarc(g, ids [i], ids [i + 1], i) != ( uvlong ) -1;
	CHECK(arcs && ugnarc(g) == arrlen(ids) - 1, "ugarc grows arc storage");
	CHECK(ugreaches(g, ids [0], ids [99]), "grown graph connectivity");
	rmugraph(g);
	CHECK(ugndot(g) == 0 && ugnarc(g) == 0, "removed graph counts are zero");
	int reused = mkugraph(0);
	CHECK(reused == g, "mkugraph reuses removed descriptor");
	rmugraph(reused);
}

int main(void) {
	dot_refs_and_all_enum();
	arc_refs_parallel_and_self_loop();
	deletion_cascades_and_tombstones();
	connectivity_and_component();
	invalid_inputs();
	growth_and_reuse();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
