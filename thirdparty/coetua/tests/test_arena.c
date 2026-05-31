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

static void clears_error(void) { errmsg(null); }

static void basic_spec(void) {
	printf("=== arena: basic allocation ===\n");
	void *p = den(1);
	void *q = den(1024 * 1024);
	CHECK(p != null, "den(1) non-null");
	CHECK(q != null, "den(1 MiB) non-null");
	CHECK(p != q, "different allocations");

	CHECK(den(0) == null, "den(0) returns null");
	CHECK(!err(), "den(0) does not set error");

	int *arr = ( int * ) sala(100, sizeof(int));
	CHECK(arr != null, "sala(100, int) non-null");
	bool all_zero = true;
	for (int i = 0; i < 100; i++)
		if (arr [i] != 0) all_zero = false;
	CHECK(all_zero, "sala zero-initialized");

	char *s = ( char * ) den(64);
	strcpy(s, "hello arena");
	den(64);
	CHECK(strcmp(s, "hello arena") == 0, "previous allocation untouched by newer one");
}

static void alignment_spec(void) {
	printf("\n=== arena: alignment ===\n");
	void *a1 = carrel(16, 32);
	CHECK(a1 != null && (( uintptr_t ) a1 & 15) == 0, "carrel returns 16-byte aligned block");

	void *a2 = carrel(64, 64);
	CHECK(a2 != null && (( uintptr_t ) a2 & 63) == 0, "carrel returns 64-byte aligned block");

	CHECK(carrel(1, 13) != null, "alignment 1 is accepted");
	CHECK(carrel(3, 13) == null && err(), "non-power-of-two alignment fails");
	clears_error();

	void *a4 = carrel(256, 1);
	CHECK(a4 != null && (( uintptr_t ) a4 & 255) == 0, "alignment may exceed allocation size");
}

static void named_arena_spec(void) {
	printf("\n=== arena: named arenas ===\n");
	int aid1 = mkarena();
	int aid2 = mkarena();
	CHECK(aid1 >= 1 && aid2 >= 2, "mkarena returns distinct IDs");

	char *p1 = ( char * ) aden(aid1, 32);
	char *p2 = ( char * ) aden(aid2, 32);
	strcpy(p1, "arena one");
	strcpy(p2, "arena two");
	CHECK(strcmp(p1, "arena one") == 0, "arena 1 data intact");
	CHECK(strcmp(p2, "arena two") == 0, "arena 2 data intact");

	rmarena(aid1);
	CHECK(strcmp(p2, "arena two") == 0, "arena 2 survives destruction of arena 1");
	rmarena(aid2);

	int aid_reuse = mkarena();
	CHECK(aid_reuse == aid1 || aid_reuse == aid2, "mkarena reuses destroyed descriptor");
	char *reuse_data = ( char * ) aden(aid_reuse, 32);
	CHECK(reuse_data != null, "reused arena allocates");
	if (reuse_data) strcpy(reuse_data, "reuse ok");
	CHECK(reuse_data && strcmp(reuse_data, "reuse ok") == 0, "reused arena data intact");
	rmarena(aid_reuse);

	rmarena(0);
	char *d = ( char * ) den(16);
	strcpy(d, "default ok");
	CHECK(strcmp(d, "default ok") == 0, "default arena cannot be destroyed");
}

static void descriptor_growth_spec(void) {
	printf("\n=== arena: descriptor growth ===\n");
	int  ids [COETUA_ARENA_TABLE_SEED + 20];
	bool ok = true;
	for (uvlong i = 0; i < arrlen(ids); i++) {
		ids [i] = mkarena();
		if (ids [i] < 0) {
			ok = false;
			break;
		}
		int *p = ( int * ) aden(ids [i], sizeof(int));
		if (!p) {
			ok = false;
			break;
		}
		*p = ( int ) i;
	}
	for (uvlong i = 0; i < arrlen(ids); i++)
		if (ids [i] >= 0) rmarena(ids [i]);
	CHECK(ok, "arena descriptors grow past seed size");
}

static void large_allocation_spec(void) {
	printf("\n=== arena: large allocations ===\n");
	void *big1 = den(512 * 1024);
	void *big2 = den(768 * 1024);
	CHECK(big1 != null, "den(512 KiB) non-null");
	CHECK(big2 != null, "den(768 KiB) non-null");
	if (big1 && big2) {
		memset(big1, 0xaa, 512 * 1024);
		memset(big2, 0xbb, 768 * 1024);
		CHECK((( uchar * ) big1) [0] == 0xaa, "512 KiB block data intact");
		CHECK((( uchar * ) big2) [0] == 0xbb, "768 KiB block data intact");
	}

	int  aid = mkarena();
	bool ok  = true;
	for (int i = 0; i < 5000; i++) {
		void *x = aden(aid, 512);
		if (!x) {
			ok = false;
			break;
		}
		memset(x, ( uchar ) i, 1);
	}
	CHECK(ok, "many small allocations fill multiple blocks");
	rmarena(aid);
}

static void error_spec(void) {
	printf("\n=== arena: error paths ===\n");
	CHECK(aden(-1, 16) == null && err(), "aden rejects negative arena");
	clears_error();

	int aid = mkarena();
	rmarena(aid);
	CHECK(aden(aid, 16) == null && err(), "aden rejects destroyed arena");
	clears_error();

	CHECK(sala((( uvlong ) -1 / 2) + 1, 2) == null && err(), "sala rejects multiplication overflow");
	clears_error();

	CHECK(carrel(16, ( uvlong ) -8) == null && err(), "carrel rejects size overflow");
	clears_error();
}

int main(void) {
	basic_spec();
	alignment_spec();
	named_arena_spec();
	descriptor_growth_spec();
	large_allocation_spec();
	error_spec();

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
