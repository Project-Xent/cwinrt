#include "nexus.h"
#include "arena.h"
#include "err.h"
#include <stddef.h>

typedef struct knot {
	uvlong yod;
	uvlong vav;
} knot;

static knot  *as_knot(uvlong k) { return ( knot * ) k; }

static uvlong knot_id(knot *k) { return ( uvlong ) k; }

static bead   empty_bead(void) { return (bead) {0, 0, 0}; }

static bool   empty(bead b) { return b.fst == 0 && b.lst == 0; }

static bool   bad_bead(bead b, char *who) {
	if ((b.fst == 0) != (b.lst == 0)) {
		errmsg(who);
		return true;
	}
	return false;
}

static void replace_neighbor(uvlong k, uvlong old, uvlong new) {
	if (!k) return;
	as_knot(k)->vav ^= old ^ new;
}

static uvlong step_raw(uvlong pre, uvlong k) {
	if (!k) return 0;
	return as_knot(k)->vav ^ pre;
}

static knot *new_knot(int arena, uvlong yod) {
	knot *k = ( knot * ) acarrel(arena, ( int ) sizeof(uvlong), sizeof(knot));
	if (!k) {
		if (!err()) errmsg("mklist: out of memory");
		return null;
	}
	k->yod = yod;
	k->vav = 0;
	return k;
}

bead mklist(int arena, uvlong *yods, uvlong n) {
	if (n == 0) return empty_bead();
	if (!yods) {
		errmsg("mklist: bad yods");
		return empty_bead();
	}

	knot *first = null;
	knot *last  = null;
	for (uvlong i = 0; i < n; i++) {
		knot *k = new_knot(arena, yods [i]);
		if (!k) return empty_bead();
		if (!first) first = k;
		if (last) {
			uvlong lastid  = knot_id(last);
			last->vav     ^= knot_id(k);
			k->vav         = lastid;
		}
		last = k;
	}
	return (bead) {0, knot_id(first), knot_id(last)};
}

uvlong lsyod(uvlong k) {
	if (!k) return 0;
	return as_knot(k)->yod;
}

void rlsyod(uvlong k, uvlong yod) {
	if (!k) return;
	as_knot(k)->yod = yod;
}

uvlong lsstep(uvlong pre, uvlong k) { return step_raw(pre, k); }

uvlong lsend(uvlong pre, uvlong k, uvlong *endpre) {
	uvlong prev = pre;
	uvlong cur  = k;
	if (!cur) {
		if (endpre) *endpre = 0;
		return 0;
	}
	for (;;) {
		uvlong next = step_raw(prev, cur);
		if (!next) {
			if (endpre) *endpre = prev;
			return cur;
		}
		prev = cur;
		cur  = next;
	}
}

uvlong lslen(uvlong pre, uvlong k) {
	uvlong n    = 0;
	uvlong prev = pre;
	uvlong cur  = k;
	while (cur) {
		uvlong next = step_raw(prev, cur);
		n++;
		prev = cur;
		cur  = next;
	}
	return n;
}

uvlong lsknots(uvlong pre, uvlong k, uvlong *buf, uvlong cap) {
	if (!buf && cap) {
		errmsg("lsknots: bad buffer");
		return 0;
	}
	uvlong n    = 0;
	uvlong prev = pre;
	uvlong cur  = k;
	while (cur) {
		uvlong next = step_raw(prev, cur);
		if (buf && n < cap) buf [n] = cur;
		n++;
		prev = cur;
		cur  = next;
	}
	return n;
}

bead lscut(bead b) {
	if (bad_bead(b, "lscut: bad bead") || empty(b)) return empty_bead();

	uvlong lastpre = b.pre;
	uvlong cur     = b.fst;
	while (cur && cur != b.lst) {
		uvlong next = step_raw(lastpre, cur);
		lastpre     = cur;
		cur         = next;
	}
	if (!cur) {
		errmsg("lscut: bad bead");
		return empty_bead();
	}

	uvlong succ = step_raw(lastpre, b.lst);
	replace_neighbor(b.pre, b.fst, succ);
	replace_neighbor(succ, b.lst, b.pre);
	replace_neighbor(b.fst, b.pre, 0);
	replace_neighbor(b.lst, succ, 0);
	return (bead) {0, b.fst, b.lst};
}

void lsput(uvlong atpre, uvlong at, bead b) {
	if (bad_bead(b, "lsput: bad bead")) return;
	if (empty(b)) return;
	if (!at) {
		errmsg("lsput: bad target");
		return;
	}
	uvlong succ = step_raw(atpre, at);
	replace_neighbor(at, succ, b.fst);
	replace_neighbor(succ, at, b.lst);
	replace_neighbor(b.fst, b.pre, at);
	replace_neighbor(b.lst, 0, succ);
}

void lscat(uvlong pre, uvlong k, bead b) {
	if (bad_bead(b, "lscat: bad bead")) return;
	if (empty(b)) return;
	uvlong endpre;
	uvlong end = lsend(pre, k, &endpre);
	if (!end) {
		errmsg("lscat: bad target");
		return;
	}
	lsput(endpre, end, b);
}

void lssplice(uvlong atpre, uvlong at, bead b) {
	if (bad_bead(b, "lssplice: bad bead")) return;
	if (empty(b)) return;
	bead cut = lscut(b);
	if (!empty(cut)) lsput(atpre, at, cut);
}

static void clear_walk(uvlong pre, uvlong cur) {
	while (cur) {
		knot  *k    = as_knot(cur);
		uvlong next = k->vav ^ pre;
		k->yod      = 0;
		k->vav      = 0;
		pre         = cur;
		cur         = next;
	}
}

void rmlist(int arena, uvlong u, uvlong v) {
	( void ) arena;
	if (!u && !v) return;
	clear_walk(v, u);
	clear_walk(u, v);
}
