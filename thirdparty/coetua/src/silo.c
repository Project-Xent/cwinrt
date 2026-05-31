#include "silo_priv.h"
#include "arena.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════
    Internal: linear silos and hash silos use independent dynamically grown
    tables.  Public descriptors encode container kind plus table index.
   ═══════════════════════════════════════════════════════ */

silo_t     *silos     = null;
htab_t     *htabs     = null;
int         nsilos    = 0;
int         nhtabs    = 0;
int         silos_cap = 0;
int         htabs_cap = 0;

void        silo_touch(silo_t *s) { s->gen++; }

/* ── Internal helpers ────────────────────────────────── */

static void silo_init_slot(silo_t *s, int t, int arena) {
	s->type  = t;
	s->arena = arena;
	s->len   = 0;
	s->head  = 0;
}

static uvlong silo_newcap(silo_t *s, uvlong needed) {
	uvlong chunked = s->cap + COETUA_SILO_CHUNK;
	return needed > chunked ? needed : chunked;
}

bool silo_desc_ensure(int needed) {
	if (needed <= silos_cap) return true;
	uint ucap   = nextpow2(( uint ) needed);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_SILO_TABLE_SEED) newcap = COETUA_SILO_TABLE_SEED;
	silo_t *ns = ( silo_t * ) calloc(( size_t ) newcap, sizeof(silo_t));
	if (!ns) {
		errmsg("silo: out of memory");
		return false;
	}
	if (silos) memcpy(ns, silos, ( uvlong ) silos_cap * sizeof(silo_t));
	free(silos);
	silos     = ns;
	silos_cap = newcap;
	return true;
}

bool htab_desc_ensure(int needed) {
	if (needed <= htabs_cap) return true;
	uint ucap   = nextpow2(( uint ) needed);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_SILO_TABLE_SEED) newcap = COETUA_SILO_TABLE_SEED;
	htab_t *nh = ( htab_t * ) calloc(( size_t ) newcap, sizeof(htab_t));
	if (!nh) {
		errmsg("htab: out of memory");
		return false;
	}
	if (htabs) memcpy(nh, htabs, ( uvlong ) htabs_cap * sizeof(htab_t));
	free(htabs);
	htabs     = nh;
	htabs_cap = newcap;
	return true;
}

int silo_new(int t, int arena) {
	/* reuse freed slot (type == 0 marks freed) */
	for (int i = 0; i < nsilos; i++)
		if (silos [i].type == 0) {
			silo_init_slot(&silos [i], t, arena);
			silos [i].gen++;
			return make_desc(i, t);
		}
	int id = nsilos;
	if (!silo_desc_ensure(id + 1)) return -1;
	nsilos          = id + 1;
	silos [id].data = null;
	silos [id].cap  = 0;
	silos [id].gen  = 1;
	silo_init_slot(&silos [id], t, arena);
	return make_desc(id, t);
}

silo_t *silo_get(int desc) {
	if (desc < 0) return null;
	int tag = desc_tag(desc);
	if (tag != silo_stack && tag != silo_seq && tag != silo_queue) return null;
	int idx = desc_index(desc);
	if (idx < 0 || idx >= nsilos) return null;
	if (silos [idx].type != tag) return null;
	return &silos [idx];
}

bool silo_grow(silo_t *s, uvlong needed) {
	if (s->cap >= needed) return true;
	uvlong newcap = silo_newcap(s, needed);
	uvlong bytes;
	if (!mulok64(newcap, sizeof(uvlong), &bytes)) {
		errmsg("silo: size overflow");
		return false;
	}
	uvlong *newdata = ( uvlong * ) aden(s->arena, bytes);
	if (!newdata) return false;

	if (s->data && s->len > 0) {
		if (s->type == silo_queue) {
			/* unwrap circular queue into new contiguous array */
			for (uvlong i = 0; i < s->len; i++) newdata [i] = s->data [(s->head + i) % s->cap];
			s->head = 0;
		}
		else { memcpy(newdata, s->data, ( uvlong ) (s->len * sizeof(uvlong))); }
	}
	s->data = newdata;
	s->cap  = newcap;
	return true;
}
