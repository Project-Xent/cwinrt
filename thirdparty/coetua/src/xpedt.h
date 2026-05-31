#pragma once
#include "silo.h"
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════
   xpedt — deferred typed source DAG.

   Positive source descriptors name unresolved external inputs.  They do
   not need to be declared.  Negative source descriptors are computed
   nodes returned by xfunctions.  Descriptor zero is invalid.

   A complete xpedt DAG has one live leaf: the most recently returned
   negative source.  xact() immediately evaluates that batch job.  xpass(),
   xcall(), and xcalleach() register jobs for later xrun().

    Missing positive sources are bound from the variadic input list in
    ascending positive sd order.  Each variadic argument is an xprod.  In
    the current implementation, xprod.d names a concrete silo input or a
    molded silo result; xprod.u, xprod.r, xprod.a, and xprod.p are reserved
    for scalar/value-producing xfunctions as the protocol grows.
   ═══════════════════════════════════════════════════════ */

typedef enum xrslt
{
	xrtuvlong,
	xrtrune,
	xrtarrst,
	xrtpair,
} xrslt;

typedef struct pair {
	arrst a;
	arrst b;
} pair;

static pair inline mkpair(arrst a, arrst b) { return ( pair ) {a, b}; }

typedef union xprod
{
	int    d;
	uvlong u;
	rune   r;
	arrst  a;
	pair   p;
} xprod;

int  mkxpedt(int arena);
int  fmap(int xpedt, int src, xrslt rs, void (*fn)(pair sd, void *), void *args);
int  filt(int xpedt, int src, bool (*pred)(arrst i, void *), void *args);
int  mold(int xpedt, int src, silotype t);
int  xtake(int xpedt, int src, uvlong n);
int  xdrop(int xpedt, int src, uvlong n);
int  xrecur(int xpedt, int src);
int  xorama(int xpedt, int src, uvlong size, uvlong stride);
int  xany(int xpedt, int src, bool (*pred)(arrst i, void *), void *args);
int  xall(int xpedt, int src, bool (*pred)(arrst i, void *), void *args);
int  xfind(int xpedt, int src, bool (*pred)(arrst i, void *), void *args);
int  sort(int xpedt, int src, int (*cmp)(void *, void *, void *), void *args);
int  aggroup(int xpedt, int src, int (*equiv)(arrst i, void *), void *args);
int  xuniq(int xpedt, int src, void (*resolv)(arrst k, arrst va, arrst vb, arrst rv, void *), void *args);
int  ximmix(int xpedt, int srca, int srcb);
int  catena(int xpedt, int srca, int srcb);
int  xmerge(int xpedt, int srca, int srcb, int (*cmp)(void *, void *, void *), void *args);
int  connex(int xpedt, int srca, int srcb, xrslt rs, void (*zippr)(arrst a, arrst b, arrst dst, void *), void *args);
int  pare(int xpedt, int src, xrslt rs, void *init, void (*red)(arrst acc, arrst i, arrst dst, void *), void *args);
int  peruse(int xpedt, int src, xrslt rs, void *init, void (*red)(arrst acc, arrst i, arrst dst, void *), void *args);
int  diffuse(int xpedt, int src, xrslt rs);
int  flatten(int xpedt, int src);
int  flatmap(int xpedt, int src, xrslt rs, void (*fn)(arrst i, void (*yield)(arrst x), void *), void *args);

void vxact(int xpedt, xprod *out, va_list inputs);
void xact(int xpedt, xprod *out, ...);
void vxpass(int xpedt, xprod *out, va_list inputs);
void xpass(int xpedt, xprod *out, ...);
void vxcall(int xpedt, void (*fn)(xprod *out, void *), void *args, va_list inputs);
void xcall(int xpedt, void (*fn)(xprod *out, void *), void *args, ...);
void vxcalleach(int xpedt, void (*fn)(xprod *out, void *), void *args, va_list inputs);
void xcalleach(int xpedt, void (*fn)(xprod *out, void *), void *args, ...);
void xrun(int xpedt);
void rmxpedt(int xpedt);
