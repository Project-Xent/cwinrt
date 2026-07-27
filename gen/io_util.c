#include "io_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* The generator runs one worker thread per CPU, each creating its own arenas
   and output streams. Descriptor slots are claimed lock-free with a compare-
   exchange; a given arena/stream is only ever touched by its owning thread,
   so per-object state (the block list, the FILE*) needs no locking. */

#if defined(_WIN32)
  #define IO_TLS __declspec(thread)
#else
  #define IO_TLS __thread
#endif

/* ── Arena ─────────────────────────────────────────────────────────── */

typedef struct arena {
    void  **ptrs; /* owner-thread-only: the individual allocations */
    size_t  n, cap;
} arena;

#define IO_MAX_ARENAS 4096
static arena *g_arenas[IO_MAX_ARENAS]; /* slot 0 reserved/unused */

int mkarena(void) {
    arena *ar = calloc(1, sizeof *ar);
    if (!ar)
        return -1;
    for (int i = 1; i < IO_MAX_ARENAS; i++) {
        if (InterlockedCompareExchangePointer((void *volatile *)&g_arenas[i], ar, NULL) == NULL)
            return i;
    }
    free(ar); /* table full */
    return -1;
}

void *aden(int a, uvlong size) {
    arena *ar;
    void  *p;
    if (a <= 0 || a >= IO_MAX_ARENAS || !(ar = g_arenas[a]))
        return NULL;
    /* Zeroed, like coetua: its arena carves from large malloc'd blocks backed by
       fresh (zero) OS pages, so callers rely on aden memory being zero-initialized.
       A per-allocation malloc comes off the freelist (non-zero), so zero it here —
       otherwise uninitialized pointer fields fault (e.g. map.c reads them). */
    p = calloc(1, size ? (size_t)size : 1);
    if (!p)
        return NULL;
    if (ar->n == ar->cap) {
        size_t nc = ar->cap ? ar->cap * 2 : 16;
        void **np = realloc(ar->ptrs, nc * sizeof *np);
        if (!np) {
            free(p);
            return NULL;
        }
        ar->ptrs = np;
        ar->cap  = nc;
    }
    ar->ptrs[ar->n++] = p;
    return p;
}

void rmarena(int a) {
    arena *ar;
    if (a <= 0 || a >= IO_MAX_ARENAS || !(ar = g_arenas[a]))
        return;
    for (size_t i = 0; i < ar->n; i++)
        free(ar->ptrs[i]);
    free(ar->ptrs);
    free(ar);
    g_arenas[a] = NULL;
}

/* ── Buffered file writer ──────────────────────────────────────────── */

#define IO_MAX_STREAMS 1024
static FILE *g_streams[IO_MAX_STREAMS]; /* slot 0 reserved/unused */

int bopen(int arena, char *file, omode mod) {
    /* Binary mode: the generated output must be byte-identical across
       platforms, so no CRLF translation. The generator only writes. */
    char const *m = mod.a ? "ab" : (mod.w ? "wb" : "rb");
    FILE       *fp;
    (void)arena;
    fp = fopen(file, m);
    if (!fp)
        return -1;
    for (int i = 1; i < IO_MAX_STREAMS; i++) {
        if (InterlockedCompareExchangePointer((void *volatile *)&g_streams[i], fp, NULL) == NULL)
            return i;
    }
    fclose(fp); /* table full */
    return -1;
}

static FILE *io_stream(int bd) {
    return (bd > 0 && bd < IO_MAX_STREAMS) ? g_streams[bd] : NULL;
}

int bprint(int bd, char *fm, ...) {
    FILE   *fp = io_stream(bd);
    va_list ap;
    int     n;
    if (!fp)
        return -1;
    va_start(ap, fm);
    n = vfprintf(fp, fm, ap);
    va_end(ap);
    return n;
}

vlong bwrite(int bd, void *buf, uvlong len) {
    FILE  *fp = io_stream(bd);
    size_t w;
    if (!fp)
        return -1;
    w = fwrite(buf, 1, (size_t)len, fp);
    return w == (size_t)len ? (vlong)w : -1;
}

int rmbio(int bd) {
    FILE *fp = io_stream(bd);
    if (!fp)
        return -1;
    g_streams[bd] = NULL;
    return fclose(fp);
}

/* ── Thread-local error latch ──────────────────────────────────────── */

static IO_TLS char *g_err;

char *errmsg(char *setmsg) {
    char *old = g_err;
    g_err = setmsg; /* setmsg==NULL clears; a non-NULL message is latched */
    return old;
}

void efail(void) {
    if (g_err) {
        fprintf(stderr, "%s\n", g_err);
        exit(1);
    }
}
