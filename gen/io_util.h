#pragma once

#include <stdarg.h>
#include <stdbool.h>

typedef long long          vlong;
typedef unsigned long long uvlong;

typedef struct omode {
    bool r, w, x, t, d, a;
} omode;

/** Individual allocations cannot be freed; rmarena() releases them all. One thread owns an arena. */
int   mkarena(void);
void  rmarena(int arena);
void *aden(int arena, uvlong size);

/** Output is binary, so bytes are written verbatim. */
int   bopen(int arena, char *file, omode mod);
int   bprint(int bd, char *fm, ...);
vlong bwrite(int bd, void *buf, uvlong len);
int   rmbio(int bd);

/** `setmsg` must outlive the call. `errmsg(NULL)` clears the latch and returns the previous message. */
char *errmsg(char *setmsg);
void  efail(void);
