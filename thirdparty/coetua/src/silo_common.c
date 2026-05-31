#include "silo_priv.h"
#include "arena.h"
#include "err.h"
#include <string.h>

static uchar empty_field;

static bool  bad_fields(uvlong *p, bool ismap, char *who) {
	if (!p) {
		errmsg(who);
		return true;
	}
	if (!p [1] && p [0] > 0) {
		errmsg(who);
		return true;
	}
	if (ismap && !p [3] && p [2] > 0) {
		errmsg(who);
		return true;
	}
	return false;
}

static void *field_ptr(uvlong len, uvlong ptr) { return ptr ? ( void * ) ptr : len == 0 ? &empty_field : null; }

static void  copy_linear_data(silo_t *src, uvlong *dst) {
	if (src->type == silo_queue) {
		for (uvlong i = 0; i < src->len; i++) dst [i] = src->data [(src->head + i) % src->cap];
		return;
	}
	memcpy(dst, src->data, src->len * sizeof(uvlong));
}

static uvlong first_live_slot(htab_t *t) {
	for (uvlong i = 0; i < t->cap; i++)
		if (htab_slot_live(t->meta [i])) return i;
	return t->cap;
}

static void htab_delete_slot(htab_t *t, uvlong idx) {
	htab_slot_kill(t, idx);
	htab_touch(t);
}

static void htab_write_fields(htab_t *t, uvlong idx, uvlong **out) {
	uvlong *p  = *out;
	p [0]      = t->klens [idx];
	p [1]      = ( uvlong ) (t->keys + t->koffs [idx]);
	p         += 2;
	if (t->ismap) {
		p [0]  = t->vlens [idx];
		p [1]  = ( uvlong ) (t->vals + t->voffs [idx]);
		p     += 2;
	}
	*out = p;
}

static bool htab_insert_fields(int desc, bool ismap, uvlong **in) {
	uvlong *p = *in;
	if (bad_fields(p, ismap, "silo fields: bad data")) return false;
	uint  klen  = ( uint ) p [0];
	void *key   = field_ptr(p [0], p [1]);
	p          += 2;
	if (!ismap) {
		adds(desc, key, klen);
		*in = p;
		return !err();
	}
	uint  vlen  = ( uint ) p [0];
	void *val   = field_ptr(p [0], p [1]);
	p          += 2;
	insert(desc, key, klen, val, vlen);
	*in = p;
	return !err();
}

static int htab_new_same_kind(htab_t *t, int arena) {
	if (t->ismultiset) return mkmultiset(arena);
	if (t->ismap) return mkmap(arena);
	return mkset(arena);
}

static void htab_copy_entries(int dst, htab_t *src) {
	for (uvlong i = 0; i < src->cap; i++) {
		if (!htab_slot_live(src->meta [i])) continue;
		if (src->ismap)
			insert(dst, src->keys + src->koffs [i], src->klens [i], src->vals + src->voffs [i], src->vlens [i]);
		else adds(dst, src->keys + src->koffs [i], src->klens [i]);
		if (err()) return;
	}
}

static void htab_teem_desc(int desc) {
	htab_t *t = htab_get(desc);
	if (!t) return;
	if (t->meta && t->cap > 0) memset(t->meta, 0, ( uvlong ) t->cap);
	t->len        = 0;
	t->tombstones = 0;
	t->key_total  = 0;
	t->val_total  = 0;
	htab_touch(t);
}

uvlong cize(int desc) {
	silo_t *s = silo_get(desc);
	if (!s) {
		errmsg("cize: bad linear silo");
		return 0;
	}
	return s->len;
}

void teem(int desc) {
	silo_t *s = silo_get(desc);
	if (s) {
		s->len  = 0;
		s->head = 0;
		silo_touch(s);
		return;
	}
	htab_t *t = htab_get(desc);
	if (!t) {
		errmsg("teem: bad silo");
		return;
	}
	htab_teem_desc(desc);
}

uvlong carten(int desc) {
	htab_t *t = htab_get(desc);
	if (!t) {
		errmsg("carten: bad hash silo");
		return 0;
	}
	return t->len;
}

void compact(int desc) {
	silo_t *s = silo_get(desc);
	if (s) return;
	htab_t *t = htab_get(desc);
	if (!t) {
		errmsg("compact: bad silo");
		return;
	}
	htab_compact(t);
}

void efflate(int desc, uvlong size) {
	silo_t *s = silo_get(desc);
	if (s) {
		silo_grow(s, size);
		return;
	}
	if (htab_get(desc)) return;
	errmsg("efflate: bad silo");
}

void tamp(int desc) {
	silo_t *s = silo_get(desc);
	if (s) {
		if (s->len == 0) return;
		uvlong bytes;
		if (!mulok64(s->len, sizeof(uvlong), &bytes)) {
			errmsg("tamp: size overflow");
			return;
		}
		uvlong *newdata = ( uvlong * ) aden(s->arena, bytes);
		if (!newdata) return;
		copy_linear_data(s, newdata);
		s->head = 0;
		s->data = newdata;
		s->cap  = s->len;
		silo_touch(s);
		return;
	}
	htab_t *t = htab_get(desc);
	if (!t) {
		errmsg("tamp: bad silo");
		return;
	}
	htab_compact(t);
}

int replica(int arena, int desc) {
	silo_t *s = silo_get(desc);
	if (s) {
		int nid = silo_new(s->type, arena);
		if (nid < 0) return -1;
		silo_t *ns = silo_get(nid);
		if (!silo_grow(ns, s->len)) return -1;
		if (s->len > 0) copy_linear_data(s, ns->data);
		ns->len  = s->len;
		ns->head = 0;
		return nid;
	}

	htab_t *t = htab_get(desc);
	if (t) {
		int nid = htab_new_same_kind(t, arena);
		if (nid < 0) return -1;
		htab_copy_entries(nid, t);
		if (err()) return -1;
		return nid;
	}
	errmsg("replica: bad silo");
	return -1;
}

static void silo_swop_linear(silo_t *s, void *data) {
	uvlong *val = ( uvlong * ) data;
	if (s->len == 0) {
		if (!silo_grow(s, 1)) return;
		s->data [0] = *val;
		s->head     = 0;
		s->len      = 1;
		silo_touch(s);
		return;
	}

	switch (s->type) {
	case silo_stack :
	case silo_seq   : {
		uvlong tmp           = s->data [s->len - 1];
		s->data [s->len - 1] = *val;
		*val                 = tmp;
		silo_touch(s);
	} break;
	case silo_queue : {
		uvlong idx    = s->head;
		uvlong tmp    = s->data [idx];
		s->data [idx] = *val;
		*val          = tmp;
		silo_touch(s);
	} break;
	default : break;
	}
}

static void linear_cram_one(int desc, silo_t *s, uvlong v) {
	switch (s->type) {
	case silo_stack : push(desc, v); break;
	case silo_seq   : atch(desc, v); break;
	case silo_queue : enqueue(desc, v); break;
	default         : break;
	}
}

static bool linear_spew_one(int desc, silo_t *s, uvlong *out) {
	if (s->len == 0) return false;
	if (s->type == silo_queue) {
		*out = dequeue(desc);
		return true;
	}
	if (s->type != silo_stack && s->type != silo_seq) return false;
	*out = s->data [0];
	memmove(s->data, s->data + 1, (s->len - 1) * sizeof(uvlong));
	s->len--;
	silo_touch(s);
	return true;
}

static void swop_setlike(int desc, htab_t *t, uvlong idx, uvlong *fields) {
	if (bad_fields(fields, false, "swop: bad data")) return;
	uint   tmp_klen = t->klens [idx];
	uchar *tmp_key  = t->keys + t->koffs [idx];
	uint   in_klen  = ( uint ) fields [0];
	uchar *in_key   = field_ptr(fields [0], fields [1]);
	htab_delete_slot(t, idx);
	adds(desc, in_key, in_klen);
	fields [0] = tmp_klen;
	fields [1] = ( uvlong ) tmp_key;
}

static void swop_maplike(int desc, htab_t *t, uvlong idx, uvlong *fields) {
	if (bad_fields(fields, true, "swop: bad data")) return;
	uint   tmp_klen = t->klens [idx];
	uchar *tmp_key  = t->keys + t->koffs [idx];
	uint   tmp_vlen = t->vlens [idx];
	uchar *tmp_val  = t->vals + t->voffs [idx];
	uint   in_klen  = ( uint ) fields [0];
	uchar *in_key   = field_ptr(fields [0], fields [1]);
	uint   in_vlen  = ( uint ) fields [2];
	uchar *in_val   = field_ptr(fields [2], fields [3]);
	htab_delete_slot(t, idx);
	insert(desc, in_key, in_klen, in_val, in_vlen);
	fields [0] = tmp_klen;
	fields [1] = ( uvlong ) tmp_key;
	fields [2] = tmp_vlen;
	fields [3] = ( uvlong ) tmp_val;
}

void swop(int desc, void *data) {
	if (!data) {
		errmsg("swop: bad data");
		return;
	}
	silo_t *s = silo_get(desc);
	if (s) {
		silo_swop_linear(s, data);
		return;
	}

	htab_t *t = htab_get(desc);
	if (!t) {
		errmsg("swop: bad silo");
		return;
	}

	uvlong *fields = ( uvlong * ) data;
	if (bad_fields(fields, t->ismap, "swop: bad data")) return;
	uvlong idx = first_live_slot(t);
	if (idx < t->cap) {
		if (!t->ismap) swop_setlike(desc, t, idx, fields);
		else swop_maplike(desc, t, idx, fields);
		return;
	}
	htab_insert_fields(desc, t->ismap, &fields);
}

void cram(int desc, uvlong *data, uvlong n) {
	if (!data) {
		if (n == 0) return;
		errmsg("cram: bad data");
		return;
	}
	if (n == 0) return;
	silo_t *s = silo_get(desc);
	if (s) {
		if (s->type != silo_stack && s->type != silo_seq && s->type != silo_queue) return;
		for (uvlong i = 0; i < n && !err(); i++) linear_cram_one(desc, s, data [i]);
		return;
	}
	htab_t *t = htab_get(desc);
	if (!t) {
		errmsg("cram: bad silo");
		return;
	}
	uvlong *p = data;
	for (uvlong i = 0; i < n; i++)
		if (!htab_insert_fields(desc, t->ismap, &p)) return;
}

uvlong spew(int desc, uvlong *buf, uvlong n) {
	if (!buf) {
		if (n == 0) return 0;
		errmsg("spew: bad buffer");
		return 0;
	}
	if (n == 0) return 0;
	silo_t *s = silo_get(desc);
	if (s) {
		uvlong i;
		for (i = 0; i < n; i++)
			if (!linear_spew_one(desc, s, &buf [i])) return i;
		return n;
	}

	htab_t *t = htab_get(desc);
	if (!t) {
		errmsg("spew: bad silo");
		return 0;
	}
	uvlong  taken = 0;
	uvlong *p     = buf;
	for (uvlong i = 0; i < t->cap && taken < n; i++) {
		if (!htab_slot_live(t->meta [i])) continue;
		htab_write_fields(t, i, &p);
		htab_delete_slot(t, i);
		taken++;
	}
	return taken;
}

int silotype_of(int desc) {
	silo_t *s = silo_get(desc);
	if (s) {
		switch (s->type) {
		case silo_stack : return ( int ) silo_stack;
		case silo_seq   : return ( int ) silo_seq;
		case silo_queue : return ( int ) silo_queue;
		default         : return 0;
		}
	}
	htab_t *t = htab_get(desc);
	if (t) return t->ismultiset ? ( int ) silo_multiset : t->ismap ? ( int ) silo_map : ( int ) silo_set;
	errmsg("silotype_of: bad silo");
	return 0;
}
