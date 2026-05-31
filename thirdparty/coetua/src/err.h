#pragma once
#include "atom.h"

/* ═══════════════════════════════════════════════════════
   err — thread-local error state
   Errors must not be silently overwritten; attempting to set a new
   error while one is pending aborts the program.
   ═══════════════════════════════════════════════════════ */

/* Check if an error is pending on this thread. */
bool  err(void);

/* Get the current error message (clears error state).
   Or set a new error message (non-null), returning the old one.
   If an error is already pending when you try to set another,
   the program aborts — errors must not be silently overwritten. */
char *errmsg(char *setmsg);

/* If an error is pending, print it to stderr and exit(1). */
void  efail(void);
