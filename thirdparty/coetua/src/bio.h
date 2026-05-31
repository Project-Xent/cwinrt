#pragma once
#include "io.h"
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════
   Buffered I/O — descriptor streams built over raw d I/O.
   bio owns buffering, logical read/write transitions, and optional
   ownership of an underlying raw fd opened by bopen().
   ═══════════════════════════════════════════════════════ */

/* Create a buffered stream descriptor in arena. */
int   mkbio(int arena);
/* Bind an existing fd to a buffered stream.  The bio does not own fd;
   bclose() disassociates it without closing it.  Rebinding first closes
   a previously bopen-owned fd or disassociates a previously binit-bound fd. */
void  binit(int bd, int fd, omode mod);
/* Create a buffered stream and open file.  The bio owns the opened fd. */
int   bopen(int arena, char *file, omode mod);
/* Return the underlying file descriptor, or -1 if invalid. */
int   bfildes(int bd);
/* Read through delim and return a borrowed buffer slice, including delim if found. */
arrst brdline(int bd, rune delim);
/* Read through delim into a NUL-terminated strand; if nulldelim, omit the terminal delimiter. */
int   brdstr(int bd, int arena, rune delim, bool nulldelim);
/* Read up to len bytes through the buffer. */
vlong bread(int bd, void *buf, uvlong len);

static vlong inline breada(int bd, arrst buf) { return bread(bd, buf.x, buf.len); }

/* Write len bytes through the buffer. */
vlong bwrite(int bd, void *buf, uvlong len);

static vlong inline bwritea(int bd, arrst buf) { return bwrite(bd, buf.x, buf.len); }

/* Set the logical stream offset, discarding buffered bytes. */
vlong  bseek(int bd, vlong amount, int from);
/* Return the logical file offset of the next byte to be read or written. */
vlong  boffset(int bd);
/* Return bytes buffered for reading, pushed back, or pending output. */
uvlong bbuffered(int bd);

/* Read one byte, returning 0..255, or -1 at EOF/error.  Check err() to distinguish errors. */
int    bgetc(int bd);
/* Back up one byte immediately after a successful bgetc(), if possible. */
void   bungetc(int bd);
/* Write one byte.  Returns 1 on success, or -1 on error. */
int    bputc(int bd, char c);
/* Read one UTF-8 rune, returning the rune or (rune)-1 at EOF/error. */
rune   bgetrune(int bd);
/* Back up the UTF sequence immediately after bgetrune(), so it can be reread as bytes or a rune. */
void   bungetrune(int bd);
/* Write one UTF-8 rune.  Returns bytes written, or -1 on error. */
int    bputrune(int bd, rune r);
/* Print formatted output through the buffer.  Returns bytes written, or -1 on error. */
int    bvprint(int bd, char *fm, va_list args);
int    bprint(int bd, char *fm, ...);
/* Read a formatted floating-point number, skipping leading blanks and tabs. */
double bgetd(int bd);

/* Flush pending output bytes. */
void   bflush(int bd);
/* Flush pending output.  If the fd was opened by bopen(), close it;
   otherwise disassociate the fd bound by binit(). */
void   bclose(int bd);
/* Remove buffered descriptor, first applying bclose() ownership rules. */
void   rmbio(int bd);
