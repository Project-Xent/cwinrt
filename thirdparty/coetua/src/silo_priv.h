#pragma once
#include "silo.h"

/*
 * Private silo/hash-table state.
 *
 * Public descriptors encode the container family in their low bits and the
 * table index above them.  Linear silos and hash silos keep independent
 * internal descriptor tables.
 */

#define HASH_META_EMPTY     0
#define HASH_META_TOMBSTONE 1
#define HASH_LOAD_FACTOR_N  7
#define HASH_LOAD_FACTOR_D  8

static bool inline htab_slot_empty(uchar meta) { return meta == HASH_META_EMPTY; }

static bool inline htab_slot_dead(uchar meta) { return meta <= HASH_META_TOMBSTONE; }

static bool inline htab_slot_live(uchar meta) { return !htab_slot_dead(meta); }

typedef struct {
	int     type;
	int     arena;
	uvlong *data;
	uvlong  cap;
	uvlong  len;
	uvlong  head;
	uvlong  gen;
} silo_t;

typedef struct {
	int    arena;
	uchar *meta;
	uchar *keys;
	uchar *vals;
	uint  *koffs;
	uint  *klens;
	uint  *voffs;
	uint  *vlens;
	uvlong cap;
	uvlong len;
	uvlong tombstones;
	uint   key_total;
	uint   val_total;
	uvlong gen;
	bool   ismap;
	bool   ismultiset;
	bool   live;
} htab_t;

static void inline htab_slot_kill(htab_t *t, uvlong idx) {
	t->meta [idx] = HASH_META_TOMBSTONE;
	t->tombstones++;
	t->len--;
}

extern silo_t *silos;
extern int     nsilos;
extern int     silos_cap;
extern htab_t *htabs;
extern int     nhtabs;
extern int     htabs_cap;

#define SILO_DESC_BITS 3
#define SILO_DESC_MASK ((1 << SILO_DESC_BITS) - 1)

/* Low bits carry silotype; high bits carry the index in that family's table. */

static int inline desc_tag(int desc) { return desc & SILO_DESC_MASK; }

static int inline desc_index(int desc) { return desc >> SILO_DESC_BITS; }

static int inline make_desc(int idx, int tag) { return (idx << SILO_DESC_BITS) | tag; }

void    silo_touch(silo_t *s);
bool    silo_desc_ensure(int needed);
bool    htab_desc_ensure(int needed);
int     silo_new(int t, int arena);
silo_t *silo_get(int desc);
bool    silo_grow(silo_t *s, uvlong needed);

void    htab_touch(htab_t *t);
htab_t *htab_get(int desc);
uchar   htab_hash_meta(uvlong h);
int     htab_new_desc(void);
void    htab_init(htab_t *t, int arena, bool ismap);
void    htab_clear(htab_t *t);
uvlong  htab_probe(htab_t *t, void *key, uvlong klen, uvlong hash, bool *found);
bool    htab_keys_grow(htab_t *t, uint needed);
bool    htab_vals_grow(htab_t *t, uint needed);
bool    htab_maybe_grow(htab_t *t);
bool    htab_compact(htab_t *t);
