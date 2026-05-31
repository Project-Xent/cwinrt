#include "regex9.h"
#include "err.h"
#include "text.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Thompson-style executor for the rebuilt regex VM spine.
 * Handles ordinary opcodes, captures, and counted @ opcodes with dynamic
 * thread lists.
 */

typedef struct Thread Thread;
typedef struct List   List;

typedef struct Thread {
	Reinst *inst;
	Resub  *cap;
	uvlong *cnt;
	uvlong  ncnt;
} Thread;

typedef struct List {
	Thread *t;
	uvlong  len;
	uvlong  cap;
} List;

static void freelist(List *l) {
	if (!l) return;
	for (uvlong i = 0; i < l->len; i++) {
		free(l->t [i].cap);
		free(l->t [i].cnt);
	}
	free(l->t);
	l->t   = null;
	l->len = 0;
	l->cap = 0;
}

static bool grow(List *l, uvlong need) {
	if (l->cap >= need) return true;
	uvlong ncap = l->cap ? l->cap * 2 : 16;
	while (ncap < need) {
		uvlong next = ncap * 2;
		if (next <= ncap) {
			errmsg("regex allocation failed");
			return false;
		}
		ncap = next;
	}
	if (ncap > ( uvlong ) (SIZE_MAX / sizeof(Thread))) {
		errmsg("regex allocation failed");
		return false;
	}
	Thread *nt = realloc(l->t, ( size_t ) (ncap * sizeof(Thread)));
	if (!nt) {
		errmsg("regex allocation failed");
		return false;
	}
	l->t   = nt;
	l->cap = ncap;
	return true;
}

static bool seen(List *l, Reinst *i, Resub *cap, int ms) {
	for (uvlong n = 0; n < l->len; n++)
		if (l->t [n].inst == i && (!cap || !memcmp(l->t [n].cap, cap, ( size_t ) ms * sizeof(Resub)))) return true;
	return false;
}

static Resub *copycaps(Resub *src, int ms) {
	if (ms <= 0) return null;
	Resub *dst = calloc(( size_t ) ms, sizeof(Resub));
	if (!dst) errmsg("regex allocation failed");
	if (src) memcpy(dst, src, ( size_t ) ms * sizeof(Resub));
	return dst;
}

static uvlong *copycnt(uvlong *src, uvlong nc) {
	if (!nc) return null;
	uvlong *dst = calloc(( size_t ) nc, sizeof(uvlong));
	if (!dst) errmsg("regex allocation failed");
	if (dst && src) memcpy(dst, src, ( size_t ) nc * sizeof(uvlong));
	return dst;
}

static bool addthread_ex(List *l, Reinst *i, Resub *src, int ms, uvlong *srccnt, uvlong nc);

static bool epsilon(List *l, Reinst *i, Resub *cap, int ms, uvlong *cnt, uvlong nc) {
	while (i) {
		switch (i->type) {
		case NOP : i = i->u2.next; continue;
		case OR :
			if (!addthread_ex(l, i->u1.right, cap, ms, cnt, nc)) return false;
			i = i->u2.left;
			continue;
		default : return addthread_ex(l, i, cap, ms, cnt, nc);
		}
	}
	return true;
}

static bool addthread_ex(List *l, Reinst *i, Resub *src, int ms, uvlong *srccnt, uvlong nc) {
	if (!i || seen(l, i, src, ms)) return true;
	if (!grow(l, l->len + 1)) return false;
	Resub *cap = copycaps(src, ms);
	if (ms > 0 && !cap) return false;
	uvlong *c = copycnt(srccnt, nc);
	if (nc > 0 && !c) {
		free(cap);
		return false;
	}
	l->t [l->len] = (Thread) {i, cap, c, nc};
	l->len++;
	return epsilon(l, i, cap, ms, c, nc);
}

static bool classmatch(Reclass *c, rune r) {
	if (!c) return false;
	for (rune *p = c->spans; p < c->end; p += 2)
		if (r >= p [0] && r <= p [1]) return true;
	return false;
}

int regexec9(Reprog *prog, char *s, uvlong n, Resub *match, int msize) {
	if (!prog || !s) return errmsg("null regex argument"), -1;
	errmsg(null);
	if (msize < 0) return errmsg("negative match size"), -1;
	if (msize > 0 && !match) return errmsg("null match vector"), -1;

	char *end = s + n;
	int   ms  = msize;
	if (prog->nsub > 0 && ( uvlong ) ms < prog->nsub) ms = ( int ) prog->nsub;
	List   clist   = {0};
	List   nlist   = {0};
	bool   matched = false;
	Resub *best    = calloc(( size_t ) ms, sizeof(Resub));
	if (!best) return errmsg("regex allocation failed"), -1;

	for (char *start = s; start <= end; start++) {
		bool startmatched = false;
		freelist(&clist);
		freelist(&nlist);
		Resub *base = calloc(( size_t ) ms, sizeof(Resub));
		if (!base) {
			freelist(&clist);
			freelist(&nlist);
			free(best);
			return errmsg("regex allocation failed"), -1;
		}
		for (int i = 0; i < ms; i++) base [i].kind = RSUB_NONE;
		base [0].s.sp = start;
		base [0].e.ep = start;
		base [0].kind = RSUB_TEXT;
		if (!addthread_ex(&clist, prog->startinst, base, ms, null, prog->nquant)) {
			free(base);
			freelist(&clist);
			freelist(&nlist);
			return errmsg("regex allocation failed"), -1;
		}
		free(base);

		char *p = start;
		while (1) {
			uvlong ci = 0;
			while (ci < clist.len) {
				Reinst *ins = clist.t [ci].inst;
				Resub  *cap = clist.t [ci].cap;
				uvlong *cnt = clist.t [ci].cnt;
				uvlong  nc  = clist.t [ci].ncnt;
				ci++;
				if (!ins) continue;
				if (cap && ms > 0) cap [0].e.ep = p;
				switch (ins->type) {
				case END : {
					uvlong mlen = ( uvlong ) (p - start);
					if (!startmatched || mlen > ( uvlong ) (best [0].e.ep - best [0].s.sp)) {
						for (int k = 0; k < ms; k++) best [k] = cap [k];
						best [0].s.sp = start;
						best [0].e.ep = p;
						best [0].kind = RSUB_TEXT;
						startmatched  = true;
					}
					break;
				}
				case LBRA :
					if (cap && ins->u1.subid >= 0 && ins->u1.subid < ms) {
						cap [ins->u1.subid].s.sp = p;
						cap [ins->u1.subid].kind = RSUB_TEXT;
					}
					if (!addthread_ex(&clist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					break;
				case RBRA :
					if (cap && ins->u1.subid >= 0 && ins->u1.subid < ms) {
						cap [ins->u1.subid].e.ep = p;
						cap [ins->u1.subid].kind = RSUB_TEXT;
					}
					if (!addthread_ex(&clist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					break;
				case RUNE :
					if (p < end && *p == ( char ) ins->u1.r) {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				case ANY :
					if (p < end && *p != '\n') {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				case ANYNL :
					if (p < end)
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					break;
				case BOL :
					if (p == s || (p > s && p [-1] == '\n')) {
						if (!addthread_ex(&clist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				case EOL :
					if (p == end || *p == '\n') {
						if (!addthread_ex(&clist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				case CCLASS :
					if (p < end && classmatch(ins->u1.cp, ( uchar ) *p)) {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				case NCCLASS :
					if (p < end && !classmatch(ins->u1.cp, ( uchar ) *p)) {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				case QEXACT : {
					uvlong qid = ins->q.qid;
					if (!cnt) break;
					cnt [qid]++;
					if (cnt [qid] < ins->q.n) {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
					}
					else {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				}
				case QATLEAST : {
					uvlong qid = ins->q.qid;
					if (!cnt) break;
					cnt [qid]++;
					if (cnt [qid] >= ins->q.n) {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					else {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
					}
					break;
				}
				case QATMOST : {
					uvlong qid = ins->q.qid;
					if (!cnt) break;
					cnt [qid]++;
					if (cnt [qid] < ins->q.n) {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					else {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				}
				case QRANGE : {
					uvlong qid = ins->q.qid;
					if (!cnt) break;
					cnt [qid]++;
					if (cnt [qid] < ins->q.n) {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
					}
					else if (cnt [qid] < ins->q.m) {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					else {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				}
				case QOUTSIDE : {
					uvlong qid = ins->q.qid;
					if (!cnt) break;
					cnt [qid]++;
					if (cnt [qid] < ins->q.n || cnt [qid] > ins->q.m) {
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					else {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
					}
					break;
				}
				case CQANY :
				case CQATLEAST :
				case CQATMOST :
				case CQRANGE :
				case CQOUTSIDE : {
					uvlong qid = ins->q.qid;
					if (!cnt) break;
					cnt [qid]++;
					bool more = false, done = false;
					switch (ins->type) {
					case CQANY :
						done = true;
						more = true;
						break;
					case CQATLEAST :
						more = true;
						if (cnt [qid] >= ins->q.n) done = true;
						break;
					case CQATMOST :
						done = true;
						if (cnt [qid] < ins->q.n) more = true;
						break;
					case CQRANGE :
						if (cnt [qid] >= ins->q.n) done = true;
						if (cnt [qid] < ins->q.m) more = true;
						break;
					case CQOUTSIDE :
						more = true;
						if (cnt [qid] < ins->q.n || cnt [qid] > ins->q.m) done = true;
						break;
					}
					if (more) {
						if (!addthread_ex(&clist, ins->u1.right, cap, ms, cnt, nc)) goto fail;
					}
					if (done) {
						if (cap && ins->q.subid < ( uint ) ms) {
							cap [ins->q.subid].s.q  = cnt [qid];
							cap [ins->q.subid].kind = RSUB_QUANTITY;
						}
						if (!addthread_ex(&nlist, ins->u2.next, cap, ms, cnt, nc)) goto fail;
					}
					break;
				}
				default : break;
				}
			}
			if (p == end) break;
			if (nlist.len == 0) break;
			freelist(&clist);
			clist    = nlist;
			nlist    = (List) {0};
			int step = chartorune(&(rune) {0}, p);
			if (step <= 0) step = 1;
			p += step;
		}
		freelist(&clist);
		freelist(&nlist);
		if (startmatched) {
			matched = true;
			break;
		}
	}
	if (matched && match)
		for (int k = 0; k < msize && k < ms; k++) match [k] = best [k];
	free(best);
	return matched ? 1 : 0;

fail:
	freelist(&clist);
	freelist(&nlist);
	free(best);
	if (!err()) errmsg("regex execution failed");
	return -1;
}
