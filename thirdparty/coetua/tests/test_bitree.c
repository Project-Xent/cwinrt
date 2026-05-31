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

static bool refs_are(uvlong *ks, uvlong *refs, uvlong n) {
	for (uvlong i = 0; i < n; i++)
		if (btref(ks [i]) != refs [i]) return false;
	return true;
}

static bool kids_are(uvlong k, uvlong zero, uvlong one) {
	uvlong z = 99, o = 99;
	btkids(k, &z, &o);
	return z == zero && o == one;
}

static void creation_refs_and_queries(void) {
	printf("\n=== bitree: creation refs and queries ===\n");
	uvlong r = mkbitree(0, 10);
	CHECK(r != 0, "mkbitree creates root knod");
	CHECK(btref(r) == 10, "btref reads ref");
	rbtref(r, 11);
	CHECK(btref(r) == 11, "rbtref updates ref");
	CHECK(btpar(r) == 0 && !err(), "btpar root is quiet zero");
	check_expected_error(!btside(r), "btside root sets error");

	uvlong z = 20;
	uvlong o = 30;
	rbtkids(0, r, &z, &o);
	CHECK(z != 20 && o != 30 && btpar(z) == r && btpar(o) == r, "rbtkids creates both children");
	CHECK(!btside(z) && btside(o), "btside reports zero and one");
	uvlong qz = 0, qo = 0;
	btkids(r, &qz, &qo);
	CHECK(qz == z && qo == o, "btkids returns both children");
	qz = 99;
	btkids(r, &qz, null);
	CHECK(qz == z, "btkids skips null output");
}

static void batch_creation_errors(void) {
	printf("\n=== bitree: batch creation errors ===\n");
	uvlong r = mkbitree(0, 1);
	uvlong z = 2;
	rbtkids(0, r, &z, null);
	CHECK(btref(z) == 2 && btpar(z) == r, "rbtkids creates one side");
	uvlong oldz = z;
	uvlong nz   = 3;
	rbtkids(0, r, &nz, null);
	CHECK(nz == 3 && btpar(oldz) == r, "rbtkids occupied slot leaves input unchanged");
	check_expected_error(true, "rbtkids occupied slot sets error");
	rbtkids(0, r, null, null);
	check_expected_error(true, "rbtkids rejects both null");
	btkids(r, null, null);
	check_expected_error(true, "btkids rejects both null");
	rbtkids(-1, r, null, &nz);
	check_expected_error(true, "rbtkids invalid arena sets error");
}

static void drop_and_place(void) {
	printf("\n=== bitree: drop and place ===\n");
	uvlong r = mkbitree(0, 1);
	uvlong z = 2, o = 3;
	rbtkids(0, r, &z, &o);
	uvlong dropped = btdrop(r, 0);
	CHECK(dropped == z && btpar(z) == 0, "btdrop detaches present zero child");
	CHECK(btdrop(r, 0) == 0 && !err(), "btdrop empty slot is quiet zero");
	CHECK(btplace(r, 0, z) == 0 && btpar(z) == r, "btplace fills empty slot");
	uvlong replacement = mkbitree(0, 4);
	uvlong old         = btplace(r, 0, replacement);
	CHECK(old == z && btpar(old) == 0 && btpar(replacement) == r, "btplace replaces and returns old child");
	check_expected_error(btplace(r, 0, 0) == 0, "btplace rejects null kid");
	check_expected_error(btplace(r, 2, z) == 0, "btplace rejects bad side");
	check_expected_error(btplace(r, 1, replacement) == 0, "btplace rejects parented kid");
	check_expected_error(btplace(replacement, 0, r) == 0, "btplace rejects cycle");
	check_expected_error(btdrop(0, 0) == 0, "btdrop rejects null parent");
}

static void rotations(void) {
	printf("\n=== bitree: rotations ===\n");
	uvlong r = mkbitree(0, 4);
	uvlong z = 2, o = 6;
	rbtkids(0, r, &z, &o);
	uvlong zz = 1, zo = 3;
	rbtkids(0, z, &zz, &zo);
	uvlong nr = btrot(r, 0);
	CHECK(nr == z && btpar(nr) == 0 && btpar(r) == z && btpar(zo) == r && kids_are(z, zz, r) && kids_are(r, zo, o),
	      "btrot promotes zero child");
	uvlong back = btrot(nr, 1);
	CHECK(back == r && btpar(r) == 0 && btpar(z) == r && kids_are(r, z, o), "btrot promotes one child back");
	uvlong nochild = mkbitree(0, 9);
	check_expected_error(btrot(nochild, 0) == 0, "btrot missing child sets error");

	uvlong lr = mkbitree(0, 30);
	uvlong lz = 10, lo = 40;
	rbtkids(0, lr, &lz, &lo);
	uvlong lzo = 20;
	rbtkids(0, lz, null, &lzo);
	uvlong lroot = btrrotte(lr, 1, 0);
	CHECK(lroot == lzo && btpar(lroot) == 0 && btpar(lr) == lzo && btpar(lz) == lzo && kids_are(lroot, lz, lr),
	      "btrrotte handles zero-one double rotation");

	uvlong rr = mkbitree(0, 10);
	uvlong rz = 5, ro = 30;
	rbtkids(0, rr, &rz, &ro);
	uvlong roz = 20;
	rbtkids(0, ro, &roz, null);
	uvlong rroot = btrrotte(rr, 0, 1);
	CHECK(rroot == roz && btpar(rroot) == 0 && btpar(rr) == roz && btpar(ro) == roz && kids_are(rroot, rr, ro),
	      "btrrotte handles one-zero double rotation");

	uvlong zzr = mkbitree(0, 30);
	uvlong zza = 20;
	rbtkids(0, zzr, &zza, null);
	uvlong zzb = 10;
	rbtkids(0, zza, &zzb, null);
	CHECK(btrrotte(zzr, 0, 0) == zzb, "btrrotte handles zero-zero pair");

	uvlong oor = mkbitree(0, 10);
	uvlong ooa = 20;
	rbtkids(0, oor, null, &ooa);
	uvlong oob = 30;
	rbtkids(0, ooa, null, &oob);
	CHECK(btrrotte(oor, 1, 1) == oob, "btrrotte handles one-one pair");

	uvlong p = mkbitree(0, 100);
	CHECK(btplace(p, 0, r) == 0, "btplace attaches rotated subtree under parent");
	uvlong sub = btrot(r, 0);
	CHECK(btpar(sub) == p && btpar(r) == sub, "btrot reconnects parent slot");
}

static void walks_and_cleanup(void) {
	printf("\n=== bitree: walks and cleanup ===\n");
	uvlong r = mkbitree(0, 4);
	uvlong z = 2, o = 6;
	rbtkids(0, r, &z, &o);
	uvlong zz = 1, zo = 3, oz = 5, oo = 7;
	rbtkids(0, z, &zz, &zo);
	rbtkids(0, o, &oz, &oo);
	uvlong buf [8] = {0};
	uvlong pre []  = {4, 2, 1, 3, 6, 5, 7};
	uvlong in []   = {1, 2, 3, 4, 5, 6, 7};
	uvlong post [] = {1, 3, 2, 5, 7, 6, 4};
	uvlong lev []  = {4, 2, 6, 1, 3, 5, 7};
	CHECK(btwalk(r, 0, null, 0) == 7, "btwalk counts without buffer");
	CHECK(btwalk(r, 0, buf, 7) == 7 && refs_are(buf, pre, 7), "btwalk preorder");
	CHECK(btwalk(r, 1, buf, 7) == 7 && refs_are(buf, in, 7), "btwalk inorder");
	CHECK(btwalk(r, 2, buf, 7) == 7 && refs_are(buf, post, 7), "btwalk postorder");
	CHECK(btwalk(r, 3, buf, 7) == 7 && refs_are(buf, lev, 7), "btwalk level order");
	buf [2] = 99;
	CHECK(btwalk(r, 0, buf, 2) == 7 && btref(buf [0]) == 4 && btref(buf [1]) == 2 && buf [2] == 99,
	      "btwalk respects cap");
	check_expected_error(btwalk(r, 4, null, 0) == 0, "btwalk rejects bad order");
	check_expected_error(btwalk(r, 0, null, 1) == 0, "btwalk rejects null nonzero buffer");
	check_expected_error(btwalk(0, 0, null, 0) == 0, "btwalk rejects null root");
	rmbitree(r);
	CHECK(btref(r) == 0 && btpar(z) == 0 && btpar(o) == 0, "rmbitree clears subtree shape and refs");

	uvlong pr = mkbitree(0, 8);
	uvlong pz = 9, po = 10;
	rbtkids(0, pr, &pz, &po);
	rmbitree(pz);
	uvlong qz = 99, qo = 99;
	btkids(pr, &qz, &qo);
	CHECK(qz == 0 && qo == po && btpar(po) == pr, "rmbitree detaches cleared subtree from parent");
}

static void null_errors_and_growth(void) {
	printf("\n=== bitree: null errors and growth ===\n");
	check_expected_error(btref(0) == 0, "btref rejects null");
	rbtref(0, 1);
	check_expected_error(true, "rbtref rejects null");
	check_expected_error(btpar(0) == 0, "btpar rejects null");
	btkids(0, null, &(uvlong) {0});
	check_expected_error(true, "btkids rejects null knod");
	check_expected_error(btside(0), "btside rejects null");
	rmbitree(0);
	check_expected_error(true, "rmbitree rejects null root");

	bool ok = true;
	for (uvlong i = 0; i < 300; i++) {
		uvlong k = mkbitree(0, i);
		ok       = ok && k != 0 && btref(k) == i;
	}
	CHECK(ok, "bitree allocates many knods");
}

int main(void) {
	creation_refs_and_queries();
	batch_creation_errors();
	drop_and_place();
	rotations();
	walks_and_cleanup();
	null_errors_and_growth();
	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
