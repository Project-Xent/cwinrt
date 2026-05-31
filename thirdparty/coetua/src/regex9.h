#pragma once
#include "atom.h"

/* ═══════════════════════════════════════════════════════
   regex9 — Plan 9 Thompson NFA regex VM (Coetua port)
   Uses arena allocation and executes against known-length text.
   ═══════════════════════════════════════════════════════ */

/* ── Forward declarations ───────────────────────────── */
typedef struct Resub   Resub;
typedef struct Reclass Reclass;
typedef struct Reinst  Reinst;
typedef struct Reprog  Reprog;
typedef struct Requant Requant;

typedef enum Resubkind
{
	RSUB_NONE,
	RSUB_TEXT,
	RSUB_QUANTITY,
} Resubkind;

/* ── Match result ───────────────────────────────────── */
typedef struct Resub {
	union
	{
		char  *sp;
		rune  *rsp;
		uvlong q;
	} s;

	union
	{
		char *ep;
		rune *rep;
	} e;

	Resubkind kind;
} Resub;

typedef struct Requant {
	uvlong n;
	uvlong m;
	uint   qid;
	uint   subid;
} Requant;

/* ── Character class ────────────────────────────────── */
typedef struct Reclass {
	rune  *spans;
	rune  *end;
	uvlong cap;
} Reclass;

/* ── VM instruction ─────────────────────────────────── */
typedef struct Reinst {
	int type;

	union
	{
		Reclass *cp;    /* class pointer (CCLASS/NCCLASS) */
		rune     r;     /* character (RUNE)               */
		int      subid; /* sub-expression id (LBRA/RBRA)  */
		Reinst  *right; /* right child of OR              */
	} u1;

	union
	{
		Reinst *left; /* left child of OR                */
		Reinst *next; /* next for CAT / fallthrough      */
	} u2;

	Requant q; /* counted @ quantifier metadata */
} Reinst;

/* ── Compiled program ───────────────────────────────── */
typedef struct Reprog {
	Reinst *startinst; /* first instruction          */
	uvlong  ninst;     /* number of VM instructions  */
	Reclass *class;    /* character class storage    */
	uvlong nclass;     /* number of character classes */
	uvlong nsub;       /* number of captures including whole match */
	uvlong nquant;     /* number of counted @ states */
} Reprog;

/* ── VM instruction types ───────────────────────────── */
/*  Operators (0200–0277): value == precedence           */
#define RUNE      0177
#define OPERATOR  0200
#define START     0200
#define RBRA      0201
#define LBRA      0202
#define OR        0203
#define CAT       0204
#define STAR      0205
#define PLUS      0206
#define QUEST     0207

/*  Operands (0300–0377)                                 */
#define ANY       0300
#define ANYNL     0301
#define NOP       0302
#define BOL       0303
#define EOL       0304
#define CCLASS    0305
#define NCCLASS   0306
/*  Counted @ quantifiers (0210–0221): value == precedence       */
#define QEXACT    0210 /* @n' */
#define QATLEAST  0211 /* @n+ */
#define QATMOST   0212 /* @m- */
#define QRANGE    0213 /* @n~m' */
#define QOUTSIDE  0214 /* @n^m; */
#define CQANY     0215 /* @()' */
#define CQATLEAST 0216 /* @(n)+ */
#define CQATMOST  0217 /* @(m)- */
#define CQRANGE   0220 /* @(n~m)' */
#define CQOUTSIDE 0221 /* @(n^m); */
#define END       0377

/* ── API ────────────────────────────────────────────── */
/* Compile regex pattern into a Reprog in the given arena.
   Returns null on error (check errmsg). */
Reprog *regcomp9(int arena, char *pattern);

/* Execute compiled regex on string s [0..n).
   Fills match[0..msize) with sub-expression boundaries.
   Returns >0 on match, 0 on no match, <0 on internal error. */
int     regexec9(Reprog *prog, char *s, uvlong n, Resub *match, int msize);
