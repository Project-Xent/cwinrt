#pragma once
#include "atom.h"
#include "err.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════
   text — string views, dynamic strings, UTF-8 / rune
   ═══════════════════════════════════════════════════════ */

/* ── Slitr: immutable string view ───────────────────── */

typedef struct {
	char  *s;
	uvlong len;
} slitr;

static slitr inline mkslitr(char *s, uvlong len) { return ( slitr ) {s, len}; }

#define umslitr(sl)    (sl).s, (sl).len
#define slitr_of(cstr) mkslitr((cstr), strlen(cstr))
#define slitr_empty()  mkslitr(null, 0)

/* substring: [start, end] inclusive, negative indices from end */
slitr subsitr(slitr s, vlong start, vlong end);
bool  slitreq(slitr a, slitr b);
int   slitrcmp(slitr a, slitr b);   /* -1, 0, 1             */
slitr slitrdup(slitr s, int arena); /* arena-allocated copy */

/* ── Strand: dynamic string builder ─────────────────── */

int   mkstrand(int arena);
slitr obslitr(int strand); /* current content as slitr  */
/* Create a strand from char* arguments, terminated by null. */
int   catstr(int arena, ...);
/* Create a strand from a zero-terminated rune array.  Null input is empty. */
int   runetostr(int arena, rune *r);
/* Create a sequence descriptor containing decoded runes followed by a 0 terminator.
   Null input returns a sequence containing only the terminator. */
int   strtorune(int arena, char *s);
void  concat(int strand, char *s);             /* append C string          */
void  concats(int strand, slitr s);            /* append slitr             */
void  concatr(int strand, rune r);             /* append single rune       */
void  putstr(int strand, vlong pos, char *s);  /* insert at pos    */
void  dropstr(int strand, vlong pos, vlong n); /* remove n bytes at pos; if n<0, remove -n bytes before pos */
void  repeat(int strand, uvlong n);            /* repeat content n times   */
void  rmstrand(int strand);

/* ── Rune / UTF-8 ───────────────────────────────────── */

enum
{
	UTFmax    = 4,
	Runesync  = 0x80,
	Runeself  = 0x80,
	Runeerror = 0xfffd,
	Runemax   = 0x10ffff,
};

/* Unicode scalar value check. Surrogates are not valid runes. */
static bool inline chkrune(rune r) { return r <= Runemax && (r < 0xd800 || r > 0xdfff); }

/* Encode *r into s. Invalid runes encode as Runeerror. s must hold UTFmax bytes. */
int    runetochar(char *s, rune *r);

/* Decode one rune from a NUL-terminated UTF-8 string.
   Malformed bytes decode to Runeerror and consume one byte. */
int    chartorune(rune *r, char *s);

/* Return whether n bytes contain enough input for chartorune to decode one rune.
   This is an availability check, not a UTF validity check. */
int    fullrune(char *s, int n);

/* Encoded byte length. Invalid runes have the length of Runeerror. */
int    runelen(long r);
int    runenlen(rune *r, int nrune);

/* Count decoded runes. Malformed bytes count as one Runeerror rune. */
int    utfnlen(char *s, long n);
int    utflen(char *s);

/* Find first/last decoded rune. Searching for NUL returns the terminator. */
char  *utfrune(char *s, long r);
char  *utfrrune(char *s, long r);

/* Find byte substring t in NUL-terminated UTF-8 string s; strstr semantics. */
char  *utfutf(char *s, char *t);

/* Copy UTF text and terminate, stopping before a partial rune would be written. */
char  *utfecpy(char *to, char *eto, char *from);

/* Unicode classification and one-to-one case mapping. */
int    isalpharune(rune c);
int    islowerrune(rune c);
int    isspacerune(rune c);
int    istitlerune(rune c);
int    isupperrune(rune c);
rune   tolowerrune(rune c);
rune   totitlerune(rune c);
rune   toupperrune(rune c);

/* ── C string helpers ───────────────────────────────── */

/* Tokenize str in place on whitespace, with single quotes grouping text.
    Inside quotes, doubled single quotes produce one literal quote.
    Returns the total token count; stores at most max token pointers in arr. */
uvlong tknize(char *str, char **arr, uvlong max);

/* Copy at most (ed - d - 1) bytes, always NUL-terminate. Returns d. */
char  *strecpy(char *d, char *ed, char *s);

/* Append to d, same bounds as strecpy. Returns d. */
char  *strecat(char *d, char *ed, char *s);
