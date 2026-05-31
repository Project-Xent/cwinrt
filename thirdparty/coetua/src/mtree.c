#include "nexus.h"
#include "config.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

#define MTNONE (( uvlong ) -1)

typedef struct mtknob_t mtknob_t;
typedef struct mtarm_t  mtarm_t;
typedef struct mtree_t  mtree_t;

struct mtknob_t {
	uvlong  ref;
	uvlong  par;
	uvlong  inarm;
	uvlong *kids;
	uvlong *kidarms;
	uvlong  nkid;
	uvlong  capkid;
	uvlong  capkidarm;
	bool    live;
};

struct mtarm_t {
	uvlong par;
	uvlong kid;
	uvlong ref;
	bool   live;
};

struct mtree_t {
	int       arena;
	mtknob_t *knobs;
	uvlong    nknob;
	uvlong    nlknob;
	uvlong    capknob;
	mtarm_t  *arms;
	uvlong    narm;
	uvlong    nlarm;
	uvlong    caparm;
	bool      live;
};

static mtree_t *mts;
static int      mtcap;

static bool     mt_table_init(void) {
	if (mts) return true;
	mtcap = COETUA_MTREE_TABLE_SEED > 0 ? COETUA_MTREE_TABLE_SEED : 1;
	mts   = ( mtree_t * ) calloc(( size_t ) mtcap, sizeof(mtree_t));
	if (!mts) {
		errmsg("mtree: out of memory");
		mtcap = 0;
		return false;
	}
	return true;
}

static bool mt_table_grow(void) {
	int  need   = mtcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_MTREE_TABLE_SEED) newcap = COETUA_MTREE_TABLE_SEED;
	mtree_t *p = ( mtree_t * ) realloc(mts, ( size_t ) newcap * sizeof(mtree_t));
	if (!p) {
		errmsg("mtree: out of memory");
		return false;
	}
	memset(p + mtcap, 0, ( size_t ) (newcap - mtcap) * sizeof(mtree_t));
	mts   = p;
	mtcap = newcap;
	return true;
}

static mtree_t *mt_get(int tree) {
	if (!mt_table_init() || tree < 0 || tree >= mtcap || !mts [tree].live) return null;
	return &mts [tree];
}

static bool knoblive(mtree_t *t, uvlong knob) { return t && knob < t->nknob && t->knobs [knob].live; }

static bool armlive(mtree_t *t, uvlong arm) { return t && arm < t->narm && t->arms [arm].live; }

static bool badbuf(uvlong *buf, uvlong cap) { return !buf && cap; }

static bool grow_uvs(uvlong **xs, uvlong *cap, uvlong need) {
	if (*cap >= need) return true;
	uvlong newcap = nextpow2_64(need);
	if (newcap < 8) newcap = 8;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
		errmsg("mtree: capacity overflow");
		return false;
	}
	uvlong *p = ( uvlong * ) realloc(*xs, ( size_t ) newcap * sizeof(uvlong));
	if (!p) {
		errmsg("mtree: out of memory");
		return false;
	}
	*xs  = p;
	*cap = newcap;
	return true;
}

static bool grow_knobs(mtree_t *t) {
	uvlong newcap = nextpow2_64(t->nknob + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(mtknob_t))) {
		errmsg("mtree: knob capacity overflow");
		return false;
	}
	mtknob_t *p = ( mtknob_t * ) realloc(t->knobs, ( size_t ) newcap * sizeof(mtknob_t));
	if (!p) {
		errmsg("mtree: out of memory");
		return false;
	}
	memset(p + t->capknob, 0, ( size_t ) (newcap - t->capknob) * sizeof(mtknob_t));
	t->knobs   = p;
	t->capknob = newcap;
	return true;
}

static bool grow_arms(mtree_t *t) {
	uvlong newcap = nextpow2_64(t->narm + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(mtarm_t))) {
		errmsg("mtree: arm capacity overflow");
		return false;
	}
	mtarm_t *p = ( mtarm_t * ) realloc(t->arms, ( size_t ) newcap * sizeof(mtarm_t));
	if (!p) {
		errmsg("mtree: out of memory");
		return false;
	}
	t->arms   = p;
	t->caparm = newcap;
	return true;
}

static bool ensure_kid_slot(mtree_t *t, uvlong par) {
	uvlong need;
	if (!addok64(t->knobs [par].nkid, 1, &need)) {
		errmsg("mtree: kid capacity overflow");
		return false;
	}
	if (!grow_uvs(&t->knobs [par].kids, &t->knobs [par].capkid, need)) return false;
	if (!grow_uvs(&t->knobs [par].kidarms, &t->knobs [par].capkidarm, need)) return false;
	return true;
}

static bool append_kid(mtree_t *t, uvlong par, uvlong kid, uvlong arm) {
	if (!ensure_kid_slot(t, par)) return false;
	uvlong n                   = t->knobs [par].nkid++;
	t->knobs [par].kids [n]    = kid;
	t->knobs [par].kidarms [n] = arm;
	return true;
}

static bool remove_kid(mtree_t *t, uvlong par, uvlong kid, uvlong arm) {
	mtknob_t *p = &t->knobs [par];
	for (uvlong i = 0; i < p->nkid; i++) {
		if (p->kids [i] != kid || p->kidarms [i] != arm) continue;
		for (uvlong j = i + 1; j < p->nkid; j++) {
			p->kids [j - 1]    = p->kids [j];
			p->kidarms [j - 1] = p->kidarms [j];
		}
		p->nkid--;
		return true;
	}
	return false;
}

static void dead_arm(mtree_t *t, uvlong arm) {
	if (!armlive(t, arm)) return;
	t->arms [arm].live = false;
	t->nlarm--;
}

static bool rootknob(mtree_t *t, uvlong knob) {
	return knoblive(t, knob) && t->knobs [knob].par == MTNONE && t->knobs [knob].inarm == MTNONE;
}

static bool ancestor_live(mtree_t *t, uvlong anc, uvlong kid) {
	if (!knoblive(t, anc) || !knoblive(t, kid)) return false;
	for (uvlong p = kid; p != MTNONE; p = t->knobs [p].par)
		if (p == anc) return true;
	return false;
}

static uvlong copy_live_knobs(mtree_t *t, uvlong *buf, uvlong cap) {
	uvlong n = 0;
	for (uvlong i = 0; i < t->nknob; i++) {
		if (!t->knobs [i].live) continue;
		if (buf && n < cap) buf [n] = i;
		n++;
	}
	return n;
}

static uvlong copy_roots(mtree_t *t, uvlong *buf, uvlong cap) {
	uvlong n = 0;
	for (uvlong i = 0; i < t->nknob; i++) {
		if (!rootknob(t, i)) continue;
		if (buf && n < cap) buf [n] = i;
		n++;
	}
	return n;
}

static uvlong copy_kids(mtree_t *t, uvlong par, uvlong *buf, uvlong cap, bool arms) {
	mtknob_t *p = &t->knobs [par];
	uvlong    n = 0;
	for (uvlong i = 0; i < p->nkid; i++) {
		uvlong kid = p->kids [i];
		uvlong arm = p->kidarms [i];
		if (!knoblive(t, kid) || !armlive(t, arm)) continue;
		if (buf && n < cap) buf [n] = arms ? arm : kid;
		n++;
	}
	return n;
}

static uvlong *collect_subtree(mtree_t *t, uvlong knob, uvlong *np) {
	uvlong *xs    = ( uvlong * ) malloc(( size_t ) t->nknob * sizeof(uvlong));
	uvlong *stack = ( uvlong * ) malloc(( size_t ) t->nknob * sizeof(uvlong));
	if (!xs || !stack) {
		free(xs);
		free(stack);
		errmsg("mtree: out of memory");
		return null;
	}
	uvlong sp = 0, n = 0;
	stack [sp++] = knob;
	while (sp) {
		uvlong k = stack [--sp];
		if (!knoblive(t, k)) continue;
		xs [n] = k;
		n++;
		mtknob_t *p = &t->knobs [k];
		for (uvlong i = p->nkid; i > 0; i--) {
			uvlong kid = p->kids [i - 1];
			uvlong arm = p->kidarms [i - 1];
			if (knoblive(t, kid) && armlive(t, arm)) stack [sp++] = kid;
		}
	}
	free(stack);
	*np = n;
	return xs;
}

static uvlong subtree_walk(mtree_t *t, uvlong knob, uvlong *buf, uvlong cap) {
	uvlong  n;
	uvlong *xs = collect_subtree(t, knob, &n);
	if (!xs) return 0;
	for (uvlong i = 0; i < n && i < cap; i++)
		if (buf) buf [i] = xs [i];
	free(xs);
	return n;
}

int mkmtree(int arena) {
	if (!mt_table_init()) return -1;
	for (;;) {
		for (int i = 0; i < mtcap; i++) {
			if (!mts [i].live) {
				mts [i] = (mtree_t) {.arena = arena, .live = true};
				return i;
			}
		}
		if (!mt_table_grow()) return -1;
	}
}

void rmmtree(int tree) {
	mtree_t *t = mt_get(tree);
	if (!t) return;
	for (uvlong i = 0; i < t->nknob; i++) {
		free(t->knobs [i].kids);
		free(t->knobs [i].kidarms);
	}
	free(t->knobs);
	free(t->arms);
	*t = (mtree_t) {0};
}

uvlong mtroot(int tree, uvlong ref) {
	mtree_t *t = mt_get(tree);
	if (!t) {
		errmsg("mtroot: bad tree");
		return MTNONE;
	}
	if (t->nknob == t->capknob && !grow_knobs(t)) return MTNONE;
	uvlong id     = t->nknob++;
	t->knobs [id] = (mtknob_t) {.ref = ref, .par = MTNONE, .inarm = MTNONE, .live = true};
	t->nlknob++;
	return id;
}

uvlong mtknob(int tree, uvlong par, uvlong ref, uvlong armref) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, par)) {
		errmsg("mtknob: bad par");
		return MTNONE;
	}
	if (t->nknob == t->capknob && !grow_knobs(t)) return MTNONE;
	if (t->narm == t->caparm && !grow_arms(t)) return MTNONE;
	uvlong kid = t->nknob;
	uvlong arm = t->narm;
	if (!append_kid(t, par, kid, arm)) return MTNONE;
	t->knobs [kid] = (mtknob_t) {.ref = ref, .par = par, .inarm = arm, .live = true};
	t->arms [arm]  = (mtarm_t) {.par = par, .kid = kid, .ref = armref, .live = true};
	t->nknob++;
	t->nlknob++;
	t->narm++;
	t->nlarm++;
	return kid;
}

uvlong mtknobref(int tree, uvlong knob) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, knob)) {
		errmsg("mtknobref: bad knob");
		return 0;
	}
	return t->knobs [knob].ref;
}

void rmtknobref(int tree, uvlong knob, uvlong ref) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, knob)) {
		errmsg("rmtknobref: bad knob");
		return;
	}
	t->knobs [knob].ref = ref;
}

uvlong mtnknob(int tree) {
	mtree_t *t = mt_get(tree);
	return t ? t->nlknob : 0;
}

uvlong mtknobs(int tree, uvlong *buf, uvlong cap) {
	mtree_t *t = mt_get(tree);
	if (!t || badbuf(buf, cap)) {
		errmsg("mtknobs: bad tree");
		return 0;
	}
	return copy_live_knobs(t, buf, cap);
}

uvlong mtdelknob(int tree, uvlong knob) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, knob)) {
		errmsg("mtdelknob: bad knob");
		return MTNONE;
	}
	uvlong  n;
	uvlong *xs = collect_subtree(t, knob, &n);
	if (!xs) return MTNONE;
	if (!rootknob(t, knob)) remove_kid(t, t->knobs [knob].par, knob, t->knobs [knob].inarm);
	for (uvlong i = 0; i < n; i++) {
		uvlong    k = xs [i];
		mtknob_t *p = &t->knobs [k];
		if (p->inarm != MTNONE) dead_arm(t, p->inarm);
		for (uvlong j = 0; j < p->nkid; j++) dead_arm(t, p->kidarms [j]);
		p->live = false;
		t->nlknob--;
	}
	free(xs);
	return n;
}

uvlong mtarmref(int tree, uvlong arm) {
	mtree_t *t = mt_get(tree);
	if (!armlive(t, arm)) {
		errmsg("mtarmref: bad arm");
		return 0;
	}
	return t->arms [arm].ref;
}

void rmtarmref(int tree, uvlong arm, uvlong ref) {
	mtree_t *t = mt_get(tree);
	if (!armlive(t, arm)) {
		errmsg("rmtarmref: bad arm");
		return;
	}
	t->arms [arm].ref = ref;
}

uvlong mtnarm(int tree) {
	mtree_t *t = mt_get(tree);
	return t ? t->nlarm : 0;
}

bool mtisroot(int tree, uvlong knob) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, knob)) {
		errmsg("mtisroot: bad knob");
		return false;
	}
	return rootknob(t, knob);
}

uvlong mtroots(int tree, uvlong *buf, uvlong cap) {
	mtree_t *t = mt_get(tree);
	if (!t || badbuf(buf, cap)) {
		errmsg("mtroots: bad tree");
		return 0;
	}
	return copy_roots(t, buf, cap);
}

uvlong mtpar(int tree, uvlong kid) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, kid)) {
		errmsg("mtpar: bad knob");
		return MTNONE;
	}
	if (rootknob(t, kid)) {
		errmsg("mtpar: root has no par");
		return MTNONE;
	}
	return t->knobs [kid].par;
}

uvlong mtinarm(int tree, uvlong kid) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, kid)) {
		errmsg("mtinarm: bad knob");
		return MTNONE;
	}
	if (rootknob(t, kid)) {
		errmsg("mtinarm: root has no arm");
		return MTNONE;
	}
	return t->knobs [kid].inarm;
}

uvlong mtkids(int tree, uvlong par, uvlong *buf, uvlong cap) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, par) || badbuf(buf, cap)) {
		errmsg("mtkids: bad par");
		return 0;
	}
	return copy_kids(t, par, buf, cap, false);
}

uvlong mtkidarms(int tree, uvlong par, uvlong *buf, uvlong cap) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, par) || badbuf(buf, cap)) {
		errmsg("mtkidarms: bad par");
		return 0;
	}
	return copy_kids(t, par, buf, cap, true);
}

bool mtancestor(int tree, uvlong anc, uvlong kid) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, anc) || !knoblive(t, kid)) {
		errmsg("mtancestor: bad knob");
		return false;
	}
	return ancestor_live(t, anc, kid);
}

uvlong mtsubtree(int tree, uvlong knob, uvlong *buf, uvlong cap) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, knob) || badbuf(buf, cap)) {
		errmsg("mtsubtree: bad knob");
		return 0;
	}
	return subtree_walk(t, knob, buf, cap);
}

bool mtdetach(int tree, uvlong kid) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, kid)) {
		errmsg("mtdetach: bad knob");
		return false;
	}
	if (rootknob(t, kid)) {
		errmsg("mtdetach: root");
		return false;
	}
	uvlong par = t->knobs [kid].par;
	uvlong arm = t->knobs [kid].inarm;
	if (!remove_kid(t, par, kid, arm)) {
		errmsg("mtdetach: bad shape");
		return false;
	}
	dead_arm(t, arm);
	t->knobs [kid].par   = MTNONE;
	t->knobs [kid].inarm = MTNONE;
	return true;
}

bool mtmove(int tree, uvlong kid, uvlong newpar) {
	mtree_t *t = mt_get(tree);
	if (!knoblive(t, kid) || !knoblive(t, newpar)) {
		errmsg("mtmove: bad knob");
		return false;
	}
	if (rootknob(t, kid)) {
		errmsg("mtmove: root");
		return false;
	}
	if (kid == newpar || ancestor_live(t, kid, newpar)) {
		errmsg("mtmove: cycle");
		return false;
	}
	uvlong oldpar = t->knobs [kid].par;
	if (oldpar == newpar) return true;
	uvlong arm = t->knobs [kid].inarm;
	if (!ensure_kid_slot(t, newpar)) return false;
	if (!remove_kid(t, oldpar, kid, arm)) {
		errmsg("mtmove: bad shape");
		return false;
	}
	append_kid(t, newpar, kid, arm);
	t->knobs [kid].par = newpar;
	t->arms [arm].par  = newpar;
	return true;
}
