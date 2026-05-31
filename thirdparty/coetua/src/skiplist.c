#include "nexus.h"
#include "config.h"
#include "err.h"
#include "rand.h"
#include <stdlib.h>
#include <string.h>

#define SKMAXLAYER 64
#define SKNONE     (( uvlong ) -1)

typedef struct skpin_t skpin_t;
typedef struct skip_t  skip_t;
typedef int            (*skcmpfn)(uvlong, uvlong, void *);

struct skpin_t {
	uvlong  yod;
	uvlong *knots;
	uvlong *preds;
	uvlong  height;
	bool    live;
};

struct skip_t {
	int      arena;
	uvlong   heads [SKMAXLAYER];
	uvlong   nlayer;
	skpin_t *pins;
	uvlong   npin;
	uvlong   nlpin;
	uvlong   cappin;
	bool     live;
};

static skip_t *sks;
static int     skcap;

static bool    sk_table_init(void) {
	if (sks) return true;
	skcap = COETUA_SKIPLIST_TABLE_SEED > 0 ? COETUA_SKIPLIST_TABLE_SEED : 1;
	sks   = ( skip_t * ) calloc(( size_t ) skcap, sizeof(skip_t));
	if (!sks) {
		errmsg("skiplist: out of memory");
		skcap = 0;
		return false;
	}
	return true;
}

static bool sk_table_grow(void) {
	int  need   = skcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_SKIPLIST_TABLE_SEED) newcap = COETUA_SKIPLIST_TABLE_SEED;
	skip_t *p = ( skip_t * ) realloc(sks, ( size_t ) newcap * sizeof(skip_t));
	if (!p) {
		errmsg("skiplist: out of memory");
		return false;
	}
	memset(p + skcap, 0, ( size_t ) (newcap - skcap) * sizeof(skip_t));
	sks   = p;
	skcap = newcap;
	return true;
}

static skip_t *sk_get(int skip) {
	if (!sk_table_init() || skip < 0 || skip >= skcap || !sks [skip].live) return null;
	return &sks [skip];
}

static bool pinlive(skip_t *sk, uvlong pin) { return sk && pin < sk->npin && sk->pins [pin].live; }

static bool badbuf(uvlong *buf, uvlong cap) { return !buf && cap; }

static bool ensure_pin_cap(skip_t *sk) {
	if (sk->npin < sk->cappin) return true;
	uvlong newcap = nextpow2_64(sk->npin + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(skpin_t))) {
		errmsg("skiplist: pin capacity overflow");
		return false;
	}
	skpin_t *p = ( skpin_t * ) realloc(sk->pins, ( size_t ) newcap * sizeof(skpin_t));
	if (!p) {
		errmsg("skiplist: out of memory");
		return false;
	}
	memset(p + sk->cappin, 0, ( size_t ) (newcap - sk->cappin) * sizeof(skpin_t));
	sk->pins   = p;
	sk->cappin = newcap;
	return true;
}

static bool make_head(skip_t *sk, uvlong level) {
	if (sk->heads [level]) return true;
	uvlong y = 0;
	bead   b = mklist(sk->arena, &y, 1);
	if (!b.fst) {
		if (!err()) errmsg("skiplist: out of memory");
		return false;
	}
	sk->heads [level] = b.fst;
	return true;
}

static bool ensure_layers(skip_t *sk, uvlong nlayer) {
	if (nlayer > SKMAXLAYER) nlayer = SKMAXLAYER;
	for (uvlong i = sk->nlayer; i < nlayer; i++)
		if (!make_head(sk, i)) return false;
	if (sk->nlayer < nlayer) sk->nlayer = nlayer;
	return true;
}

static uvlong random_height(void) { return ctz64(qrand64() << 1); }

static int    cmp_pin(skip_t *sk, uvlong knot, skcmpfn cmp, uvlong chet, void *arg) {
	uvlong pin = lsyod(knot);
	return cmp(sk->pins [pin].yod, chet, arg);
}

static bool find_update(skip_t *sk, uvlong chet, skcmpfn cmp, void *arg, bool after_equals, uvlong *upre, uvlong *uat) {
	if (!sk || !cmp || sk->nlayer == 0) return false;
	uvlong at  = sk->heads [sk->nlayer - 1];
	uvlong pre = 0;
	for (uvlong li = sk->nlayer; li > 0; li--) {
		uvlong level = li - 1;
		if (li != sk->nlayer) {
			uvlong atpin = at == sk->heads [level + 1] ? SKNONE : lsyod(at);
			if (atpin == SKNONE) {
				at  = sk->heads [level];
				pre = 0;
			}
			else {
				at  = sk->pins [atpin].knots [level];
				pre = sk->pins [atpin].preds [level];
			}
		}
		for (;;) {
			uvlong next = lsstep(pre, at);
			if (!next) break;
			int c = cmp_pin(sk, next, cmp, chet, arg);
			if (after_equals ? c > 0 : c >= 0) break;
			pre = at;
			at  = next;
		}
		if (upre) upre [level] = pre;
		if (uat) uat [level] = at;
	}
	return true;
}

static void rebuild_preds(skip_t *sk) {
	if (!sk) return;
	for (uvlong i = 0; i < sk->npin; i++) {
		if (!sk->pins [i].live) continue;
		for (uvlong l = 0; l < sk->pins [i].height; l++) sk->pins [i].preds [l] = 0;
	}
	for (uvlong l = 0; l < sk->nlayer; l++) {
		uvlong pre = 0;
		uvlong cur = sk->heads [l];
		for (;;) {
			uvlong next = lsstep(pre, cur);
			if (!next) break;
			uvlong pin = lsyod(next);
			if (pinlive(sk, pin) && l < sk->pins [pin].height) {
				sk->pins [pin].knots [l] = next;
				sk->pins [pin].preds [l] = cur;
			}
			pre = cur;
			cur = next;
		}
	}
}

static bool alloc_pin_arrays(skpin_t *p, uvlong h) {
	p->knots = ( uvlong * ) calloc(( size_t ) h, sizeof(uvlong));
	p->preds = ( uvlong * ) calloc(( size_t ) h, sizeof(uvlong));
	if (!p->knots || !p->preds) {
		free(p->knots);
		free(p->preds);
		p->knots = null;
		p->preds = null;
		errmsg("skiplist: out of memory");
		return false;
	}
	return true;
}

static void free_pin(skpin_t *p) {
	free(p->knots);
	free(p->preds);
	*p = (skpin_t) {0};
}

int mkskiplist(int arena) {
	if (!sk_table_init()) return -1;
	for (;;) {
		for (int i = 0; i < skcap; i++) {
			if (sks [i].live) continue;
			skip_t sk = {.arena = arena, .live = true};
			if (!ensure_layers(&sk, 1)) return -1;
			sks [i] = sk;
			return i;
		}
		if (!sk_table_grow()) return -1;
	}
}

void rmskiplist(int skip) {
	skip_t *sk = sk_get(skip);
	if (!sk) return;
	for (uvlong l = 0; l < sk->nlayer; l++)
		if (sk->heads [l]) rmlist(sk->arena, 0, sk->heads [l]);
	for (uvlong i = 0; i < sk->npin; i++) free_pin(&sk->pins [i]);
	free(sk->pins);
	*sk = (skip_t) {0};
}

uvlong skput(int skip, uvlong yod, int (*cmp)(uvlong yod, uvlong chet, void *arg), void *arg) {
	skip_t *sk = sk_get(skip);
	if (!sk || !cmp) {
		errmsg("skput: bad argument");
		return SKNONE;
	}
	uvlong h = random_height();
	if (!ensure_layers(sk, h) || !ensure_pin_cap(sk)) return SKNONE;

	uvlong upre [SKMAXLAYER] = {0};
	uvlong uat [SKMAXLAYER]  = {0};
	find_update(sk, yod, cmp, arg, true, upre, uat);

	uvlong  pin = sk->npin;
	skpin_t p   = {.yod = yod, .height = h, .live = true};
	if (!alloc_pin_arrays(&p, h)) return SKNONE;

	uvlong made [SKMAXLAYER] = {0};
	for (uvlong l = 0; l < h; l++) {
		uvlong py = pin;
		bead   b  = mklist(sk->arena, &py, 1);
		if (!b.fst) {
			free_pin(&p);
			for (uvlong j = 0; j < l; j++) rmlist(sk->arena, 0, made [j]);
			return SKNONE;
		}
		p.knots [l] = b.fst;
		made [l]    = b.fst;
	}
	sk->pins [pin] = p;
	sk->npin++;
	sk->nlpin++;
	for (uvlong l = 0; l < h; l++) lsput(upre [l], uat [l], (bead) {.pre = 0, .fst = p.knots [l], .lst = p.knots [l]});
	rebuild_preds(sk);
	return pin;
}

bool skdel(int skip, uvlong pin) {
	skip_t *sk = sk_get(skip);
	if (!pinlive(sk, pin)) {
		errmsg("skdel: bad pin");
		return false;
	}
	skpin_t *p = &sk->pins [pin];
	for (uvlong l = 0; l < p->height; l++) {
		bead b = lscut((bead) {.pre = p->preds [l], .fst = p->knots [l], .lst = p->knots [l]});
		rmlist(sk->arena, 0, b.fst);
	}
	free(p->knots);
	free(p->preds);
	p->knots = null;
	p->preds = null;
	p->live  = false;
	sk->nlpin--;
	rebuild_preds(sk);
	return true;
}

uvlong skdels(int skip, uvlong pin, int (*cmp)(uvlong yod, uvlong chet, void *arg), void *arg) {
	skip_t *sk = sk_get(skip);
	if (!pinlive(sk, pin) || !cmp) {
		errmsg("skdels: bad argument");
		return 0;
	}
	uvlong chet = sk->pins [pin].yod;
	uvlong n    = 0;
	uvlong cur  = pin;
	for (;;) {
		if (!pinlive(sk, cur) || cmp(sk->pins [cur].yod, chet, arg) != 0) break;
		uvlong next = 0;
		bool   has  = sknext(skip, cur, &next);
		skdel(skip, cur);
		n++;
		if (!has) break;
		cur = next;
	}
	return n;
}

uvlong skyod(int skip, uvlong pin) {
	skip_t *sk = sk_get(skip);
	if (!pinlive(sk, pin)) {
		errmsg("skyod: bad pin");
		return 0;
	}
	return sk->pins [pin].yod;
}

uvlong sknpin(int skip) {
	skip_t *sk = sk_get(skip);
	return sk ? sk->nlpin : 0;
}

uvlong skpins(int skip, uvlong *buf, uvlong cap) {
	skip_t *sk = sk_get(skip);
	if (!sk || badbuf(buf, cap)) {
		errmsg("skpins: bad skiplist");
		return 0;
	}
	uvlong n   = 0;
	uvlong pre = sk->heads [0];
	uvlong cur = lsstep(0, pre);
	while (cur) {
		if (buf && n < cap) buf [n] = lsyod(cur);
		n++;
		uvlong next = lsstep(pre, cur);
		pre         = cur;
		cur         = next;
	}
	return n;
}

static uvlong first_after(skip_t *sk, uvlong chet, skcmpfn cmp, void *arg, bool upper) {
	uvlong upre [SKMAXLAYER] = {0};
	uvlong uat [SKMAXLAYER]  = {0};
	find_update(sk, chet, cmp, arg, upper, upre, uat);
	uvlong k = lsstep(upre [0], uat [0]);
	if (!k) {
		errmsg("skiplist: no pin");
		return SKNONE;
	}
	return lsyod(k);
}

uvlong sklower(int skip, uvlong chet, int (*cmp)(uvlong yod, uvlong chet, void *arg), void *arg) {
	skip_t *sk = sk_get(skip);
	if (!sk || !cmp) {
		errmsg("sklower: bad argument");
		return SKNONE;
	}
	return first_after(sk, chet, cmp, arg, false);
}

uvlong skupper(int skip, uvlong chet, int (*cmp)(uvlong yod, uvlong chet, void *arg), void *arg) {
	skip_t *sk = sk_get(skip);
	if (!sk || !cmp) {
		errmsg("skupper: bad argument");
		return SKNONE;
	}
	return first_after(sk, chet, cmp, arg, true);
}

bool skfind(int skip, uvlong chet, int (*cmp)(uvlong yod, uvlong chet, void *arg), void *arg, uvlong *pin) {
	skip_t *sk = sk_get(skip);
	if (!sk || !cmp) {
		errmsg("skfind: bad argument");
		return false;
	}
	uvlong upre [SKMAXLAYER] = {0};
	uvlong uat [SKMAXLAYER]  = {0};
	find_update(sk, chet, cmp, arg, false, upre, uat);
	uvlong k = lsstep(upre [0], uat [0]);
	if (!k) return false;
	uvlong found = lsyod(k);
	if (cmp(sk->pins [found].yod, chet, arg) != 0) return false;
	if (pin) *pin = found;
	return true;
}

uvlong skfirst(int skip) {
	skip_t *sk = sk_get(skip);
	if (!sk) {
		errmsg("skfirst: bad skiplist");
		return SKNONE;
	}
	uvlong k = lsstep(0, sk->heads [0]);
	if (!k) {
		errmsg("skfirst: empty skiplist");
		return SKNONE;
	}
	return lsyod(k);
}

uvlong sklast(int skip) {
	skip_t *sk = sk_get(skip);
	if (!sk) {
		errmsg("sklast: bad skiplist");
		return SKNONE;
	}
	uvlong end = lsend(0, sk->heads [0], null);
	if (end == sk->heads [0]) {
		errmsg("sklast: empty skiplist");
		return SKNONE;
	}
	return lsyod(end);
}

bool sknext(int skip, uvlong pin, uvlong *next) {
	skip_t *sk = sk_get(skip);
	if (!pinlive(sk, pin)) {
		errmsg("sknext: bad pin");
		return false;
	}
	uvlong k = lsstep(sk->pins [pin].preds [0], sk->pins [pin].knots [0]);
	if (!k) return false;
	if (next) *next = lsyod(k);
	return true;
}

bool skprev(int skip, uvlong pin, uvlong *prev) {
	skip_t *sk = sk_get(skip);
	if (!pinlive(sk, pin)) {
		errmsg("skprev: bad pin");
		return false;
	}
	uvlong p = sk->pins [pin].preds [0];
	if (p == sk->heads [0]) return false;
	if (prev) *prev = lsyod(p);
	return true;
}
