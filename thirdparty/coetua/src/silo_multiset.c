#include "silo_priv.h"
#include "err.h"
#include <string.h>

static uchar   empty_mkey;

static htab_t *mset_get(int ms) { return htab_get(ms); }

static htab_t *mset_need(int ms, char *who) {
	htab_t *t = mset_get(ms);
	if (!t) errmsg(who);
	return t;
}

static void *mkey_bytes(void *data, uvlong len, char *who) {
	if (data || len == 0) return data ? data : &empty_mkey;
	errmsg(who);
	return null;
}

static uchar *mset_key(htab_t *t, uvlong idx, uint *len) {
	*len = t->klens [idx];
	return t->keys + t->koffs [idx];
}

static bool mset_next_key(htab_t *t, uvlong *pos, uchar **key, uint *len) {
	while (*pos < t->cap && !htab_slot_live(t->meta [*pos])) (*pos)++;
	if (*pos >= t->cap) return false;
	*key = mset_key(t, *pos, len);
	(*pos)++;
	return true;
}

static void mset_set_count(int ms, void *data, uvlong len, uvlong cnt) {
	if (cnt == 0) oblit(ms, data, len);
	else insert(ms, data, len, &cnt, sizeof(cnt));
}

static void mset_add_count(int dst, void *data, uvlong len, uvlong add) {
	uvlong cur = cntms(dst, data, len);
	uvlong cnt;
	if (!addok64(cur, add, &cnt)) {
		errmsg("multiset: count overflow");
		return;
	}
	mset_set_count(dst, data, len, cnt);
}

static uvlong mset_min(uvlong a, uvlong b) { return a < b ? a : b; }

static uvlong mset_sub(uvlong a, uvlong b) { return a > b ? a - b : 0; }

static uvlong mset_absdiff(uvlong a, uvlong b) { return a > b ? a - b : b - a; }

int           mkmultiset(int arena) {
	int id = htab_new_desc();
	if (id < 0) return -1;
	htab_init(&htabs [id], arena, true);
	htabs [id].ismultiset = true;
	return make_desc(id, silo_multiset);
}

uvlong cntms(int ms, void *data, uvlong len) {
	if (!mset_need(ms, "cntms: bad multiset")) return 0;
	data = mkey_bytes(data, len, "cntms: bad key");
	if (!data) return 0;
	uvlong cnt  = 0;
	uvlong clen = sizeof(cnt);
	if (!lookup(ms, data, len, &cnt, &clen) || clen != sizeof(cnt)) return 0;
	return cnt;
}

bool memms(int ms, void *data, uvlong len) { return cntms(ms, data, len) > 0; }

void addms(int ms, void *data, uvlong len) {
	if (!mset_need(ms, "addms: bad multiset")) return;
	data = mkey_bytes(data, len, "addms: bad key");
	if (!data) return;
	uvlong cnt;
	if (!addok64(cntms(ms, data, len), 1, &cnt)) {
		errmsg("addms: count overflow");
		return;
	}
	mset_set_count(ms, data, len, cnt);
}

void delms(int ms, void *data, uvlong len) {
	if (!mset_need(ms, "delms: bad multiset")) return;
	data = mkey_bytes(data, len, "delms: bad key");
	if (!data) return;
	uvlong cnt = cntms(ms, data, len);
	if (cnt == 0) return;
	if (cnt == 1) {
		oblit(ms, data, len);
		return;
	}
	mset_set_count(ms, data, len, cnt - 1);
}

void prgms(int ms, void *data, uvlong len) {
	if (!mset_need(ms, "prgms: bad multiset")) return;
	data = mkey_bytes(data, len, "prgms: bad key");
	if (!data) return;
	oblit(ms, data, len);
}

void addtums(int dst, int src) {
	htab_t *d = mset_need(dst, "addtums: bad multiset");
	if (!d) return;
	htab_t *s = mset_need(src, "addtums: bad multiset");
	if (!s || d == s) return;
	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (mset_next_key(s, &pos, &key, &klen)) {
		uvlong add = 0, clen = sizeof(add);
		lookup(src, key, klen, &add, &clen);
		mset_add_count(dst, key, klen, add);
		if (err()) return;
	}
}

void unionms(int dst, int src) {
	htab_t *d = mset_need(dst, "unionms: bad multiset");
	if (!d) return;
	htab_t *s = mset_need(src, "unionms: bad multiset");
	if (!s || d == s) return;
	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (mset_next_key(s, &pos, &key, &klen)) {
		uvlong sc = cntms(src, key, klen);
		uvlong dc = cntms(dst, key, klen);
		if (sc > dc) mset_set_count(dst, key, klen, sc);
		if (err()) return;
	}
}

void intxnms(int dst, int src) {
	htab_t *d = mset_need(dst, "intxnms: bad multiset");
	if (!d) return;
	htab_t *s = mset_need(src, "intxnms: bad multiset");
	if (!s) return;
	if (d == s) return;
	for (uvlong i = 0; i < d->cap; i++) {
		if (!htab_slot_live(d->meta [i])) continue;
		uint   klen;
		uchar *key = mset_key(d, i, &klen);
		uvlong dc  = cntms(dst, key, klen);
		uvlong sc  = cntms(src, key, klen);
		mset_set_count(dst, key, klen, mset_min(dc, sc));
		if (err()) return;
	}
}

void diffms(int dst, int src) {
	htab_t *d = mset_need(dst, "diffms: bad multiset");
	if (!d) return;
	htab_t *s = mset_need(src, "diffms: bad multiset");
	if (!s) return;
	if (d == s) {
		teem(dst);
		return;
	}
	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (mset_next_key(s, &pos, &key, &klen)) {
		uvlong dc = cntms(dst, key, klen);
		uvlong sc = cntms(src, key, klen);
		mset_set_count(dst, key, klen, mset_sub(dc, sc));
		if (err()) return;
	}
}

void symmdiffms(int dst, int src) {
	htab_t *d = mset_need(dst, "symmdiffms: bad multiset");
	if (!d) return;
	htab_t *s = mset_need(src, "symmdiffms: bad multiset");
	if (!s) return;
	if (d == s) {
		teem(dst);
		return;
	}
	int tmp = mkmultiset(d->arena);
	if (tmp < 0) return;
	for (uvlong i = 0; i < d->cap; i++) {
		if (!htab_slot_live(d->meta [i])) continue;
		uint   klen;
		uchar *key = mset_key(d, i, &klen);
		uvlong dc  = cntms(dst, key, klen);
		uvlong sc  = cntms(src, key, klen);
		mset_set_count(tmp, key, klen, mset_absdiff(dc, sc));
		if (err()) return;
	}
	uvlong pos = 0;
	uint   klen;
	uchar *key;
	while (mset_next_key(s, &pos, &key, &klen)) {
		if (cntms(dst, key, klen) == 0) mset_set_count(tmp, key, klen, cntms(src, key, klen));
		if (err()) return;
	}
	teem(dst);
	addtums(dst, tmp);
	rmmultiset(tmp);
}

bool submultisets(int a, int b) {
	htab_t *ma = mset_need(a, "submultisets: bad multiset");
	if (!ma) return false;
	htab_t *mb = mset_need(b, "submultisets: bad multiset");
	if (!mb) return false;
	if (ma == mb) return true;
	for (uvlong i = 0; i < ma->cap; i++) {
		if (!htab_slot_live(ma->meta [i])) continue;
		uint   klen;
		uchar *key = mset_key(ma, i, &klen);
		if (cntms(a, key, klen) > cntms(b, key, klen)) return false;
	}
	return true;
}

bool simsubmss(int a, int b, int deviat, int excess) {
	htab_t *ma = mset_need(a, "simsubmss: bad multiset");
	if (!ma) return false;
	htab_t *mb = mset_need(b, "simsubmss: bad multiset");
	if (!mb) return false;
	uvlong total = 0;
	uvlong pos   = 0;
	uint   klen;
	uchar *key;
	while (mset_next_key(ma, &pos, &key, &klen)) {
		uvlong ac = cntms(a, key, klen);
		uvlong bc = cntms(b, key, klen);
		if (ac <= bc) continue;
		uvlong over = ac - bc;
		if (deviat >= 0 && over > ( uvlong ) deviat) return false;
		if (!addok64(total, over, &total)) {
			errmsg("simsubmss: excess overflow");
			return false;
		}
		if (excess >= 0 && total > ( uvlong ) excess) return false;
	}
	return true;
}

int cartprodms(int arena, int a, int b) {
	htab_t *ma = mset_need(a, "cartprodms: bad multiset");
	if (!ma) return -1;
	htab_t *mb = mset_need(b, "cartprodms: bad multiset");
	if (!mb) return -1;
	int prod = mkmultiset(arena);
	if (prod < 0) return -1;
	for (uvlong i = 0; i < ma->cap; i++) {
		if (!htab_slot_live(ma->meta [i])) continue;
		uint   alen;
		uchar *ak = mset_key(ma, i, &alen);
		uvlong ac = cntms(a, ak, alen);
		for (uvlong j = 0; j < mb->cap; j++) {
			if (!htab_slot_live(mb->meta [j])) continue;
			uint   blen;
			uchar *bk         = mset_key(mb, j, &blen);
			uvlong fields [4] = {alen, ( uvlong ) ak, blen, ( uvlong ) bk};
			uvlong count;
			if (!mulok64(ac, cntms(b, bk, blen), &count)) {
				errmsg("cartprodms: count overflow");
				return -1;
			}
			mset_set_count(prod, fields, sizeof(fields), count);
			if (err()) return -1;
		}
	}
	return prod;
}

void rmmultiset(int ms) {
	htab_t *t = mset_get(ms);
	if (t) htab_clear(t);
}
