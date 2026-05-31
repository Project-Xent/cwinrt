#include "silo_priv.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
	int    desc;
	uvlong pos;
	uvlong gen;
	bool   obliterate;
	bool   live;
} iter_t;

static iter_t *iters     = null;
static int     niters    = 0;
static int     iters_cap = 0;

static bool    iter_ensure(int needed) {
	if (needed <= iters_cap) return true;
	uint ucap   = nextpow2(( uint ) needed);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_ITER_TABLE_SEED) newcap = COETUA_ITER_TABLE_SEED;
	iter_t *next = ( iter_t * ) calloc(( size_t ) newcap, sizeof(iter_t));
	if (!next) {
		errmsg("iter: out of memory");
		return false;
	}
	if (iters) memcpy(next, iters, ( uvlong ) iters_cap * sizeof(iter_t));
	free(iters);
	iters     = next;
	iters_cap = newcap;
	return true;
}

static int iter_new(void) {
	for (int i = 0; i < niters; i++)
		if (!iters [i].live) {
			iters [i].live = true;
			return i;
		}
	if (!iter_ensure(niters + 1)) return -1;
	int id          = niters++;
	iters [id].live = true;
	return id;
}

static iter_t *iter_get(int ii) {
	if (ii < 0 || ii >= niters || !iters [ii].live) return null;
	return &iters [ii];
}

static void iter_seek_hash_live(iter_t *it, htab_t *t) {
	while (it->pos < t->cap && !htab_slot_live(t->meta [it->pos])) it->pos++;
}

static void iter_init(iter_t *it, int desc, uvlong gen) {
	it->desc       = desc;
	it->pos        = 0;
	it->gen        = gen;
	it->obliterate = false;
}

static bool iter_source(int desc, silo_t **s, htab_t **t) {
	*s = silo_get(desc);
	*t = htab_get(desc);
	return *s || *t;
}

int mkiter(int desc) {
	silo_t *s;
	htab_t *t;
	if (!iter_source(desc, &s, &t)) {
		errmsg("mkiter: invalid desc");
		return -1;
	}

	int id = iter_new();
	if (id < 0) return -1;
	iter_init(&iters [id], desc, s ? s->gen : t->gen);

	if (t) iter_seek_hash_live(&iters [id], t);
	return id;
}

int mkoter(int desc) {
	int id = mkiter(desc);
	if (id >= 0) iters [id].obliterate = true;
	return id;
}

static void iter_remove_linear(silo_t *s, uvlong pos) {
	if (pos >= s->len) return;
	if (s->type == silo_queue) {
		if (pos == 0) {
			s->head = (s->head + 1) % s->cap;
			return;
		}
		for (uvlong i = pos; i + 1 < s->len; i++)
			s->data [(s->head + i) % s->cap] = s->data [(s->head + i + 1) % s->cap];
		return;
	}
	memmove(&s->data [pos], &s->data [pos + 1], (s->len - pos - 1) * sizeof(uvlong));
}

static bool iter_next_linear(iter_t *it, silo_t *s, void *data) {
	if (!it->obliterate && it->gen != s->gen) return false;
	if (it->pos >= s->len) return false;
	*( uvlong * ) data = s->data [(s->head + it->pos) % s->cap];
	if (it->obliterate) {
		iter_remove_linear(s, it->pos);
		s->len--;
		silo_touch(s);
		it->gen = s->gen;
	}
	else { it->pos++; }
	return true;
}

static void iter_hash_fields(htab_t *t, uvlong pos, uvlong *fields) {
	if (!t->ismap) {
		fields [0] = t->klens [pos];
		fields [1] = ( uvlong ) (t->keys + t->koffs [pos]);
	}
	else if (t->ismultiset) {
		uvlong count = 0;
		if (t->vlens [pos] == sizeof(count)) memcpy(&count, t->vals + t->voffs [pos], sizeof(count));
		fields [0] = t->klens [pos];
		fields [1] = ( uvlong ) (t->keys + t->koffs [pos]);
		fields [2] = count;
	}
	else {
		fields [0] = t->klens [pos];
		fields [1] = ( uvlong ) (t->keys + t->koffs [pos]);
		fields [2] = t->vlens [pos];
		fields [3] = ( uvlong ) (t->vals + t->voffs [pos]);
	}
}

static bool iter_next_hash(iter_t *it, htab_t *t, void *data) {
	if (!it->obliterate && it->gen != t->gen) return false;
	iter_seek_hash_live(it, t);
	if (it->pos >= t->cap) return false;

	iter_hash_fields(t, it->pos, ( uvlong * ) data);
	if (it->obliterate) {
		htab_slot_kill(t, it->pos);
		htab_touch(t);
		it->gen = t->gen;
	}
	it->pos++;
	iter_seek_hash_live(it, t);
	return true;
}

bool next(int ii, void *data) {
	if (!data) {
		errmsg("next: bad output");
		return false;
	}
	iter_t *it = iter_get(ii);
	if (!it) {
		errmsg("next: bad iterator");
		return false;
	}
	silo_t *s;
	htab_t *t;
	if (!iter_source(it->desc, &s, &t)) return false;
	if (s) return iter_next_linear(it, s, data);
	return iter_next_hash(it, t, data);
}

void rmiter(int ii) {
	iter_t *it = iter_get(ii);
	if (!it) return;
	it->live = false;
}
