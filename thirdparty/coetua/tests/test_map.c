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

int main(void) {
	printf("=== map: basic insert/lookup/oblit ===\n");
	int m = mkmap(0);
	CHECK(m >= 0, "mkmap");

	/* insert and look up */
	insert(m, "name", 4, "Coetua", 6);
	insert(m, "lang", 4, "C17", 3);
	insert(m, "year", 4, "2026", 4);

	char   buf [128];
	uvlong vlen;

	vlen = sizeof(buf);
	CHECK(lookup(m, "name", 4, buf, &vlen) && vlen == 6 && memcmp(buf, "Coetua", 6) == 0, "lookup name");
	vlen = sizeof(buf);
	CHECK(lookup(m, "lang", 4, buf, &vlen) && vlen == 3 && memcmp(buf, "C17", 3) == 0, "lookup lang");
	vlen = sizeof(buf);
	CHECK(lookup(m, "year", 4, buf, &vlen) && vlen == 4 && memcmp(buf, "2026", 4) == 0, "lookup year");

	/* delete and verify gone */
	CHECK(oblit(m, "lang", 4), "oblit lang");
	vlen = sizeof(buf);
	CHECK(!lookup(m, "lang", 4, buf, &vlen) && !err(), "lookup after oblit returns false quietly");

	/* delete non-existent */
	CHECK(!oblit(m, "nonexistent", 11) && !err(), "oblit non-existent returns false quietly");

	/* remaining keys still accessible */
	vlen = sizeof(buf);
	CHECK(lookup(m, "name", 4, buf, &vlen) && vlen == 6 && memcmp(buf, "Coetua", 6) == 0,
	      "lookup name after deleting lang");

	/* re-insert deleted key */
	insert(m, "lang", 4, "C23", 3);
	vlen = sizeof(buf);
	CHECK(lookup(m, "lang", 4, buf, &vlen) && vlen == 3 && memcmp(buf, "C23", 3) == 0, "re-insert after oblit");

	printf("\n=== map: update existing key ===\n");
	insert(m, "name", 4, "Coetua-v2", 9);
	vlen = sizeof(buf);
	CHECK(lookup(m, "name", 4, buf, &vlen) && vlen == 9 && memcmp(buf, "Coetua-v2", 9) == 0, "update name");

	/* update with different length */
	insert(m, "name", 4, "X", 1);
	vlen = sizeof(buf);
	CHECK(lookup(m, "name", 4, buf, &vlen) && vlen == 1 && memcmp(buf, "X", 1) == 0, "update name to shorter value");

	printf("\n=== map: empty value ===\n");
	insert(m, "empty", 5, "", 0);
	vlen = sizeof(buf);
	CHECK(lookup(m, "empty", 5, buf, &vlen) && vlen == 0, "lookup empty value");
	CHECK(oblit(m, "empty", 5), "oblit empty value");

	printf("\n=== map: growth and probing preserve entries ===\n");
	int  m2 = mkmap(0);
	char key [16], val [16];
	for (int i = 0; i < 128; i++) {
		sprintf(key, "key-%04d", i);
		sprintf(val, "val-%04d", i);
		insert(m2, key, strlen(key), val, strlen(val));
	}
	for (int i = 0; i < 128; i++) {
		sprintf(key, "key-%04d", i);
		sprintf(val, "val-%04d", i);
		vlen    = sizeof(buf);
		bool ok = lookup(m2, key, strlen(key), buf, &vlen);
		if (!ok) {
			printf("FAIL: lookup %s\n", key);
			failures++;
			continue;
		}
		if (vlen != strlen(val) || memcmp(buf, val, vlen) != 0) {
			printf("FAIL: %s value mismatch (got %.*s, expected %s)\n", key, ( int ) vlen, buf, val);
			failures++;
		}
	}
	printf("  ok: grown map lookup all correct\n");

	int del_ok = 0;
	for (int i = 0; i < 64; i++) {
		sprintf(key, "key-%04d", i);
		if (oblit(m2, key, strlen(key))) del_ok++;
	}
	CHECK(del_ok == 64, "deleted first half after growth");
	int remain_ok = 0;
	for (int i = 64; i < 128; i++) {
		sprintf(key, "key-%04d", i);
		if (lookup(m2, key, strlen(key), buf, &vlen)) remain_ok++;
	}
	CHECK(remain_ok == 64, "remaining half intact");
	int gone_ok = 0;
	for (int i = 0; i < 64; i++) {
		sprintf(key, "key-%04d", i);
		if (!lookup(m2, key, strlen(key), buf, &vlen)) gone_ok++;
	}
	CHECK(gone_ok == 64, "deleted half truly gone");

	rmmap(m2);
	rmmap(m);

	printf("\n=== map: tombstone reuse ===\n");
	int m4 = mkmap(0);
	/* Insert, delete, reinsert at same slot */
	for (int i = 0; i < 50; i++) {
		sprintf(key, "tk%d", i);
		insert(m4, key, strlen(key), key, strlen(key));
	}
	for (int i = 0; i < 25; i++) {
		sprintf(key, "tk%d", i);
		oblit(m4, key, strlen(key));
	}
	/* Reinsert deleted keys */
	for (int i = 0; i < 25; i++) {
		sprintf(key, "tk%d", i);
		insert(m4, key, strlen(key), "NEW", 3);
	}
	/* Verify */
	for (int i = 0; i < 25; i++) {
		sprintf(key, "tk%d", i);
		vlen = sizeof(buf);
		if (!lookup(m4, key, strlen(key), buf, &vlen) || vlen != 3 || memcmp(buf, "NEW", 3) != 0) {
			printf("FAIL: tombstone reinsert %s\n", key);
			failures++;
		}
	}
	printf("  ok: tombstone reuse (25 delete+reinsert)\n");
	compact(m4);
	for (int i = 0; i < 50; i++) {
		sprintf(key, "tk%d", i);
		vlen    = sizeof(buf);
		bool ok = lookup(m4, key, strlen(key), buf, &vlen);
		if (i < 25) {
			if (!ok || vlen != 3 || memcmp(buf, "NEW", 3) != 0) {
				printf("FAIL: compact preserved reinserted %s\n", key);
				failures++;
			}
		}
		else if (!ok || vlen != strlen(key) || memcmp(buf, key, vlen) != 0) {
			printf("FAIL: compact preserved untouched %s\n", key);
			failures++;
		}
	}
	printf("  ok: compact preserves tombstone-heavy map lookups\n");
	rmmap(m4);

	printf("\n=== map: binary keys and values ===\n");
	int   m5          = mkmap(0);
	uchar bin_key [8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
	uchar bin_val [4] = {0xff, 0xee, 0xdd, 0xcc};
	insert(m5, bin_key, 8, bin_val, 4);
	vlen = sizeof(buf);
	CHECK(lookup(m5, bin_key, 8, buf, &vlen) && vlen == 4 && memcmp(buf, bin_val, 4) == 0, "binary key/val");
	/* Key with embedded NUL */
	uchar nul_key [5] = {'a', 0, 'b', 0, 'c'};
	insert(m5, nul_key, 5, "yes", 3);
	vlen = sizeof(buf);
	CHECK(lookup(m5, nul_key, 5, buf, &vlen) && vlen == 3 && memcmp(buf, "yes", 3) == 0, "key with embedded NUL");
	uchar zero_key [4] = {0, 0, 0, 0};
	uchar zero_val [3] = {0, 1, 0};
	insert(m5, zero_key, sizeof(zero_key), zero_val, sizeof(zero_val));
	vlen = sizeof(buf);
	CHECK(lookup(m5, zero_key, sizeof(zero_key), buf, &vlen)
	        && vlen == sizeof(zero_val)
	        && memcmp(buf, zero_val, sizeof(zero_val)) == 0,
	      "all-zero binary key/value");
	insert(m5, null, 0, null, 0);
	vlen = sizeof(buf);
	CHECK(lookup(m5, null, 0, buf, &vlen) && vlen == 0 && !err(), "null zero-length key/value is valid");
	rmmap(m5);

	printf("\n=== map: lookup buf too small ===\n");
	int m6 = mkmap(0);
	insert(m6, "long", 4, "this is a long value", 20);
	char small [5];
	vlen = 5;
	CHECK(lookup(m6, "long", 4, small, &vlen) && vlen == 20, "lookup with small buf returns full length");
	CHECK(memcmp(small, "this ", 5) == 0, "small buf gets truncated content");
	rmmap(m6);

	printf("\n=== map: revamp ===\n");
	int m7 = mkmap(0);
	insert(m7, "keep", 4, "old", 3);
	revamp(m7, "keep", 4, "new", 3);
	vlen = sizeof(buf);
	CHECK(lookup(m7, "keep", 4, buf, &vlen) && vlen == 3 && memcmp(buf, "new", 3) == 0, "revamp updates existing key");
	revamp(m7, "absent", 6, "no", 2);
	vlen = sizeof(buf);
	CHECK(!lookup(m7, "absent", 6, buf, &vlen) && !err(), "revamp does not insert absent key quietly");
	rmmap(m7);

	printf("\n=== map: conjoin ===\n");
	int left  = mkmap(0);
	int right = mkmap(0);
	insert(left, "a", 1, "L-a", 3);
	insert(left, "b", 1, "L-b", 3);
	insert(right, "b", 1, "R-b", 3);
	insert(right, "c", 1, "R-c", 3);
	int inner = replica(0, left);
	conjoin(inner, right, 0);
	CHECK(!lookup(inner, "a", 1, buf, &vlen), "conjoin inner drops left-only");
	vlen = sizeof(buf);
	CHECK(lookup(inner, "b", 1, buf, &vlen) && vlen == 3 && memcmp(buf, "R-b", 3) == 0,
	      "conjoin inner keeps shared from right");
	CHECK(!lookup(inner, "c", 1, buf, &vlen), "conjoin inner drops right-only");

	int outer = replica(0, left);
	conjoin(outer, right, 1);
	vlen = sizeof(buf);
	CHECK(lookup(outer, "a", 1, buf, &vlen) && vlen == 3 && memcmp(buf, "L-a", 3) == 0, "conjoin full keeps left-only");
	vlen = sizeof(buf);
	CHECK(lookup(outer, "b", 1, buf, &vlen) && vlen == 3 && memcmp(buf, "R-b", 3) == 0,
	      "conjoin full overwrites shared");
	vlen = sizeof(buf);
	CHECK(lookup(outer, "c", 1, buf, &vlen) && vlen == 3 && memcmp(buf, "R-c", 3) == 0, "conjoin full adds right-only");

	int self = replica(0, left);
	conjoin(self, right, 2);
	vlen = sizeof(buf);
	CHECK(lookup(self, "a", 1, buf, &vlen) && vlen == 3 && memcmp(buf, "L-a", 3) == 0, "conjoin left keeps left-only");
	vlen = sizeof(buf);
	CHECK(lookup(self, "b", 1, buf, &vlen) && vlen == 3 && memcmp(buf, "R-b", 3) == 0,
	      "conjoin left overwrites shared");
	CHECK(!lookup(self, "c", 1, buf, &vlen), "conjoin left ignores right-only");
	conjoin(self, right, 99);
	check_expected_error(true, "conjoin rejects bad method");
	rmmap(left);
	rmmap(right);
	rmmap(inner);
	rmmap(outer);
	rmmap(self);

	printf("\n=== set: binary and compact stress ===\n");
	int ss = mkset(0);
	CHECK(ss >= 0, "mkset for binary stress");
	uchar skey [6] = {'s', 0, 'e', 0, 't', 0};
	adds(ss, skey, sizeof(skey));
	CHECK(mems(ss, skey, sizeof(skey)), "set binary key with embedded NUL");
	for (int i = 0; i < 120; i++) {
		sprintf(key, "set-%03d", i);
		adds(ss, key, strlen(key));
	}
	for (int i = 0; i < 120; i += 3) {
		sprintf(key, "set-%03d", i);
		dels(ss, key, strlen(key));
	}
	compact(ss);
	bool set_ok = mems(ss, skey, sizeof(skey));
	for (int i = 0; i < 120; i++) {
		sprintf(key, "set-%03d", i);
		bool has = mems(ss, key, strlen(key));
		if ((i % 3) == 0) {
			if (has) set_ok = false;
		}
		else if (!has) set_ok = false;
	}
	CHECK(set_ok, "set compact preserves live binary/string keys and deleted keys stay gone");
	adds(ss, null, 0);
	CHECK(mems(ss, null, 0) && !err(), "set accepts null zero-length key");
	dels(ss, "missing", 7);
	CHECK(!err(), "dels missing key is quiet");
	check_expected_error(!mems(-1, "x", 1), "mems invalid descriptor sets error");
	adds(ss, null, 1);
	check_expected_error(true, "adds null nonzero key sets error");
	dels(ss, null, 1);
	check_expected_error(true, "dels null nonzero key sets error");
	rmset(ss);

	printf("\n=== map: errors ===\n");
	m    = mkmap(0);
	vlen = sizeof(buf);
	check_expected_error(!lookup(-1, "k", 1, buf, &vlen), "lookup invalid descriptor sets error");
	insert(-1, "k", 1, "v", 1);
	check_expected_error(true, "insert invalid descriptor sets error");
	insert(m, null, 1, "v", 1);
	check_expected_error(true, "insert null nonzero key sets error");
	insert(m, "k", 1, null, 1);
	check_expected_error(true, "insert null nonzero value sets error");
	check_expected_error(!lookup(m, null, 1, buf, &vlen), "lookup null nonzero key sets error");
	check_expected_error(!oblit(m, null, 1), "oblit null nonzero key sets error");
	revamp(m, null, 1, "v", 1);
	check_expected_error(true, "revamp null nonzero key sets error");
	revamp(m, "k", 1, null, 1);
	check_expected_error(true, "revamp null nonzero value sets error");
	rmmap(m);

	printf("\n=== result: %d failures ===\n", failures);
	return failures;
}
