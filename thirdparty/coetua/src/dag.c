#include "nexus.h"
#include "arena.h"
#include "config.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

typedef struct dot dot;
typedef struct arc arc;
typedef struct dag dag_t;

struct dot {
	uvlong  ref;
	uvlong *outs;
	uvlong  nout;
	uvlong  capout;
	uvlong *ins;
	uvlong  nin;
	uvlong  capin;
};

struct arc {
	uvlong from;
	uvlong to;
	uvlong ref;
};

struct dag {
	int    arena;
	dot   *dots;
	uvlong ndot;
	uvlong capdot;
	arc   *arcs;
	uvlong narc;
	uvlong caparc;
	bool   live;
};

static dag_t *dags;
static int    dagcap;

static bool   dag_table_init(void) {
	if (dags) return true;
	dagcap = COETUA_DAG_TABLE_SEED > 0 ? COETUA_DAG_TABLE_SEED : 1;
	dags   = ( dag_t * ) calloc(( size_t ) dagcap, sizeof(dag_t));
	if (!dags) {
		errmsg("dag: out of memory");
		dagcap = 0;
		return false;
	}
	return true;
}

static bool dag_table_grow(void) {
	int  need   = dagcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_DAG_TABLE_SEED) newcap = COETUA_DAG_TABLE_SEED;
	dag_t *p = ( dag_t * ) realloc(dags, ( size_t ) newcap * sizeof(dag_t));
	if (!p) {
		errmsg("dag: out of memory");
		return false;
	}
	memset(p + dagcap, 0, ( size_t ) (newcap - dagcap) * sizeof(dag_t));
	dags   = p;
	dagcap = newcap;
	return true;
}

static dag_t *dag_get(int dag) {
	if (!dag_table_init() || dag < 0 || dag >= dagcap || !dags [dag].live) return null;
	return &dags [dag];
}

static bool dotok(dag_t *d, uvlong dot) { return d && dot < d->ndot; }

static bool arcok(dag_t *d, uvlong arc) { return d && arc < d->narc; }

static bool badbuf(uvlong *buf, uvlong cap) { return !buf && cap; }

static bool grow_uvs(uvlong **xs, uvlong *cap, uvlong need) {
	if (*cap >= need) return true;
	uvlong newcap = nextpow2_64(need);
	if (newcap < 8) newcap = 8;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
		errmsg("dag: capacity overflow");
		return false;
	}
	uvlong *p = ( uvlong * ) realloc(*xs, ( size_t ) newcap * sizeof(uvlong));
	if (!p) {
		errmsg("dag: out of memory");
		return false;
	}
	*xs  = p;
	*cap = newcap;
	return true;
}

static bool grow_dots(dag_t *d) {
	uvlong newcap = nextpow2_64(d->ndot + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(dot))) {
		errmsg("dag: dot capacity overflow");
		return false;
	}
	dot *p = ( dot * ) realloc(d->dots, ( size_t ) newcap * sizeof(dot));
	if (!p) {
		errmsg("dag: out of memory");
		return false;
	}
	memset(p + d->capdot, 0, ( size_t ) (newcap - d->capdot) * sizeof(dot));
	d->dots   = p;
	d->capdot = newcap;
	return true;
}

static bool grow_arcs(dag_t *d) {
	uvlong newcap = nextpow2_64(d->narc + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(arc))) {
		errmsg("dag: arc capacity overflow");
		return false;
	}
	arc *p = ( arc * ) realloc(d->arcs, ( size_t ) newcap * sizeof(arc));
	if (!p) {
		errmsg("dag: out of memory");
		return false;
	}
	d->arcs   = p;
	d->caparc = newcap;
	return true;
}

static bool reserve_arc_slot(dag_t *d, uvlong from, uvlong to) {
	uvlong need;
	if (d->narc == d->caparc && !grow_arcs(d)) return false;
	if (!addok64(d->dots [from].nout, 1, &need)) {
		errmsg("dag: arc capacity overflow");
		return false;
	}
	if (!grow_uvs(&d->dots [from].outs, &d->dots [from].capout, need)) return false;
	if (!addok64(d->dots [to].nin, 1, &need)) {
		errmsg("dag: arc capacity overflow");
		return false;
	}
	if (!grow_uvs(&d->dots [to].ins, &d->dots [to].capin, need)) return false;
	return true;
}

static uvlong findarc(dag_t *d, uvlong from, uvlong to) {
	if (!dotok(d, from) || !dotok(d, to)) return ( uvlong ) -1;
	dot *p = &d->dots [from];
	for (uvlong i = 0; i < p->nout; i++) {
		uvlong a = p->outs [i];
		if (d->arcs [a].to == to) return a;
	}
	return ( uvlong ) -1;
}

static uvlong arckid(dag_t *d, uvlong arc) { return d->arcs [arc].to; }

static uvlong arcpar(dag_t *d, uvlong arc) { return d->arcs [arc].from; }

static bool   reaches(dag_t *d, uvlong from, uvlong to) {
	if (from == to) return true;
	bool   *seen  = ( bool * ) calloc(( size_t ) d->ndot, sizeof(bool));
	uvlong *stack = ( uvlong * ) malloc(( size_t ) d->ndot * sizeof(uvlong));
	if (!seen || !stack) {
		free(seen);
		free(stack);
		errmsg("dag: out of memory");
		return false;
	}
	uvlong nstack    = 0;
	stack [nstack++] = from;
	seen [from]      = true;
	while (nstack > 0) {
		uvlong n = stack [--nstack];
		dot   *p = &d->dots [n];
		for (uvlong i = 0; i < p->nout; i++) {
			uvlong kid = d->arcs [p->outs [i]].to;
			if (kid == to) {
				free(seen);
				free(stack);
				return true;
			}
			if (!seen [kid]) {
				seen [kid]       = true;
				stack [nstack++] = kid;
			}
		}
	}
	free(seen);
	free(stack);
	return false;
}

static uvlong copy_uvs(uvlong *src, uvlong n, uvlong *buf, uvlong cap) {
	if (buf) {
		uvlong m = n < cap ? n : cap;
		for (uvlong i = 0; i < m; i++) buf [i] = src [i];
	}
	return n;
}

static uvlong copy_arc_dots(dag_t *d, uvlong *arcs, uvlong n, uvlong *buf, uvlong cap, bool kids) {
	if (buf) {
		uvlong m = n < cap ? n : cap;
		for (uvlong i = 0; i < m; i++) buf [i] = kids ? arckid(d, arcs [i]) : arcpar(d, arcs [i]);
	}
	return n;
}

static uvlong copy_boundary(dag_t *d, uvlong *buf, uvlong cap, bool roots) {
	uvlong n = 0;
	for (uvlong i = 0; i < d->ndot; i++) {
		bool hit = roots ? d->dots [i].nin == 0 : d->dots [i].nout == 0;
		if (!hit) continue;
		if (buf && n < cap) buf [n] = i;
		n++;
	}
	return n;
}

static bool topo_ready(dag_t *d, uvlong **indegp, uvlong **queuep) {
	uvlong  n     = d->ndot;
	uvlong *indeg = ( uvlong * ) malloc(( size_t ) n * sizeof(uvlong));
	uvlong *queue = ( uvlong * ) malloc(( size_t ) n * sizeof(uvlong));
	if (!indeg || !queue) {
		free(indeg);
		free(queue);
		errmsg("dgtopo: out of memory");
		return false;
	}
	for (uvlong i = 0; i < n; i++) indeg [i] = d->dots [i].nin;
	*indegp = indeg;
	*queuep = queue;
	return true;
}

static void free_topo(uvlong *indeg, uvlong *queue) {
	free(indeg);
	free(queue);
}

int mkdag(int arena) {
	if (!dag_table_init()) return -1;
	for (;;) {
		for (int i = 0; i < dagcap; i++) {
			if (!dags [i].live) {
				dags [i] = (dag_t) {.arena = arena, .live = true};
				return i;
			}
		}
		if (!dag_table_grow()) return -1;
	}
}

void rmdag(int dag) {
	dag_t *d = dag_get(dag);
	if (!d) return;
	for (uvlong i = 0; i < d->ndot; i++) {
		free(d->dots [i].outs);
		free(d->dots [i].ins);
	}
	free(d->dots);
	free(d->arcs);
	*d = (dag_t) {0};
}

uvlong dgdot(int dag, uvlong ref) {
	dag_t *d = dag_get(dag);
	if (!d) {
		errmsg("dgdot: bad dag");
		return ( uvlong ) -1;
	}
	if (d->ndot == d->capdot && !grow_dots(d)) return ( uvlong ) -1;
	uvlong id    = d->ndot++;
	d->dots [id] = (dot) {.ref = ref};
	return id;
}

uvlong dgdotref(int dag, uvlong dot) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, dot)) {
		errmsg("dgdotref: bad dot");
		return 0;
	}
	return d->dots [dot].ref;
}

void rdgdotref(int dag, uvlong dot, uvlong ref) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, dot)) {
		errmsg("rdgdotref: bad dot");
		return;
	}
	d->dots [dot].ref = ref;
}

uvlong dgndot(int dag) {
	dag_t *d = dag_get(dag);
	return d ? d->ndot : 0;
}

uvlong dgarc(int dag, uvlong from, uvlong to, uvlong ref) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, from) || !dotok(d, to)) {
		errmsg("dgarc: bad dot");
		return ( uvlong ) -1;
	}
	uvlong old = findarc(d, from, to);
	if (old != ( uvlong ) -1) {
		d->arcs [old].ref = ref;
		return old;
	}
	if (reaches(d, to, from)) {
		if (err()) return ( uvlong ) -1;
		errmsg("dgarc: cycle");
		return ( uvlong ) -1;
	}
	if (!reserve_arc_slot(d, from, to)) return ( uvlong ) -1;
	uvlong id                                   = d->narc++;
	d->arcs [id]                                = (arc) {from, to, ref};
	d->dots [from].outs [d->dots [from].nout++] = id;
	d->dots [to].ins [d->dots [to].nin++]       = id;
	return id;
}

uvlong dgarcref(int dag, uvlong arc) {
	dag_t *d = dag_get(dag);
	if (!arcok(d, arc)) {
		errmsg("dgarcref: bad arc");
		return 0;
	}
	return d->arcs [arc].ref;
}

void rdgarcref(int dag, uvlong arc, uvlong ref) {
	dag_t *d = dag_get(dag);
	if (!arcok(d, arc)) {
		errmsg("rdgarcref: bad arc");
		return;
	}
	d->arcs [arc].ref = ref;
}

uvlong dgnarc(int dag) {
	dag_t *d = dag_get(dag);
	return d ? d->narc : 0;
}

uvlong dgfrom(int dag, uvlong arc) {
	dag_t *d = dag_get(dag);
	if (!arcok(d, arc)) {
		errmsg("dgfrom: bad arc");
		return 0;
	}
	return d->arcs [arc].from;
}

uvlong dgto(int dag, uvlong arc) {
	dag_t *d = dag_get(dag);
	if (!arcok(d, arc)) {
		errmsg("dgto: bad arc");
		return 0;
	}
	return d->arcs [arc].to;
}

bool dglinked(int dag, uvlong from, uvlong to) {
	dag_t *d = dag_get(dag);
	return findarc(d, from, to) != ( uvlong ) -1;
}

uvlong dgkids(int dag, uvlong dot, uvlong *buf, uvlong cap) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, dot) || badbuf(buf, cap)) {
		errmsg("dgkids: bad dot");
		return 0;
	}
	return copy_arc_dots(d, d->dots [dot].outs, d->dots [dot].nout, buf, cap, true);
}

uvlong dgpars(int dag, uvlong dot, uvlong *buf, uvlong cap) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, dot) || badbuf(buf, cap)) {
		errmsg("dgpars: bad dot");
		return 0;
	}
	return copy_arc_dots(d, d->dots [dot].ins, d->dots [dot].nin, buf, cap, false);
}

uvlong dgouts(int dag, uvlong dot, uvlong *buf, uvlong cap) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, dot) || badbuf(buf, cap)) {
		errmsg("dgouts: bad dot");
		return 0;
	}
	return copy_uvs(d->dots [dot].outs, d->dots [dot].nout, buf, cap);
}

uvlong dgins(int dag, uvlong dot, uvlong *buf, uvlong cap) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, dot) || badbuf(buf, cap)) {
		errmsg("dgins: bad dot");
		return 0;
	}
	return copy_uvs(d->dots [dot].ins, d->dots [dot].nin, buf, cap);
}

uvlong dgroots(int dag, uvlong *buf, uvlong cap) {
	dag_t *d = dag_get(dag);
	if (!d || badbuf(buf, cap)) {
		errmsg("dgroots: bad dag");
		return 0;
	}
	return copy_boundary(d, buf, cap, true);
}

uvlong dgleaves(int dag, uvlong *buf, uvlong cap) {
	dag_t *d = dag_get(dag);
	if (!d || badbuf(buf, cap)) {
		errmsg("dgleaves: bad dag");
		return 0;
	}
	return copy_boundary(d, buf, cap, false);
}

bool dgreaches(int dag, uvlong from, uvlong to) {
	dag_t *d = dag_get(dag);
	if (!dotok(d, from) || !dotok(d, to)) {
		errmsg("dgreaches: bad dot");
		return false;
	}
	return reaches(d, from, to);
}

uvlong dgtopo(int dag, uvlong *buf, uvlong cap) {
	dag_t *d = dag_get(dag);
	if (!d || badbuf(buf, cap)) {
		errmsg("dgtopo: bad dag");
		return 0;
	}
	uvlong n = d->ndot;
	if (n == 0) return 0;
	uvlong *indeg = null;
	uvlong *queue = null;
	if (!topo_ready(d, &indeg, &queue)) return 0;
	uvlong head = 0, tail = 0;
	for (uvlong i = 0; i < n; i++)
		if (indeg [i] == 0) queue [tail++] = i;
	uvlong out = 0;
	while (head < tail) {
		uvlong dot = queue [head++];
		if (buf && out < cap) buf [out] = dot;
		out++;
		for (uvlong i = 0; i < d->dots [dot].nout; i++) {
			uvlong kid = arckid(d, d->dots [dot].outs [i]);
			if (--indeg [kid] == 0) queue [tail++] = kid;
		}
	}
	free_topo(indeg, queue);
	if (out != n) {
		errmsg("dgtopo: cycle");
		return 0;
	}
	return n;
}
