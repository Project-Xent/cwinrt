#include "silo_priv.h"
#include "arena.h"
#include "err.h"
#include "hash.h"
#include <string.h>

void  htab_touch(htab_t *t) { t->gen++; }

uchar htab_hash_meta(uvlong h) { return ( uchar ) (((h >> 1) & 0xfe) | 2); }

static bool inline meta_match(uchar meta, uvlong h) { return meta == htab_hash_meta(h); }

static uvlong htab_data_cap(uint used) { return used > 256 ? ( uvlong ) used : 256; }

static bool   htab_data_want(uint used, uint needed, uint *out) {
	uint want = used + needed;
	if (want < used) {
		errmsg("htab: data size overflow");
		return false;
	}
	*out = want;
	return true;
}

static bool htab_data_room(uint used, uint needed, uvlong *out) {
	uint want;
	if (!htab_data_want(used, needed, &want)) return false;
	uvlong seed = htab_data_cap(used);
	*out        = want <= seed ? seed : nextpow2_64(want);
	return true;
}

static bool htab_probe_match(htab_t *t, uvlong idx, void *key, uvlong klen, uvlong hash) {
	if (!htab_slot_live(t->meta [idx])) return false;
	if (!meta_match(t->meta [idx], hash)) return false;
	if (( uvlong ) t->klens [idx] != klen) return false;
	return memcmp(t->keys + t->koffs [idx], key, klen) == 0;
}

static uvlong htab_slot_for(uchar *meta, uvlong cap, void *key, uint klen) {
	uvlong h   = xxhash64(key, klen);
	uvlong idx = h & (cap - 1);
	while (htab_slot_live(meta [idx])) idx = (idx + 1) & (cap - 1);
	meta [idx] = htab_hash_meta(h);
	return idx;
}

static void htab_relocate_key(htab_t *t, uvlong src, uvlong dst, uchar *keys, uint *koffs, uint *klens, uint *write) {
	uint klen   = t->klens [src];
	koffs [dst] = *write;
	klens [dst] = klen;
	if (klen > 0) memcpy(keys + *write, t->keys + t->koffs [src], klen);
	*write += klen;
}

static void htab_relocate_val(htab_t *t, uvlong src, uvlong dst, uchar *vals, uint *voffs, uint *vlens, uint *write) {
	uint vlen   = t->vlens [src];
	voffs [dst] = *write;
	vlens [dst] = vlen;
	if (vlen > 0) memcpy(vals + *write, t->vals + t->voffs [src], vlen);
	*write += vlen;
}

htab_t *htab_get(int desc) {
	if (desc < 0) return null;
	int tag = desc_tag(desc);
	if (tag != silo_set && tag != silo_map && tag != silo_multiset) return null;
	int idx = desc_index(desc);
	if (idx < 0 || idx >= nhtabs || !htabs [idx].live) return null;
	htab_t *t = &htabs [idx];
	if (tag == silo_set && t->ismap) return null;
	if (tag == silo_map && (!t->ismap || t->ismultiset)) return null;
	if (tag == silo_multiset && !t->ismultiset) return null;
	return t;
}

int htab_new_desc(void) {
	for (int i = 0; i < nhtabs; i++) {
		if (htabs [i].live) continue;
		return i;
	}
	int id = nhtabs;
	if (!htab_desc_ensure(id + 1)) return -1;
	nhtabs = id + 1;
	return id;
}

void htab_init(htab_t *t, int arena, bool ismap) {
	uvlong gen = t->gen + 1;
	*t         = (htab_t) {.arena = arena, .ismap = ismap, .ismultiset = false, .live = true, .gen = gen};
}

void htab_clear(htab_t *t) {
	t->meta       = null;
	t->keys       = null;
	t->vals       = null;
	t->koffs      = null;
	t->klens      = null;
	t->voffs      = null;
	t->vlens      = null;
	t->cap        = 0;
	t->len        = 0;
	t->tombstones = 0;
	t->key_total  = 0;
	t->val_total  = 0;
	htab_touch(t);
	t->ismultiset = false;
	t->live       = false;
}

static bool htab_grow(htab_t *t) {
	uvlong newcap  = t->cap ? t->cap * 2 : 16;
	uchar *newmeta = ( uchar * ) asala(t->arena, newcap, 1);
	uint  *newkoff = ( uint * ) asala(t->arena, newcap, sizeof(uint));
	uint  *newklen = ( uint * ) asala(t->arena, newcap, sizeof(uint));
	uint  *newvoff = null;
	uint  *newvlen = null;
	if (!newmeta || !newkoff || !newklen) return false;
	memset(newkoff, 0, ( uvlong ) newcap * sizeof(uint));
	memset(newklen, 0, ( uvlong ) newcap * sizeof(uint));

	uvlong keys_sz = htab_data_cap(t->key_total);
	uchar *newkeys = ( uchar * ) aden(t->arena, keys_sz);
	uchar *newvals = null;
	if (t->ismap) {
		newvoff = ( uint * ) asala(t->arena, newcap, sizeof(uint));
		newvlen = ( uint * ) asala(t->arena, newcap, sizeof(uint));
		if (!newvoff || !newvlen) return false;
		memset(newvoff, 0, ( uvlong ) newcap * sizeof(uint));
		memset(newvlen, 0, ( uvlong ) newcap * sizeof(uint));
		uvlong vals_sz = htab_data_cap(t->val_total);
		newvals        = ( uchar * ) aden(t->arena, vals_sz);
		if (!newvals) return false;
	}
	if (!newkeys) return false;

	uint key_write = 0, val_write = 0;
	for (uvlong i = 0; i < t->cap; i++) {
		if (!htab_slot_live(t->meta [i])) continue;
		uvlong idx = htab_slot_for(newmeta, newcap, t->keys + t->koffs [i], t->klens [i]);
		htab_relocate_key(t, i, idx, newkeys, newkoff, newklen, &key_write);
		if (t->ismap) htab_relocate_val(t, i, idx, newvals, newvoff, newvlen, &val_write);
	}

	t->meta       = newmeta;
	t->koffs      = newkoff;
	t->klens      = newklen;
	t->keys       = newkeys;
	t->vals       = newvals;
	t->voffs      = newvoff;
	t->vlens      = newvlen;
	t->cap        = newcap;
	t->key_total  = key_write;
	t->val_total  = val_write;
	t->tombstones = 0;
	htab_touch(t);
	return true;
}

uvlong htab_probe(htab_t *t, void *key, uvlong klen, uvlong hash, bool *found) {
	*found = false;
	if (t->cap == 0) return 0;
	uvlong idx       = hash & (t->cap - 1);
	uvlong tombstone = ( uvlong ) -1;
	for (;; idx = (idx + 1) & (t->cap - 1)) {
		if (htab_slot_empty(t->meta [idx])) return tombstone != ( uvlong ) -1 ? tombstone : idx;
		if (t->meta [idx] == HASH_META_TOMBSTONE) {
			if (tombstone == ( uvlong ) -1) tombstone = idx;
			continue;
		}
		if (htab_probe_match(t, idx, key, klen, hash)) {
			*found = true;
			return idx;
		}
	}
}

bool htab_keys_grow(htab_t *t, uint needed) {
	uvlong cap;
	if (!htab_data_room(t->key_total, needed, &cap)) return false;
	uchar *nk = ( uchar * ) aden(t->arena, cap);
	if (!nk) return false;
	if (t->key_total) memcpy(nk, t->keys, t->key_total);
	t->keys = nk;
	return true;
}

bool htab_vals_grow(htab_t *t, uint needed) {
	if (!t->ismap) return true;
	uvlong cap;
	if (!htab_data_room(t->val_total, needed, &cap)) return false;
	uchar *nv = ( uchar * ) aden(t->arena, cap);
	if (!nv) return false;
	if (t->val_total) memcpy(nv, t->vals, t->val_total);
	t->vals = nv;
	return true;
}

bool htab_maybe_grow(htab_t *t) {
	if (t->cap == 0 || (t->len + t->tombstones) * HASH_LOAD_FACTOR_D >= t->cap * HASH_LOAD_FACTOR_N)
		return htab_grow(t);
	return true;
}

bool htab_compact(htab_t *t) {
	if (t->cap == 0 || t->len == 0) return true;

	uchar *newmeta = ( uchar * ) asala(t->arena, t->cap, 1);
	uint  *newkoff = ( uint * ) asala(t->arena, t->cap, sizeof(uint));
	uint  *newklen = ( uint * ) asala(t->arena, t->cap, sizeof(uint));
	uint  *newvoff = null;
	uint  *newvlen = null;
	uchar *newkeys = null;
	uchar *newvals = null;
	if (!newmeta || !newkoff || !newklen) return false;
	if (t->key_total > 0) {
		newkeys = ( uchar * ) aden(t->arena, t->key_total);
		if (!newkeys) return false;
	}
	if (t->ismap) {
		newvoff = ( uint * ) asala(t->arena, t->cap, sizeof(uint));
		newvlen = ( uint * ) asala(t->arena, t->cap, sizeof(uint));
		if (!newvoff || !newvlen) return false;
		if (t->val_total > 0) {
			newvals = ( uchar * ) aden(t->arena, t->val_total);
			if (!newvals) return false;
		}
	}

	uint key_write = 0;
	uint val_write = 0;
	for (uvlong i = 0; i < t->cap; i++) {
		if (!htab_slot_live(t->meta [i])) continue;
		uvlong idx = htab_slot_for(newmeta, t->cap, t->keys + t->koffs [i], t->klens [i]);
		htab_relocate_key(t, i, idx, newkeys, newkoff, newklen, &key_write);
		if (t->ismap) htab_relocate_val(t, i, idx, newvals, newvoff, newvlen, &val_write);
	}

	t->meta       = newmeta;
	t->koffs      = newkoff;
	t->klens      = newklen;
	t->keys       = newkeys;
	t->voffs      = newvoff;
	t->vlens      = newvlen;
	t->vals       = newvals;
	t->key_total  = key_write;
	t->val_total  = val_write;
	t->tombstones = 0;
	htab_touch(t);
	return true;
}
