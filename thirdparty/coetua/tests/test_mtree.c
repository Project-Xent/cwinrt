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

static void creation_refs_and_order(void) {
	printf("\n=== mtree: creation refs and order ===\n");
	int t = mkmtree(0);
	CHECK(t >= 0, "mkmtree");
	uvlong r0 = mtroot(t, 100);
	uvlong r1 = mtroot(t, 200);
	uvlong a  = mtknob(t, r0, 300, 30);
	uvlong b  = mtknob(t, r0, 400, 40);
	uvlong c  = mtknob(t, a, 500, 50);
	CHECK(r0 != ( uvlong ) -1 && r1 != ( uvlong ) -1 && a != ( uvlong ) -1 && b != ( uvlong ) -1 && c != ( uvlong ) -1,
	      "mtroot and mtknob create knobs");
	CHECK(mtnknob(t) == 5 && mtnarm(t) == 3, "mtree counts live knobs and arms");
	CHECK(mtknobref(t, a) == 300, "mtknobref returns ref");
	rmtknobref(t, a, 333);
	CHECK(mtknobref(t, a) == 333, "rmtknobref updates ref");
	uvlong aa = mtinarm(t, a);
	CHECK(aa != ( uvlong ) -1 && mtarmref(t, aa) == 30, "mtinarm exposes incoming arm");
	rmtarmref(t, aa, 35);
	CHECK(mtarmref(t, aa) == 35, "rmtarmref updates arm ref");
	CHECK(mtisroot(t, r0) && mtisroot(t, r1) && !mtisroot(t, a), "mtisroot distinguishes roots");
	CHECK(mtpar(t, c) == a, "mtpar returns parent");
	uvlong roots [4] = {99, 99, 99, 99};
	CHECK(mtroots(t, null, 0) == 2, "mtroots counts roots without buffer");
	CHECK(mtroots(t, roots, 1) == 2 && roots [1] == 99, "mtroots returns full count with cap");
	uvlong nr = mtroots(t, roots, arrlen(roots));
	CHECK(nr == 2 && has_uv(roots, nr, r0) && has_uv(roots, nr, r1), "mtroots enumerates live roots");
	uvlong kids [4] = {99, 99, 99, 99};
	uvlong arms [4] = {99, 99, 99, 99};
	CHECK(mtkids(t, r0, kids, arrlen(kids)) == 2 && kids [0] == a && kids [1] == b, "mtkids preserves sibling order");
	CHECK(mtkidarms(t, r0, arms, arrlen(arms)) == 2 && arms [0] == mtinarm(t, a) && arms [1] == mtinarm(t, b),
	      "mtkidarms matches kid order");
	CHECK(mtknobs(t, null, 0) == 5, "mtknobs counts without buffer");
	CHECK(mtknobs(t, kids, 2) == 5 && kids [2] == 99 && kids [3] == 99, "mtknobs respects cap");
	rmmtree(t);
}

static void move_and_detach(void) {
	printf("\n=== mtree: move and detach ===\n");
	int t = mkmtree(0);
	CHECK(t >= 0, "mkmtree for move");
	uvlong r    = mtroot(t, 1);
	uvlong a    = mtknob(t, r, 2, 20);
	uvlong b    = mtknob(t, r, 3, 30);
	uvlong d    = mtknob(t, b, 5, 50);
	uvlong c    = mtknob(t, a, 4, 40);
	uvlong aarm = mtinarm(t, a);
	uvlong darm = mtinarm(t, d);
	CHECK(mtmove(t, a, b), "mtmove moves non-root subtree");
	CHECK(mtinarm(t, a) == aarm && mtarmref(t, aarm) == 20, "mtmove preserves incoming arm id and ref");
	CHECK(mtpar(t, a) == b && mtancestor(t, b, c), "mtmove updates parent and preserves subtree");
	uvlong kids [4] = {99, 99, 99, 99};
	uvlong arms [4] = {99, 99, 99, 99};
	CHECK(mtkids(t, r, kids, arrlen(kids)) == 1 && kids [0] == b, "mtmove removes kid from old parent order");
	CHECK(mtkids(t, b, kids, arrlen(kids)) == 2 && kids [0] == d && kids [1] == a, "mtmove appends kid to new parent");
	CHECK(mtkidarms(t, b, arms, arrlen(arms)) == 2 && arms [0] == darm && arms [1] == aarm,
	      "mtmove preserves kidarm order");
	CHECK(mtmove(t, a, b), "mtmove to same parent is a no-op");
	CHECK(mtdetach(t, a), "mtdetach makes subtree root");
	CHECK(mtnarm(t) == 3 && mtisroot(t, a), "mtdetach tombstones incoming arm");
	check_expected_error(mtarmref(t, aarm) == 0, "detached arm ref is an error");
	check_expected_error(mtpar(t, a) == ( uvlong ) -1, "detached root has no parent");
	CHECK(mtroots(t, kids, arrlen(kids)) == 2 && has_uv(kids, 2, r) && has_uv(kids, 2, a), "detached subtree is root");
	CHECK(mtkids(t, b, kids, arrlen(kids)) == 1 && kids [0] == d, "mtdetach removes kid from old parent");
	rmmtree(t);
}

static void subtree_delete(void) {
	printf("\n=== mtree: subtree deletion ===\n");
	int t = mkmtree(0);
	CHECK(t >= 0, "mkmtree for delete");
	uvlong r    = mtroot(t, 1);
	uvlong a    = mtknob(t, r, 2, 20);
	uvlong b    = mtknob(t, r, 3, 30);
	uvlong c    = mtknob(t, a, 4, 40);
	uvlong d    = mtknob(t, a, 5, 50);
	uvlong carm = mtinarm(t, c);
	CHECK(d != ( uvlong ) -1, "delete sample creates second descendant");
	CHECK(mtdelknob(t, a) == 3, "mtdelknob returns deleted subtree knob count");
	CHECK(mtnknob(t) == 2 && mtnarm(t) == 1, "mtdelknob updates live counts");
	CHECK(mtkids(t, r, null, 0) == 1, "deleted subtree is removed from parent order");
	uvlong kids [4] = {99, 99, 99, 99};
	CHECK(mtkids(t, r, kids, arrlen(kids)) == 1 && kids [0] == b, "sibling subtree remains live");
	check_expected_error(mtknobref(t, c) == 0, "deleted knob ref is an error");
	check_expected_error(mtarmref(t, carm) == 0, "deleted arm ref is an error");
	CHECK(mtdelknob(t, r) == 2, "mtdelknob deletes remaining root subtree");
	CHECK(mtnknob(t) == 0 && mtnarm(t) == 0, "all knobs and arms deleted");
	rmmtree(t);
}

static void ancestry_and_subtree(void) {
	printf("\n=== mtree: ancestry and subtree ===\n");
	int t = mkmtree(0);
	CHECK(t >= 0, "mkmtree for ancestry");
	uvlong r = mtroot(t, 1);
	uvlong a = mtknob(t, r, 2, 20);
	uvlong b = mtknob(t, r, 3, 30);
	uvlong c = mtknob(t, a, 4, 40);
	uvlong d = mtknob(t, a, 5, 50);
	CHECK(mtancestor(t, r, d) && mtancestor(t, a, c) && mtancestor(t, a, a), "mtancestor is reflexive and transitive");
	CHECK(!mtancestor(t, b, d), "mtancestor rejects unrelated branch");
	CHECK(!err(), "false ancestry is quiet for live knobs");
	uvlong buf [5] = {99, 99, 99, 99, 99};
	CHECK(mtsubtree(t, a, buf, 2) == 3 && buf [2] == 99, "mtsubtree returns full count with cap");
	uvlong n = mtsubtree(t, a, buf, arrlen(buf));
	CHECK(n == 3 && has_uv(buf, n, a) && has_uv(buf, n, c) && has_uv(buf, n, d) && !has_uv(buf, n, b),
	      "mtsubtree enumerates subtree membership");
	rmmtree(t);
}

static void invalid_inputs(void) {
	printf("\n=== mtree: invalid inputs ===\n");
	int t = mkmtree(0);
	CHECK(t >= 0, "mkmtree for invalids");
	uvlong r = mtroot(t, 1);
	uvlong a = mtknob(t, r, 2, 20);
	uvlong b = mtknob(t, a, 3, 30);
	CHECK(mtroot(-1, 1) == ( uvlong ) -1, "mtroot rejects invalid tree");
	check_expected_error(true, "mtroot invalid tree sets error");
	CHECK(mtknob(t, 99, 1, 1) == ( uvlong ) -1, "mtknob rejects bad parent");
	check_expected_error(true, "mtknob bad parent sets error");
	CHECK(mtknobs(t, null, 1) == 0, "mtknobs rejects null buffer with cap");
	check_expected_error(true, "mtknobs null buffer sets error");
	check_expected_error(mtpar(t, r) == ( uvlong ) -1, "mtpar rejects root");
	check_expected_error(mtinarm(t, r) == ( uvlong ) -1, "mtinarm rejects root");
	check_expected_error(!mtdetach(t, r), "mtdetach rejects root");
	check_expected_error(!mtmove(t, r, a), "mtmove rejects root");
	check_expected_error(!mtmove(t, a, a), "mtmove rejects self-parenting");
	check_expected_error(!mtmove(t, a, b), "mtmove rejects moving under descendant");
	CHECK(mtkids(t, 99, null, 0) == 0, "mtkids rejects bad parent");
	check_expected_error(true, "mtkids bad parent sets error");
	CHECK(mtkidarms(t, a, null, 1) == 0, "mtkidarms rejects null buffer with cap");
	check_expected_error(true, "mtkidarms null buffer sets error");
	CHECK(!mtancestor(t, a, 99), "mtancestor rejects bad knob");
	check_expected_error(true, "mtancestor bad knob sets error");
	CHECK(mtsubtree(t, 99, null, 0) == 0, "mtsubtree rejects bad knob");
	check_expected_error(true, "mtsubtree bad knob sets error");
	rmmtree(t);
}

static void growth_and_reuse(void) {
	printf("\n=== mtree: growth and descriptor reuse ===\n");
	int t = mkmtree(0);
	CHECK(t >= 0, "mkmtree for growth");
	uvlong ids [100];
	ids [0] = mtroot(t, 1000);
	bool ok = ids [0] != ( uvlong ) -1;
	for (uvlong i = 1; i < arrlen(ids); i++) {
		ids [i] = mtknob(t, ids [i - 1], i + 1000, i);
		ok      = ok && ids [i] != ( uvlong ) -1;
	}
	CHECK(ok && mtnknob(t) == arrlen(ids) && mtnarm(t) == arrlen(ids) - 1, "mtree grows knob and arm storage");
	CHECK(mtancestor(t, ids [0], ids [99]), "grown mtree ancestry");
	rmmtree(t);
	CHECK(mtnknob(t) == 0 && mtnarm(t) == 0, "removed mtree counts are zero");
	int reused = mkmtree(0);
	CHECK(reused == t, "mkmtree reuses removed descriptor");
	rmmtree(reused);
}

int main(void) {
	creation_refs_and_order();
	move_and_detach();
	subtree_delete();
	ancestry_and_subtree();
	invalid_inputs();
	growth_and_reuse();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
