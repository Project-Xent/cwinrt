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

static int sample_dag(uvlong *a, uvlong *b, uvlong *c, uvlong *ab, uvlong *ac, uvlong *bc) {
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag");
	*a  = dgdot(dag, 100);
	*b  = dgdot(dag, 200);
	*c  = dgdot(dag, 300);
	*ab = dgarc(dag, *a, *b, 10);
	*ac = dgarc(dag, *a, *c, 20);
	*bc = dgarc(dag, *b, *c, 30);
	return dag;
}

static void dot_refs(void) {
	printf("\n=== nexus: dot refs ===\n");
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag for dot refs");
	uvlong a = dgdot(dag, 100);
	uvlong b = dgdot(dag, 200);
	uvlong c = dgdot(dag, 300);
	CHECK(a == 0 && b == 1 && c == 2, "dgdot returns dense ids");
	CHECK(dgndot(dag) == 3, "dgndot counts dots");
	CHECK(dgdotref(dag, b) == 200, "dgdotref returns dot ref");
	rdgdotref(dag, b, 250);
	CHECK(dgdotref(dag, b) == 250, "rdgdotref updates dot ref");
	rmdag(dag);
}

static void arc_refs(void) {
	printf("\n=== nexus: arc refs ===\n");
	uvlong a, b, c, ab, ac, bc;
	int    dag = sample_dag(&a, &b, &c, &ab, &ac, &bc);
	CHECK(ab == 0 && ac == 1 && bc == 2, "dgarc returns dense arc ids");
	CHECK(dgnarc(dag) == 3, "dgnarc counts arcs");
	CHECK(dgarcref(dag, ac) == 20, "dgarcref returns arc ref");
	rdgarcref(dag, ac, 25);
	CHECK(dgarcref(dag, ac) == 25, "rdgarcref updates arc ref");
	CHECK(dgfrom(dag, bc) == b && dgto(dag, bc) == c, "dgfrom/dgto expose endpoints");
	CHECK(dglinked(dag, a, b), "dglinked direct arc");
	CHECK(!dglinked(dag, c, a), "dglinked rejects absent arc");
	rmdag(dag);
}

static void adjacency_views(void) {
	printf("\n=== nexus: adjacency views ===\n");
	uvlong a, b, c, ab, ac, bc;
	int    dag     = sample_dag(&a, &b, &c, &ab, &ac, &bc);
	uvlong buf [4] = {99, 99, 99, 99};
	uvlong n       = dgkids(dag, a, buf, 1);
	CHECK(n == 2 && buf [0] == b && buf [1] == 99, "dgkids returns total and respects cap");
	n = dgkids(dag, a, buf, 4);
	CHECK(n == 2 && buf [0] == b && buf [1] == c, "dgkids copies child dots");
	n = dgpars(dag, c, buf, 4);
	CHECK(n == 2 && buf [0] == a && buf [1] == b, "dgpars copies parent dots");
	n = dgouts(dag, a, buf, 4);
	CHECK(n == 2 && buf [0] == ab && buf [1] == ac, "dgouts copies outgoing arcs");
	n = dgins(dag, c, buf, 4);
	CHECK(n == 2 && buf [0] == ac && buf [1] == bc, "dgins copies incoming arcs");
	CHECK(dgkids(dag, a, null, 0) == 2, "dgkids can count without buffer");
	CHECK(dgouts(dag, a, null, 0) == 2, "dgouts can count without buffer");
	rmdag(dag);
}

static void cycles_and_upserts(void) {
	printf("\n=== nexus: cycles and upserts ===\n");
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag for cycle test");
	uvlong a  = dgdot(dag, 1);
	uvlong b  = dgdot(dag, 2);
	uvlong c  = dgdot(dag, 3);
	uvlong ab = dgarc(dag, a, b, 11);
	dgarc(dag, b, c, 22);
	CHECK(dgarc(dag, a, b, 99) == ab, "duplicate dgarc returns same arc");
	CHECK(dgarcref(dag, ab) == 99 && dgnarc(dag) == 2, "duplicate dgarc updates ref only");
	dgarc(dag, c, a, 33);
	check_expected_error(!dglinked(dag, c, a), "dgarc rejects cycle");
	dgarc(dag, a, a, 44);
	check_expected_error(!dglinked(dag, a, a), "dgarc rejects self-cycle");
	rmdag(dag);
}

static void reachability(void) {
	printf("\n=== nexus: reachability ===\n");
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag for reachability");
	uvlong a = dgdot(dag, 1);
	uvlong b = dgdot(dag, 2);
	uvlong c = dgdot(dag, 3);
	uvlong d = dgdot(dag, 4);
	uvlong e = dgdot(dag, 5);
	dgarc(dag, a, b, 10);
	dgarc(dag, b, c, 20);
	dgarc(dag, a, d, 30);
	CHECK(dgreaches(dag, a, b), "dgreaches sees direct arc");
	CHECK(dgreaches(dag, a, c), "dgreaches sees transitive path");
	CHECK(dgreaches(dag, a, a), "dgreaches is reflexive");
	CHECK(!dgreaches(dag, c, a), "dgreaches respects direction");
	CHECK(!dgreaches(dag, d, c), "dgreaches rejects disconnected path");
	CHECK(!dgreaches(dag, a, e), "dgreaches rejects isolated target");
	CHECK(!err(), "dgreaches false paths are quiet");
	rmdag(dag);
}

static void topo_order(void) {
	printf("\n=== nexus: topo order ===\n");
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag for topo");
	uvlong a = dgdot(dag, 1);
	uvlong b = dgdot(dag, 2);
	uvlong c = dgdot(dag, 3);
	uvlong d = dgdot(dag, 4);
	dgarc(dag, a, b, 10);
	dgarc(dag, a, c, 20);
	dgarc(dag, b, d, 30);
	dgarc(dag, c, d, 40);
	uvlong buf [4] = {99, 99, 99, 99};
	CHECK(dgtopo(dag, null, 0) == 4, "dgtopo counts without buffer");
	CHECK(dgtopo(dag, buf, 2) == 4, "dgtopo returns full count with small cap");
	CHECK(buf [0] == a && (buf [1] == b || buf [1] == c), "dgtopo respects cap and starts with root");
	CHECK(dgtopo(dag, buf, 4) == 4, "dgtopo fills full order");
	CHECK(buf [0] == a && buf [3] == d, "dgtopo orders dependencies");
	CHECK((buf [1] == b && buf [2] == c) || (buf [1] == c && buf [2] == b), "dgtopo keeps independent dots flexible");
	rmdag(dag);
}

static void roots_and_leaves(void) {
	printf("\n=== nexus: roots and leaves ===\n");
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag for boundaries");
	uvlong a = dgdot(dag, 1);
	uvlong b = dgdot(dag, 2);
	uvlong c = dgdot(dag, 3);
	uvlong d = dgdot(dag, 4);
	uvlong e = dgdot(dag, 5);
	dgarc(dag, a, b, 10);
	dgarc(dag, a, c, 20);
	dgarc(dag, b, d, 30);
	dgarc(dag, c, d, 40);
	uvlong buf [5] = {99, 99, 99, 99, 99};
	CHECK(dgroots(dag, null, 0) == 2, "dgroots counts without buffer");
	CHECK(dgroots(dag, buf, 1) == 2, "dgroots returns full count with cap");
	CHECK(buf [0] == a && buf [1] == 99, "dgroots respects cap");
	CHECK(dgroots(dag, buf, 5) == 2 && buf [0] == a && buf [1] == e, "dgroots copies source dots");
	CHECK(dgleaves(dag, null, 0) == 2, "dgleaves counts without buffer");
	CHECK(dgleaves(dag, buf, 1) == 2, "dgleaves returns full count with cap");
	CHECK(buf [0] == d, "dgleaves respects cap");
	CHECK(dgleaves(dag, buf, 5) == 2 && buf [0] == d && buf [1] == e, "dgleaves copies sink dots");
	rmdag(dag);
}

static void invalid_inputs(void) {
	printf("\n=== nexus: invalid inputs ===\n");
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag for errors");
	CHECK(dgdotref(dag, 0) == 0, "dgdotref invalid returns zero");
	check_expected_error(true, "dgdotref invalid sets error");
	CHECK(dgarc(dag, 0, 1, 9) == ( uvlong ) -1, "dgarc invalid returns -1");
	check_expected_error(true, "dgarc rejects invalid dot");
	CHECK(!dglinked(dag, 0, 1), "dglinked invalid dots is false");
	CHECK(!err(), "dglinked invalid dots is quiet");
	CHECK(dgndot(-1) == 0 && dgnarc(-1) == 0, "invalid dag counts are zero");
	CHECK(!err(), "invalid dag counts are quiet");
	CHECK(dgkids(dag, 0, null, 0) == 0, "dgkids invalid zero-cap returns zero");
	check_expected_error(true, "dgkids invalid sets error");
	uvlong tmp;
	CHECK(dgins(dag, 0, &tmp, 1) == 0, "dgins invalid returns zero");
	check_expected_error(true, "dgins invalid sets error");
	CHECK(dgarcref(dag, 0) == 0, "dgarcref invalid returns zero");
	check_expected_error(true, "dgarcref invalid sets error");
	CHECK(dgfrom(dag, 0) == 0, "dgfrom invalid returns zero");
	check_expected_error(true, "dgfrom invalid sets error");
	CHECK(dgroots(dag, null, 1) == 0, "dgroots rejects null buffer with cap");
	check_expected_error(true, "dgroots null buffer sets error");
	CHECK(dgleaves(dag, null, 1) == 0, "dgleaves rejects null buffer with cap");
	check_expected_error(true, "dgleaves null buffer sets error");
	CHECK(dgroots(-1, null, 0) == 0, "dgroots rejects invalid dag");
	check_expected_error(true, "dgroots invalid dag sets error");
	CHECK(dgleaves(-1, null, 0) == 0, "dgleaves rejects invalid dag");
	check_expected_error(true, "dgleaves invalid dag sets error");
	CHECK(!dgreaches(dag, 0, 1), "dgreaches rejects invalid dot");
	check_expected_error(true, "dgreaches invalid dot sets error");
	CHECK(!dgreaches(-1, 0, 0), "dgreaches rejects invalid dag");
	check_expected_error(true, "dgreaches invalid dag sets error");
	CHECK(dgtopo(dag, null, 1) == 0, "dgtopo rejects null buffer with cap");
	check_expected_error(true, "dgtopo null buffer sets error");
	CHECK(dgtopo(-1, null, 0) == 0, "dgtopo rejects invalid dag");
	check_expected_error(true, "dgtopo invalid dag sets error");
	rmdag(dag);
}

static void growth_and_reuse(void) {
	printf("\n=== nexus: growth and reuse ===\n");
	int dag = mkdag(0);
	CHECK(dag >= 0, "mkdag for growth");
	bool dense = true;
	for (uvlong i = 0; i < 100; i++) dense = dense && dgdot(dag, i + 1000) == i;
	CHECK(dense, "dgdot grows dot storage");
	bool arcs = true;
	for (uvlong i = 0; i + 1 < 100; i++) arcs = arcs && dgarc(dag, i, i + 1, i) == i;
	CHECK(arcs, "dgarc grows arc storage");
	CHECK(dgndot(dag) == 100 && dgnarc(dag) == 99, "dot and arc counts after growth");
	CHECK(dglinked(dag, 98, 99), "grown dag linkage lookup");
	uvlong tmp;
	CHECK(dgkids(dag, 50, &tmp, 1) == 1 && tmp == 51, "grown dag kid lookup");
	CHECK(dgouts(dag, 50, &tmp, 1) == 1 && tmp == 50, "grown dag out arc lookup");
	rmdag(dag);
	CHECK(dgndot(dag) == 0 && dgnarc(dag) == 0, "rmdag removes descriptor");
	int reused = mkdag(0);
	CHECK(reused == dag, "mkdag reuses removed descriptor");
	rmdag(reused);
}

int main(void) {
	dot_refs();
	arc_refs();
	adjacency_views();
	cycles_and_upserts();
	reachability();
	topo_order();
	roots_and_leaves();
	invalid_inputs();
	growth_and_reuse();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
