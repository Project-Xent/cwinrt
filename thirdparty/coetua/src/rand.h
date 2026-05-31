#pragma once
#include "atom.h"

/* ═══════════════════════════════════════════════════════
   rand — ChaCha8 (cryptographic) & wyrand (fast)
   ═══════════════════════════════════════════════════════ */

/* Feed a seed to all generators.  Up to 8 bytes are taken from data. */
void   seedfeed(void *data, uvlong len);

/* Cryptographically strong 64-bit random (ChaCha8). */
uvlong rand64(void);

/* Fast 64-bit random (wyrand variant).  Not cryptographically secure. */
uvlong qrand64(void);
