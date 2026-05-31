#include "silo_priv.h"
#include "err.h"
#include "hash.h"
#include <string.h>

static uchar   empty_key;

static htab_t *set_get(int set) { return htab_get(set); }

static htab_t *set_need(int set, char *who) {
	htab_t *t = set_get(set);
	if (!t) errmsg(who);
	return t;
}

static void *key_bytes(void *data, uvlong len, char *who) {
	if (data || len == 0) return data ? data : &empty_key;
	errmsg(who);
	return null;
}

static bool set_contains(htab_t *t, void *data, uvlong len) {
	if (!t || t->cap == 0) return false;
	bool   found;
	uvlong h = xxhash64(data, len);
	htab_probe(t, data, len, h, &found);
	return found;
}

static uchar *set_key(htab_t *t, uvlong idx, uint *len) {
	*len = t->klens [idx];
	return t->keys + t->koffs [idx];
}

static void set_add_slot(htab_t *t, uvlong idx, uvlong hash, void *data, uvlong len) {
	if (t->meta [idx] == HASH_META_TOMBSTONE) t->tombstones--;
	t->meta [idx]  = htab_hash_meta(hash);
	t->koffs [idx] = t->key_total;
	t->klens [idx] = ( uint ) len;
	if (len) memcpy(t->keys + t->key_total, data, len);
	t->key_total += ( uint ) len;
	t->len++;
	htab_touch(t);
}

static void set_delete_slot(htab_t *t, uvlong idx) {
	htab_slot_kill(t, idx);
	htab_touch(t);
}

static bool set_next_key(htab_t *t, uvlong *pos, uchar **key, uint *len) {
	while (*pos < t->cap && !htab_slot_live(t->meta [*pos])) (*pos)++;
	if (*pos >= t->cap) return false;
	*key = set_key(t, *pos, len);
	(*pos)++;
	return true;
}

static void set_add_live_entries(int dst, htab_t *src) {
	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (set_next_key(src, &pos, &key, &klen)) adds(dst, key, klen);
}

int mkset(int arena) {
	int id = htab_new_desc();
	if (id < 0) return -1;
	htab_init(&htabs [id], arena, false);
	return make_desc(id, silo_set);
}

void adds(int set, void *data, uvlong len) {
	htab_t *t = set_need(set, "adds: bad set");
	if (!t) return;
	data = key_bytes(data, len, "adds: bad key");
	if (!data) return;
	if (!htab_maybe_grow(t)) return;
	uvlong h = xxhash64(data, len);
	bool   found;
	uvlong idx = htab_probe(t, data, len, h, &found);
	if (found) return;
	if (!htab_keys_grow(t, ( uint ) len)) return;
	set_add_slot(t, idx, h, data, len);
}

void dels(int set, void *data, uvlong len) {
	htab_t *t = set_need(set, "dels: bad set");
	if (!t) return;
	data = key_bytes(data, len, "dels: bad key");
	if (!data) return;
	if (t->cap == 0) return;
	uvlong h = xxhash64(data, len);
	bool   found;
	uvlong idx = htab_probe(t, data, len, h, &found);
	if (!found) return;
	set_delete_slot(t, idx);
}

bool mems(int set, void *data, uvlong len) {
	htab_t *t = set_need(set, "mems: bad set");
	if (!t) return false;
	data = key_bytes(data, len, "mems: bad key");
	if (!data) return false;
	return set_contains(t, data, len);
}

void rmset(int set) {
	htab_t *t = set_get(set);
	if (t) htab_clear(t);
}

int cartesprod(int arena, int a, int b) {
	htab_t *sa = set_need(a, "cartesprod: bad set");
	if (!sa) return -1;
	htab_t *sb = set_need(b, "cartesprod: bad set");
	if (!sb) return -1;
	int prod = mkset(arena);
	if (prod < 0) return -1;
	for (uvlong i = 0; i < sa->cap; i++) {
		if (!htab_slot_live(sa->meta [i])) continue;
		uint   alen;
		uchar *ak = set_key(sa, i, &alen);
		for (uvlong j = 0; j < sb->cap; j++) {
			if (!htab_slot_live(sb->meta [j])) continue;
			uint   blen;
			uchar *bk         = set_key(sb, j, &blen);
			uvlong fields [4] = {alen, ( uvlong ) ak, blen, ( uvlong ) bk};
			adds(prod, fields, sizeof(fields));
		}
	}
	return prod;
}

void unions(int dst, int src) {
	htab_t *sd = set_need(dst, "unions: bad set");
	if (!sd) return;
	htab_t *ss = set_need(src, "unions: bad set");
	if (!ss) return;
	if (sd == ss) return;

	set_add_live_entries(dst, ss);
}

void intxns(int dst, int src) {
	htab_t *sd = set_need(dst, "intxns: bad set");
	if (!sd) return;
	htab_t *ss = set_need(src, "intxns: bad set");
	if (!ss) return;
	if (sd == ss) return;

	for (uvlong i = 0; i < sd->cap; i++) {
		if (!htab_slot_live(sd->meta [i])) continue;
		uint   klen;
		uchar *key = set_key(sd, i, &klen);
		if (!set_contains(ss, key, klen)) set_delete_slot(sd, i);
	}
}

void diffs(int dst, int src) {
	htab_t *sd = set_need(dst, "diffs: bad set");
	if (!sd) return;
	htab_t *ss = set_need(src, "diffs: bad set");
	if (!ss) return;
	if (sd == ss) {
		teem(dst);
		return;
	}

	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (set_next_key(ss, &pos, &key, &klen)) dels(dst, key, klen);
}

void symmdiffs(int dst, int src) {
	htab_t *sd = set_need(dst, "symmdiffs: bad set");
	if (!sd) return;
	htab_t *ss = set_need(src, "symmdiffs: bad set");
	if (!ss) return;
	if (sd == ss) {
		teem(dst);
		return;
	}

	int tmp = mkset(sd->arena);
	if (tmp < 0) return;
	set_add_live_entries(tmp, sd);

	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (set_next_key(ss, &pos, &key, &klen)) dels(dst, key, klen);
	pos = 0;
	while (set_next_key(ss, &pos, &key, &klen))
		if (!mems(tmp, key, klen)) adds(dst, key, klen);
	rmset(tmp);
}

bool subsets(int seta, int setb) {
	htab_t *sa = set_need(seta, "subsets: bad set");
	if (!sa) return false;
	htab_t *sb = set_need(setb, "subsets: bad set");
	if (!sb) return false;
	if (sa == sb) return true;

	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (set_next_key(sa, &pos, &key, &klen))
		if (!set_contains(sb, key, klen)) return false;
	return true;
}
