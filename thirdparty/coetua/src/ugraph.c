#include "nexus.h"
#include "config.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

typedef struct ugdot_t  ugdot_t;
typedef struct ugarc_t  ugarc_t;
typedef struct ugraph_t ugraph_t;

struct ugdot_t {
	uvlong  ref;
	uvlong *arcs;
	uvlong  narc;
	uvlong  caparc;
	bool    live;
};

struct ugarc_t {
	uvlong a;
	uvlong b;
	uvlong ref;
	bool   live;
};

struct ugraph_t {
	int      arena;
	ugdot_t *dots;
	uvlong   ndot;
	uvlong   nldot;
	uvlong   capdot;
	ugarc_t *arcs;
	uvlong   narc;
	uvlong   nlarc;
	uvlong   caparc;
	bool     live;
};

static ugraph_t *ugs;
static int       ugcap;

static bool      ug_table_init(void) {
	if (ugs) return true;
	ugcap = COETUA_UGRAPH_TABLE_SEED > 0 ? COETUA_UGRAPH_TABLE_SEED : 1;
	ugs   = ( ugraph_t * ) calloc(( size_t ) ugcap, sizeof(ugraph_t));
	if (!ugs) {
		errmsg("ugraph: out of memory");
		ugcap = 0;
		return false;
	}
	return true;
}

static bool ug_table_grow(void) {
	int  need   = ugcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_UGRAPH_TABLE_SEED) newcap = COETUA_UGRAPH_TABLE_SEED;
	ugraph_t *p = ( ugraph_t * ) realloc(ugs, ( size_t ) newcap * sizeof(ugraph_t));
	if (!p) {
		errmsg("ugraph: out of memory");
		return false;
	}
	memset(p + ugcap, 0, ( size_t ) (newcap - ugcap) * sizeof(ugraph_t));
	ugs   = p;
	ugcap = newcap;
	return true;
}

static ugraph_t *ug_get(int graph) {
	if (!ug_table_init() || graph < 0 || graph >= ugcap || !ugs [graph].live) return null;
	return &ugs [graph];
}

static bool dotlive(ugraph_t *g, uvlong dot) { return g && dot < g->ndot && g->dots [dot].live; }

static bool arclive(ugraph_t *g, uvlong arc) { return g && arc < g->narc && g->arcs [arc].live; }

static bool badbuf(uvlong *buf, uvlong cap) { return !buf && cap; }

static bool grow_uvs(uvlong **xs, uvlong *cap, uvlong need) {
	if (*cap >= need) return true;
	uvlong newcap = nextpow2_64(need);
	if (newcap < 8) newcap = 8;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
		errmsg("ugraph: capacity overflow");
		return false;
	}
	uvlong *p = ( uvlong * ) realloc(*xs, ( size_t ) newcap * sizeof(uvlong));
	if (!p) {
		errmsg("ugraph: out of memory");
		return false;
	}
	*xs  = p;
	*cap = newcap;
	return true;
}

static bool grow_dots(ugraph_t *g) {
	uvlong newcap = nextpow2_64(g->ndot + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(ugdot_t))) {
		errmsg("ugraph: dot capacity overflow");
		return false;
	}
	ugdot_t *p = ( ugdot_t * ) realloc(g->dots, ( size_t ) newcap * sizeof(ugdot_t));
	if (!p) {
		errmsg("ugraph: out of memory");
		return false;
	}
	memset(p + g->capdot, 0, ( size_t ) (newcap - g->capdot) * sizeof(ugdot_t));
	g->dots   = p;
	g->capdot = newcap;
	return true;
}

static bool grow_arcs(ugraph_t *g) {
	uvlong newcap = nextpow2_64(g->narc + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(ugarc_t))) {
		errmsg("ugraph: arc capacity overflow");
		return false;
	}
	ugarc_t *p = ( ugarc_t * ) realloc(g->arcs, ( size_t ) newcap * sizeof(ugarc_t));
	if (!p) {
		errmsg("ugraph: out of memory");
		return false;
	}
	g->arcs   = p;
	g->caparc = newcap;
	return true;
}

static bool append_incident(ugraph_t *g, uvlong dot, uvlong arc) {
	uvlong need;
	if (!addok64(g->dots [dot].narc, 1, &need)) {
		errmsg("ugraph: incident capacity overflow");
		return false;
	}
	if (!grow_uvs(&g->dots [dot].arcs, &g->dots [dot].caparc, need)) return false;
	g->dots [dot].arcs [g->dots [dot].narc++] = arc;
	return true;
}

static bool   same_ends(ugarc_t *e, uvlong a, uvlong b) { return (e->a == a && e->b == b) || (e->a == b && e->b == a); }

static uvlong other_dot(ugarc_t *e, uvlong dot) { return e->a == dot ? e->b : e->a; }

static uvlong copy_live_dots(ugraph_t *g, uvlong *buf, uvlong cap) {
	uvlong n = 0;
	for (uvlong i = 0; i < g->ndot; i++) {
		if (!g->dots [i].live) continue;
		if (buf && n < cap) buf [n] = i;
		n++;
	}
	return n;
}

static uvlong copy_live_arcs(ugraph_t *g, uvlong *buf, uvlong cap) {
	uvlong n = 0;
	for (uvlong i = 0; i < g->narc; i++) {
		if (!g->arcs [i].live) continue;
		if (buf && n < cap) buf [n] = i;
		n++;
	}
	return n;
}

static bool delarc_live(ugraph_t *g, uvlong arc) {
	if (!arclive(g, arc)) return false;
	g->arcs [arc].live = false;
	g->nlarc--;
	return true;
}

static uvlong comp_walk(ugraph_t *g, uvlong dot, uvlong *buf, uvlong cap, bool stop_at_target, uvlong target,
                        bool *hitp) {
	bool   *seen  = ( bool * ) calloc(( size_t ) g->ndot, sizeof(bool));
	uvlong *queue = ( uvlong * ) malloc(( size_t ) g->ndot * sizeof(uvlong));
	if (!seen || !queue) {
		free(seen);
		free(queue);
		errmsg("ugraph: out of memory");
		return 0;
	}
	uvlong head = 0, tail = 0, n = 0;
	queue [tail++] = dot;
	seen [dot]     = true;
	if (hitp) *hitp = dot == target;
	while (head < tail) {
		uvlong d = queue [head++];
		if (buf && n < cap) buf [n] = d;
		n++;
		if (stop_at_target && d == target) {
			if (hitp) *hitp = true;
			break;
		}
		ugdot_t *p = &g->dots [d];
		for (uvlong i = 0; i < p->narc; i++) {
			uvlong   a = p->arcs [i];
			ugarc_t *e = &g->arcs [a];
			if (!e->live) continue;
			uvlong od = other_dot(e, d);
			if (!seen [od]) {
				seen [od]      = true;
				queue [tail++] = od;
			}
		}
	}
	free(seen);
	free(queue);
	return n;
}

int mkugraph(int arena) {
	if (!ug_table_init()) return -1;
	for (;;) {
		for (int i = 0; i < ugcap; i++) {
			if (!ugs [i].live) {
				ugs [i] = (ugraph_t) {.arena = arena, .live = true};
				return i;
			}
		}
		if (!ug_table_grow()) return -1;
	}
}

void rmugraph(int graph) {
	ugraph_t *g = ug_get(graph);
	if (!g) return;
	for (uvlong i = 0; i < g->ndot; i++) free(g->dots [i].arcs);
	free(g->dots);
	free(g->arcs);
	*g = (ugraph_t) {0};
}

uvlong ugdot(int graph, uvlong ref) {
	ugraph_t *g = ug_get(graph);
	if (!g) {
		errmsg("ugdot: bad graph");
		return ( uvlong ) -1;
	}
	if (g->ndot == g->capdot && !grow_dots(g)) return ( uvlong ) -1;
	uvlong id    = g->ndot++;
	g->dots [id] = (ugdot_t) {.ref = ref, .live = true};
	g->nldot++;
	return id;
}

uvlong ugdotref(int graph, uvlong dot) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, dot)) {
		errmsg("ugdotref: bad dot");
		return 0;
	}
	return g->dots [dot].ref;
}

void rugdotref(int graph, uvlong dot, uvlong ref) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, dot)) {
		errmsg("rugdotref: bad dot");
		return;
	}
	g->dots [dot].ref = ref;
}

uvlong ugndot(int graph) {
	ugraph_t *g = ug_get(graph);
	return g ? g->nldot : 0;
}

uvlong ugdots(int graph, uvlong *buf, uvlong cap) {
	ugraph_t *g = ug_get(graph);
	if (!g || badbuf(buf, cap)) {
		errmsg("ugdots: bad graph");
		return 0;
	}
	return copy_live_dots(g, buf, cap);
}

uvlong ugdeldot(int graph, uvlong dot) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, dot)) {
		errmsg("ugdeldot: bad dot");
		return ( uvlong ) -1;
	}
	uvlong n = 0;
	for (uvlong i = 0; i < g->dots [dot].narc; i++)
		if (delarc_live(g, g->dots [dot].arcs [i])) n++;
	g->dots [dot].live = false;
	g->nldot--;
	return n;
}

uvlong ugarc(int graph, uvlong a, uvlong b, uvlong ref) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, a) || !dotlive(g, b)) {
		errmsg("ugarc: bad dot");
		return ( uvlong ) -1;
	}
	if (g->narc == g->caparc && !grow_arcs(g)) return ( uvlong ) -1;
	uvlong id    = g->narc++;
	g->arcs [id] = (ugarc_t) {.a = a, .b = b, .ref = ref, .live = true};
	if (!append_incident(g, a, id)) {
		g->arcs [id].live = false;
		g->narc--;
		return ( uvlong ) -1;
	}
	if (a != b && !append_incident(g, b, id)) {
		g->arcs [id].live = false;
		g->dots [a].narc--;
		g->narc--;
		return ( uvlong ) -1;
	}
	g->nlarc++;
	return id;
}

uvlong ugarcref(int graph, uvlong arc) {
	ugraph_t *g = ug_get(graph);
	if (!arclive(g, arc)) {
		errmsg("ugarcref: bad arc");
		return 0;
	}
	return g->arcs [arc].ref;
}

void rugarcref(int graph, uvlong arc, uvlong ref) {
	ugraph_t *g = ug_get(graph);
	if (!arclive(g, arc)) {
		errmsg("rugarcref: bad arc");
		return;
	}
	g->arcs [arc].ref = ref;
}

uvlong ugnarc(int graph) {
	ugraph_t *g = ug_get(graph);
	return g ? g->nlarc : 0;
}

uvlong ugarcs(int graph, uvlong *buf, uvlong cap) {
	ugraph_t *g = ug_get(graph);
	if (!g || badbuf(buf, cap)) {
		errmsg("ugarcs: bad graph");
		return 0;
	}
	return copy_live_arcs(g, buf, cap);
}

bool ugends(int graph, uvlong arc, uvlong *a, uvlong *b) {
	ugraph_t *g = ug_get(graph);
	if (!a || !b) {
		errmsg("ugends: bad argument");
		return false;
	}
	if (!arclive(g, arc)) {
		errmsg("ugends: bad arc");
		return false;
	}
	*a = g->arcs [arc].a;
	*b = g->arcs [arc].b;
	return true;
}

bool ugdelarc(int graph, uvlong arc) {
	ugraph_t *g = ug_get(graph);
	if (!arclive(g, arc)) {
		errmsg("ugdelarc: bad arc");
		return false;
	}
	return delarc_live(g, arc);
}

uvlong uglinked(int graph, uvlong a, uvlong b) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, a) || !dotlive(g, b)) {
		errmsg("uglinked: bad dot");
		return 0;
	}
	uvlong   n = 0;
	ugdot_t *p = &g->dots [a];
	for (uvlong i = 0; i < p->narc; i++) {
		ugarc_t *e = &g->arcs [p->arcs [i]];
		if (e->live && same_ends(e, a, b)) n++;
	}
	return n;
}

uvlong ugadots(int graph, uvlong dot, uvlong *buf, uvlong cap) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, dot) || badbuf(buf, cap)) {
		errmsg("ugadots: bad dot");
		return 0;
	}
	uvlong   n = 0;
	ugdot_t *p = &g->dots [dot];
	for (uvlong i = 0; i < p->narc; i++) {
		ugarc_t *e = &g->arcs [p->arcs [i]];
		if (!e->live) continue;
		if (e->a == e->b) {
			if (buf && n < cap) buf [n] = dot;
			n++;
			if (buf && n < cap) buf [n] = dot;
			n++;
		}
		else {
			if (buf && n < cap) buf [n] = other_dot(e, dot);
			n++;
		}
	}
	return n;
}

uvlong ugaarcs(int graph, uvlong dot, uvlong *buf, uvlong cap) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, dot) || badbuf(buf, cap)) {
		errmsg("ugaarcs: bad dot");
		return 0;
	}
	uvlong   n = 0;
	ugdot_t *p = &g->dots [dot];
	for (uvlong i = 0; i < p->narc; i++) {
		uvlong a = p->arcs [i];
		if (!g->arcs [a].live) continue;
		if (buf && n < cap) buf [n] = a;
		n++;
	}
	return n;
}

uvlong ugdeg(int graph, uvlong dot) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, dot)) {
		errmsg("ugdeg: bad dot");
		return 0;
	}
	uvlong   n = 0;
	ugdot_t *p = &g->dots [dot];
	for (uvlong i = 0; i < p->narc; i++) {
		ugarc_t *e = &g->arcs [p->arcs [i]];
		if (!e->live) continue;
		n += e->a == e->b ? 2 : 1;
	}
	return n;
}

bool ugreaches(int graph, uvlong a, uvlong b) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, a) || !dotlive(g, b)) {
		errmsg("ugreaches: bad dot");
		return false;
	}
	bool hit = false;
	comp_walk(g, a, null, 0, true, b, &hit);
	return hit;
}

uvlong ugcomp(int graph, uvlong dot, uvlong *buf, uvlong cap) {
	ugraph_t *g = ug_get(graph);
	if (!dotlive(g, dot) || badbuf(buf, cap)) {
		errmsg("ugcomp: bad dot");
		return 0;
	}
	return comp_walk(g, dot, buf, cap, false, 0, null);
}
