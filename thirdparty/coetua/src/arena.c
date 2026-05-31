#include "arena.h"
#include "config.h"
#include "err.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
  #pragma warning(disable:4200) /* flexible array member */
#endif

/* Region allocator: arenas are linked lists of malloc-backed blocks.
    Individual allocations are never freed; rmarena() releases a whole arena. */

typedef struct block block;

struct block {
	block *next;
	uvlong used;
	uvlong cap;
	uchar  data [];
};

typedef struct {
	block *head;
	block *tail;
	bool   live;
} arena_t;

/* Thread-local: each thread owns an independent arena table (including its own
   arena 0). Lock-free by construction — arena ids never cross threads in this
   project (workers allocate only into arenas they created; shared data is read
   through heap pointers, not through another thread's arena). */
static _Thread_local arena_t *arenas;
static _Thread_local int      narenas;
static _Thread_local int      arenacap;

static bool     arena_table_init(void) {
	if (arenas) return true;
	arenacap = COETUA_ARENA_TABLE_SEED;
	arenas   = ( arena_t * ) calloc(( size_t ) arenacap, sizeof(arena_t));
	if (!arenas) {
		errmsg("arena: out of memory");
		arenacap = 0;
		return false;
	}
	narenas = 1; /* arena 0 exists by default */
	return true;
}

static bool arena_table_grow(void) {
	int newcap = arenacap ? arenacap * 2 : COETUA_ARENA_TABLE_SEED;
	if (newcap <= arenacap) {
		errmsg("arena: descriptor overflow");
		return false;
	}
	arena_t *p = ( arena_t * ) realloc(arenas, ( size_t ) newcap * sizeof(arena_t));
	if (!p) {
		errmsg("arena: out of memory");
		return false;
	}
	memset(p + arenacap, 0, ( size_t ) (newcap - arenacap) * sizeof(arena_t));
	arenas   = p;
	arenacap = newcap;
	return true;
}

static bool arena_live(int id) {
	if (!arena_table_init()) return false;
	return id == 0 || (id > 0 && id < narenas && arenas [id].live);
}

static uvlong align_slop(uvlong align) { return align > 0 ? align - 1 : 0; }

static block *block_new(uvlong min_size) {
	uvlong cap = min_size > COETUA_ARENA_BLOCK_SIZE ? min_size : COETUA_ARENA_BLOCK_SIZE;
	uvlong alloc_sz;
	if (!addok64(sizeof(block), cap, &alloc_sz)) return null;
	block *b = ( block * ) malloc(( size_t ) alloc_sz);
	if (!b) return null;
	b->next = null;
	b->used = 0;
	b->cap  = cap;
	return b;
}

static bool block_need(uvlong size, uvlong align, uvlong *need) {
	uvlong slop = align_slop(align);
	return addok64(size, slop, need);
}

static bool block_aligned_offset(block *b, uvlong align, uvlong *offset) {
	uintptr_t base = ( uintptr_t ) b->data;
	uintptr_t ptr  = base + ( uintptr_t ) b->used;
	if (ptr < base) return false;

	uintptr_t mask = ( uintptr_t ) align - 1;
	uintptr_t mis  = ptr & mask;
	uvlong    adj  = mis ? ( uvlong ) (align - mis) : 0;
	return addok64(b->used, adj, offset);
}

static bool block_has_room(block *b, uvlong offset, uvlong size) { return offset <= b->cap && size <= b->cap - offset; }

static void arena_append_block(arena_t *a, block *b) {
	if (a->tail) a->tail->next = b;
	else a->head = b;
	a->tail = b;
}

static void arena_reset(arena_t *a) {
	a->head = null;
	a->tail = null;
}

static int arena_reuse_slot(int id) {
	arena_reset(&arenas [id]);
	arenas [id].live = true;
	return id;
}

static void *arena_alloc(arena_t *a, uvlong size, int align, bool zero) {
	if (align < 1) align = 1;
	uvlong ualign = ( uvlong ) align;
	if (!ispow2_64(ualign)) {
		errmsg("arena: alignment must be power of 2");
		return null;
	}
	if (size == 0) return null;

	uvlong need;
	if (!block_need(size, ualign, &need)) {
		errmsg("arena: size overflow");
		return null;
	}

	block *b = a->tail;
	if (!b) {
		b = block_new(need);
		if (!b) {
			errmsg("arena: out of memory");
			return null;
		}
		arena_append_block(a, b);
	}

	for (;;) {
		uvlong offset;
		if (!block_aligned_offset(b, ualign, &offset)) {
			errmsg("arena: size overflow");
			return null;
		}
		if (block_has_room(b, offset, size)) {
			void *p = b->data + offset;
			b->used = offset + size;
			if (zero) memset(p, 0, ( uvlong ) size);
			return p;
		}

		b = block_new(need);
		if (!b) {
			errmsg("arena: out of memory");
			return null;
		}
		arena_append_block(a, b);
	}
}

/* ── Default arena (0) ───────────────────────────────── */

void *den(uvlong size) { return aden(0, size); }

void *sala(uvlong count, uvlong elmsize) {
	uvlong size;
	if (!mulok64(count, elmsize, &size)) {
		errmsg("sala: size overflow");
		return null;
	}
	return asala(0, count, elmsize);
}

void *carrel(int align, uvlong size) { return acarrel(0, align, size); }

/* ── Named arenas ────────────────────────────────────── */

int   mkarena(void) {
	if (!arena_table_init()) return -1;
	for (int i = 1; i < narenas; i++)
		if (!arenas [i].live) return arena_reuse_slot(i);
	if (narenas >= arenacap && !arena_table_grow()) return -1;
	return arena_reuse_slot(narenas++);
}

void rmarena(int id) {
	if (!arena_table_init()) return;
	if (id <= 0 || id >= narenas || !arenas [id].live) return;
	block *b = arenas [id].head;
	while (b) {
		block *next = b->next;
		free(b);
		b = next;
	}
	arena_reset(&arenas [id]);
	arenas [id].live = false;
}

void *aden(int id, uvlong size) {
	if (!arena_live(id)) {
		errmsg("aden: invalid arena");
		return null;
	}
	return arena_alloc(&arenas [id], size, 1, false);
}

void *asala(int id, uvlong count, uvlong elmsize) {
	if (!arena_live(id)) {
		errmsg("asala: invalid arena");
		return null;
	}
	uvlong size;
	if (!mulok64(count, elmsize, &size)) {
		errmsg("asala: size overflow");
		return null;
	}
	return arena_alloc(&arenas [id], size, 1, true);
}

void *acarrel(int id, int align, uvlong size) {
	if (!arena_live(id)) {
		errmsg("acarrel: invalid arena");
		return null;
	}
	return arena_alloc(&arenas [id], size, align, false);
}
