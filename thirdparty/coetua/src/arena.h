#pragma once
#include "atom.h"

/* ═══════════════════════════════════════════════════════
   Memory Arena
   Region allocator backed by malloc. Individual items cannot
   be freed; the entire arena is released at once via rmarena().
   arena 0 is the default and cannot be destroyed.
   Alignment must be a power of two; large allocations use
   dedicated blocks.
   ═══════════════════════════════════════════════════════ */

/* ── Default arena operations (arena 0) ─────────────── */
void *den(uvlong size);                   /* alloc; null on OOM               */
void *sala(uvlong count, uvlong elmsize); /* zeroed alloc; null on OOM        */
void *carrel(int align, uvlong size);     /* aligned alloc; align must be 2^n  */

/* ── Named arena operations ─────────────────────────── */
int   mkarena(void);                                  /* new or reused arena id; -1 on failure            */
void  rmarena(int arena);                             /* destroy arena id > 0 and all memory within       */

void *aden(int arena, uvlong size);                   /* arena variant of den     */
void *asala(int arena, uvlong count, uvlong elmsize); /* arena variant of sala    */
void *acarrel(int arena, int align, uvlong size);     /* arena variant of carrel  */
