#include "silo_priv.h"
#include "err.h"
#include "hash.h"
#include <string.h>

static uchar   empty_data;

/* Multisets store counts in the map value area and intentionally reuse
 * insert/lookup/oblit/revamp.  Plain map-only operations use getmap. */
static htab_t *getkv(int map) {
	htab_t *t = htab_get(map);
	return t && t->ismap ? t : null;
}

static htab_t *needkv(int map, char *who) {
	htab_t *t = getkv(map);
	if (!t) errmsg(who);
	return t;
}

static htab_t *getmap(int map) {
	htab_t *t = htab_get(map);
	return t && t->ismap && !t->ismultiset ? t : null;
}

static htab_t *needmap(int map, char *who) {
	htab_t *t = getmap(map);
	if (!t) errmsg(who);
	return t;
}

static void *bytes_arg(void *p, uvlong len, char *who) {
	if (p || len == 0) return p ? p : &empty_data;
	errmsg(who);
	return null;
}

static uchar *map_key(htab_t *t, uvlong idx, uint *len) {
	*len = t->klens [idx];
	return t->keys + t->koffs [idx];
}

static uchar *map_val(htab_t *t, uvlong idx, uint *len) {
	*len = t->vlens [idx];
	return t->vals + t->voffs [idx];
}

static void map_write_pair(htab_t *t, uvlong idx, uvlong hash, void *key, uvlong klen, void *val, uvlong vlen) {
	if (t->meta [idx] == HASH_META_TOMBSTONE) t->tombstones--;
	t->meta [idx]  = htab_hash_meta(hash);
	t->koffs [idx] = t->key_total;
	t->klens [idx] = ( uint ) klen;
	if (klen) memcpy(t->keys + t->key_total, key, klen);
	t->key_total   += ( uint ) klen;

	t->voffs [idx]  = t->val_total;
	t->vlens [idx]  = ( uint ) vlen;
	if (vlen) memcpy(t->vals + t->val_total, val, vlen);
	t->val_total += ( uint ) vlen;
}

static void map_delete_slot(htab_t *t, uvlong idx) {
	htab_slot_kill(t, idx);
	htab_touch(t);
}

int mkmap(int arena) {
	int id = htab_new_desc();
	if (id < 0) return -1;
	htab_init(&htabs [id], arena, true);
	return make_desc(id, silo_map);
}

void insert(int map, void *key, uvlong klen, void *val, uvlong vlen) {
	htab_t *t = needkv(map, "insert: bad map");
	if (!t) return;
	key = bytes_arg(key, klen, "insert: bad key");
	if (!key) return;
	val = bytes_arg(val, vlen, "insert: bad value");
	if (!val) return;
	if (!htab_maybe_grow(t)) return;
	uvlong h = xxhash64(key, klen);
	bool   found;
	uvlong idx = htab_probe(t, key, klen, h, &found);

	if (!htab_keys_grow(t, ( uint ) klen)) return;
	if (!htab_vals_grow(t, ( uint ) vlen)) return;

	if (!found) t->len++;

	map_write_pair(t, idx, h, key, klen, val, vlen);
	htab_touch(t);
}

bool lookup(int map, void *key, uvlong klen, void *buf, uvlong *vlen) {
	htab_t *t = needkv(map, "lookup: bad map");
	if (!t) return false;
	key = bytes_arg(key, klen, "lookup: bad key");
	if (!key) return false;
	if (t->cap == 0) return false;
	uvlong h = xxhash64(key, klen);
	bool   found;
	uvlong idx = htab_probe(t, key, klen, h, &found);
	if (!found) return false;
	if (buf && vlen) {
		uint   vl;
		uchar *vp   = map_val(t, idx, &vl);
		uvlong copy = *vlen < vl ? *vlen : vl;
		memcpy(buf, vp, copy);
		*vlen = vl;
	}
	return true;
}

bool oblit(int map, void *key, uvlong klen) {
	htab_t *t = needkv(map, "oblit: bad map");
	if (!t) return false;
	key = bytes_arg(key, klen, "oblit: bad key");
	if (!key) return false;
	if (t->cap == 0) return false;
	uvlong h = xxhash64(key, klen);
	bool   found;
	uvlong idx = htab_probe(t, key, klen, h, &found);
	if (!found) return false;
	map_delete_slot(t, idx);
	return true;
}

void revamp(int map, void *key, uvlong klen, void *val, uvlong vlen) {
	htab_t *t = needkv(map, "revamp: bad map");
	if (!t) return;
	key = bytes_arg(key, klen, "revamp: bad key");
	if (!key) return;
	val = bytes_arg(val, vlen, "revamp: bad value");
	if (!val) return;
	if (t->cap == 0) return;
	uvlong h = xxhash64(key, klen);
	bool   found;
	htab_probe(t, key, klen, h, &found);
	if (!found) return;
	insert(map, key, klen, val, vlen);
}

void conjoin(int dst, int src, int method) {
	if (method < 0 || method > 2) {
		errmsg("conjoin: bad method");
		return;
	}
	htab_t *d = needmap(dst, "conjoin: bad map");
	if (!d) return;
	htab_t *s = needmap(src, "conjoin: bad map");
	if (!s) return;
	if (d == s) return;
	if (method == 0) {
		for (uvlong i = 0; i < d->cap; i++) {
			if (!htab_slot_live(d->meta [i])) continue;
			uint   klen;
			uchar *key  = map_key(d, i, &klen);
			uvlong vlen = 0;
			if (!lookup(src, key, klen, null, &vlen)) map_delete_slot(d, i);
		}
	}
	for (uvlong i = 0; i < s->cap; i++) {
		if (!htab_slot_live(s->meta [i])) continue;
		uint   klen;
		uchar *key = map_key(s, i, &klen);
		bool   in_dst;
		htab_probe(d, key, klen, xxhash64(key, klen), &in_dst);
		if (method == 2 && !in_dst) continue;
		if (method == 0 && !in_dst) continue;
		uint   vlen;
		uchar *val = map_val(s, i, &vlen);
		insert(dst, key, klen, val, vlen);
	}
}

void rmmap(int map) {
	htab_t *t = getmap(map);
	if (t) htab_clear(t);
}
