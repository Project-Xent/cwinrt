#pragma once

/* Minimal file-writer / arena / error helpers for cwinrt-gen.
   Replaces the small slice of coetua the generator used (arena + bio + err):
   an arena region allocator, a buffered file writer with a printf-style API,
   and a thread-local error latch. Descriptors are small ints, matching the
   original API so call sites are unchanged. */

#include <stdarg.h>
#include <stdbool.h>

/* Portable width aliases (were coetua atom.h macros). */
typedef long long          vlong;
typedef unsigned long long uvlong;

/* File open mode (was coetua io.h `omode`). The generator only ever sets
   .w (write) and .t (truncate); the other flags are kept for source-compat. */
typedef struct omode {
    bool r, w, x, t, d, a;
} omode;

/* ── Arena: region allocator, freed as a whole (was coetua arena.h) ──
   Individual allocations cannot be freed; rmarena() releases them all.
   An arena is created and used by a single thread (one per gen worker). */
int   mkarena(void);                 /* new arena id > 0, or -1 on failure */
void  rmarena(int arena);            /* free the arena and everything in it */
void *aden(int arena, uvlong size);  /* allocate `size` bytes; NULL on OOM  */

/* ── Buffered file writer (was coetua bio.h) ──
   bopen() opens `file` per `mod` and returns a stream descriptor > 0.
   Output is binary (no newline translation), so bytes are written verbatim. */
int   bopen(int arena, char *file, omode mod);   /* stream id > 0, or -1 */
int   bprint(int bd, char *fm, ...);             /* bytes written, or -1 */
vlong bwrite(int bd, void *buf, uvlong len);     /* bytes written, or -1 */
void  rmbio(int bd);                             /* flush + close        */

/* ── Thread-local error latch (was coetua err.h) ──
   errmsg(msg) records a pending error (msg must outlive the call, e.g. a
   string literal); errmsg(NULL) clears it and returns the previous message.
   efail() prints any pending error to stderr and exit(1)s. */
char *errmsg(char *setmsg);
void  efail(void);
