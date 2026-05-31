#include "xpedt.h"
#include "arena.h"
#include "config.h"
#include "err.h"
#include "nexus.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum xop
{
	xop_map,
	xop_filt,
	xop_mold,
	xop_take,
	xop_drop,
	xop_recur,
	xop_orama,
	xop_any,
	xop_all,
	xop_find,
	xop_sort,
	xop_aggroup,
	xop_uniq,
	xop_immix,
	xop_catena,
	xop_merge,
	xop_connex,
	xop_pare,
	xop_peruse,
	xop_diffuse,
	xop_flatten,
	xop_flatmap,
} xop;

typedef enum jobkind
{
	xjob_pass,
	xjob_call,
	xjob_calleach,
} jobkind;

typedef struct xnode xnode;
typedef struct xbuf  xbuf;
typedef struct xbind xbind;
typedef struct xjob  xjob;
typedef struct xeval xeval;
typedef struct xyctx xyctx;
typedef struct xpedt xp_t;

struct xnode {
	xop    op;
	xrslt  type;
	int    dep;
	int    depb;
	int    stype;
	uvlong n;
	uvlong stride;
	void  *init;

	union
	{
		void (*map)(pair, void *);
		bool (*pred)(arrst, void *);
		void (*zip)(arrst, arrst, arrst, void *);
		void (*red)(arrst, arrst, arrst, void *);
		void (*flatmap)(arrst, void (*)(arrst), void *);
		int  (*cmp)(void *, void *, void *);
		int  (*equiv)(arrst, void *);
		void (*resolv)(arrst, arrst, arrst, arrst, void *);
	} fn;

	void *args;
};

struct xbuf {
	xrslt  type;
	uvlong n;
	uvlong cap;
	uchar *data;
	bool   done;
};

struct xbind {
	int   sd;
	xprod p;
	xbuf  mat;
};

struct xjob {
	jobkind kind;
	int     target;
	xprod  *out;
	xprod   cell;
	int     nbind;
	xprod  *binds;
	void    (*fn)(xprod *, void *);
	void   *args;
};

struct xeval {
	xbind *binds;
	xbuf  *mats;
	int    nbind;
};

struct xyctx {
	xbuf *dst;
	bool  ok;
};

struct xpedt {
	int    arena;
	int    dag;
	xnode *nodes;
	int    nnode;
	int    capnode;
	int    leaf;
	xjob  *jobs;
	int    njob;
	int    capjob;
	bool   live;
};

static xp_t *xps;
static int   xpcap;

static bool  xp_table_init(void) {
	if (xps) return true;
	xpcap = COETUA_XPEDT_TABLE_SEED > 0 ? COETUA_XPEDT_TABLE_SEED : 1;
	xps   = ( xp_t * ) calloc(( size_t ) xpcap, sizeof(xp_t));
	if (!xps) {
		errmsg("xpedt: out of memory");
		xpcap = 0;
		return false;
	}
	return true;
}

static bool xp_table_grow(void) {
	int  need   = xpcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_XPEDT_TABLE_SEED) newcap = COETUA_XPEDT_TABLE_SEED;
	xp_t *p = ( xp_t * ) realloc(xps, ( size_t ) newcap * sizeof(xp_t));
	if (!p) {
		errmsg("xpedt: out of memory");
		return false;
	}
	memset(p + xpcap, 0, ( size_t ) (newcap - xpcap) * sizeof(xp_t));
	xps   = p;
	xpcap = newcap;
	return true;
}

static xp_t *xp_get(int xp) {
	if (!xp_table_init() || xp < 0 || xp >= xpcap || !xps [xp].live) return null;
	return &xps [xp];
}

static bool   external(int sd) { return sd > 0; }

static int    nodeidx(int sd) { return -sd - 1; }

static int    nodesd(int idx) { return -(idx + 1); }

static bool   nodeok(xp_t *xp, int sd) { return xp && sd < 0 && nodeidx(sd) >= 0 && nodeidx(sd) < xp->nnode; }

static uvlong item_size(xrslt t) {
	switch (t) {
	case xrtuvlong : return sizeof(uvlong);
	case xrtrune   : return sizeof(rune);
	case xrtarrst  : return sizeof(arrst);
	case xrtpair   : return sizeof(pair);
	default        : return 0;
	}
}

static bool xbuf_reserve(xbuf *b, uvlong need) {
	if (b->cap >= need) return true;
	uvlong cap = nextpow2_64(need);
	if (cap < 8) cap = 8;
	uvlong sz = item_size(b->type);
	if (!sz || cap > ( uvlong ) (SIZE_MAX / sz)) {
		errmsg("xpedt: capacity overflow");
		return false;
	}
	uchar *p = ( uchar * ) realloc(b->data, ( size_t ) (cap * sz));
	if (!p) {
		errmsg("xpedt: out of memory");
		return false;
	}
	b->data = p;
	b->cap  = cap;
	return true;
}

static bool xbuf_append(xbuf *b, void *item) {
	if (!xbuf_reserve(b, b->n + 1)) return false;
	uvlong sz = item_size(b->type);
	memcpy(b->data + b->n * sz, item, ( size_t ) sz);
	b->n++;
	return true;
}

static arrst xbuf_item(xbuf *b, uvlong i) {
	uvlong sz = item_size(b->type);
	return mkarrst(sz, b->data + i * sz);
}

static void xbuf_free(xbuf *b) {
	free(b->data);
	*b = (xbuf) {0};
}

static bool grow_nodes(xp_t *xp) {
	if (xp->nnode < xp->capnode) return true;
	uint ucap = nextpow2(( uint ) (xp->nnode + 1));
	int  cap  = ( int ) ucap;
	if (cap < 8) cap = 8;
	xnode *p = ( xnode * ) realloc(xp->nodes, ( size_t ) cap * sizeof(xnode));
	if (!p) {
		errmsg("xpedt: out of memory");
		return false;
	}
	xp->nodes   = p;
	xp->capnode = cap;
	return true;
}

static bool grow_jobs(xp_t *xp) {
	if (xp->njob < xp->capjob) return true;
	uint ucap = nextpow2(( uint ) (xp->njob + 1));
	int  cap  = ( int ) ucap;
	if (cap < 8) cap = 8;
	xjob *p = ( xjob * ) realloc(xp->jobs, ( size_t ) cap * sizeof(xjob));
	if (!p) {
		errmsg("xpedt: out of memory");
		return false;
	}
	xp->jobs   = p;
	xp->capjob = cap;
	return true;
}

static bool depok(xp_t *xp, int dep) {
	if (dep == 0) {
		errmsg("xpedt: bad source");
		return false;
	}
	if (external(dep)) return true;
	if (!nodeok(xp, dep)) {
		errmsg("xpedt: bad source");
		return false;
	}
	return true;
}

static xrslt srctype(xp_t *xp, int src) { return external(src) ? xrtarrst : xp->nodes [nodeidx(src)].type; }

static int   add_node(xp_t *xp, xnode n) {
	if (!depok(xp, n.dep) || !grow_nodes(xp)) return 0;
	int idx         = xp->nnode++;
	xp->nodes [idx] = n;
	int sd          = nodesd(idx);
	dgdot(xp->dag, ( uvlong ) idx);
	if (n.dep < 0) dgarc(xp->dag, ( uvlong ) nodeidx(n.dep), ( uvlong ) idx, 0);
	if (n.depb < 0) dgarc(xp->dag, ( uvlong ) nodeidx(n.depb), ( uvlong ) idx, 0);
	if (err()) return 0;
	xp->leaf = sd;
	return sd;
}

static void *node_copy(xp_t *xp, xrslt type, void *src) {
	uvlong sz = item_size(type);
	void  *p  = aden(xp->arena, sz);
	if (!p) {
		errmsg("xpedt: out of memory");
		return null;
	}
	memcpy(p, src, ( size_t ) sz);
	return p;
}

int mkxpedt(int arena) {
	if (!xp_table_init()) return -1;
	for (;;) {
		for (int i = 0; i < xpcap; i++) {
			if (!xps [i].live) {
				int dag = mkdag(arena);
				if (dag < 0) return -1;
				xps [i] = (xp_t) {.arena = arena, .dag = dag, .live = true};
				return i;
			}
		}
		if (!xp_table_grow()) return -1;
	}
}

int fmap(int xpedt, int src, xrslt rs, void (*fn)(pair sd, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !fn || !item_size(rs)) {
		errmsg("fmap: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_map, .type = rs, .dep = src, .fn.map = fn, .args = args});
}

int filt(int xpedt, int src, bool (*pred)(arrst i, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !pred || !depok(xp, src)) {
		if (!err()) errmsg("filt: bad argument");
		return 0;
	}
	xrslt t = external(src) ? xrtarrst : xp->nodes [nodeidx(src)].type;
	return add_node(xp, (xnode) {.op = xop_filt, .type = t, .dep = src, .fn.pred = pred, .args = args});
}

int mold(int xpedt, int src, silotype t) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !depok(xp, src)) {
		if (!err()) errmsg("mold: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_mold, .type = xrtuvlong, .dep = src, .stype = t});
}

int xtake(int xpedt, int src, uvlong n) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !depok(xp, src)) {
		if (!err()) errmsg("xtake: bad argument");
		return 0;
	}
	xrslt t = external(src) ? xrtarrst : xp->nodes [nodeidx(src)].type;
	return add_node(xp, (xnode) {.op = xop_take, .type = t, .dep = src, .n = n});
}

int xdrop(int xpedt, int src, uvlong n) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !depok(xp, src)) {
		if (!err()) errmsg("xdrop: bad argument");
		return 0;
	}
	xrslt t = external(src) ? xrtarrst : xp->nodes [nodeidx(src)].type;
	return add_node(xp, (xnode) {.op = xop_drop, .type = t, .dep = src, .n = n});
}

int xrecur(int xpedt, int src) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !depok(xp, src)) {
		if (!err()) errmsg("xrecur: bad argument");
		return 0;
	}
	xrslt t = external(src) ? xrtarrst : xp->nodes [nodeidx(src)].type;
	return add_node(xp, (xnode) {.op = xop_recur, .type = t, .dep = src});
}

int xorama(int xpedt, int src, uvlong size, uvlong stride) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !size || !stride || !depok(xp, src)) {
		if (!err()) errmsg("xorama: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_orama, .type = xrtarrst, .dep = src, .n = size, .stride = stride});
}

int xany(int xpedt, int src, bool (*pred)(arrst i, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !pred || !depok(xp, src)) {
		if (!err()) errmsg("xany: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_any, .type = xrtuvlong, .dep = src, .fn.pred = pred, .args = args});
}

int xall(int xpedt, int src, bool (*pred)(arrst i, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !pred || !depok(xp, src)) {
		if (!err()) errmsg("xall: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_all, .type = xrtuvlong, .dep = src, .fn.pred = pred, .args = args});
}

int xfind(int xpedt, int src, bool (*pred)(arrst i, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !pred || !depok(xp, src)) {
		if (!err()) errmsg("xfind: bad argument");
		return 0;
	}
	xrslt t = external(src) ? xrtarrst : xp->nodes [nodeidx(src)].type;
	return add_node(xp, (xnode) {.op = xop_find, .type = t, .dep = src, .fn.pred = pred, .args = args});
}

int sort(int xpedt, int src, int (*cmp)(void *, void *, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !cmp || !depok(xp, src)) {
		if (!err()) errmsg("sort: bad argument");
		return 0;
	}
	xrslt t = external(src) ? xrtarrst : xp->nodes [nodeidx(src)].type;
	return add_node(xp, (xnode) {.op = xop_sort, .type = t, .dep = src, .fn.cmp = cmp, .args = args});
}

int aggroup(int xpedt, int src, int (*equiv)(arrst i, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !equiv || !depok(xp, src)) {
		if (!err()) errmsg("aggroup: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_aggroup, .type = xrtpair, .dep = src, .fn.equiv = equiv, .args = args});
}

int xuniq(int xpedt, int src, void (*resolv)(arrst k, arrst va, arrst vb, arrst rv, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !depok(xp, src)) {
		if (!err()) errmsg("xuniq: bad argument");
		return 0;
	}
	xrslt t = srctype(xp, src);
	if (t != xrtpair) {
		errmsg("xuniq: pair source required");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_uniq, .type = xrtpair, .dep = src, .fn.resolv = resolv, .args = args});
}

static int binary_same_type(xp_t *xp, int a, int b, xop op, char *name) {
	if (!depok(xp, a) || !depok(xp, b)) return 0;
	xrslt ta = srctype(xp, a);
	xrslt tb = srctype(xp, b);
	if (ta != tb) {
		errmsg(name);
		return 0;
	}
	return add_node(xp, (xnode) {.op = op, .type = ta, .dep = a, .depb = b});
}

int ximmix(int xpedt, int srca, int srcb) {
	xp_t *xp = xp_get(xpedt);
	if (!xp) {
		errmsg("ximmix: bad argument");
		return 0;
	}
	return binary_same_type(xp, srca, srcb, xop_immix, "ximmix: source type mismatch");
}

int catena(int xpedt, int srca, int srcb) {
	xp_t *xp = xp_get(xpedt);
	if (!xp) {
		errmsg("catena: bad argument");
		return 0;
	}
	return binary_same_type(xp, srca, srcb, xop_catena, "catena: source type mismatch");
}

int xmerge(int xpedt, int srca, int srcb, int (*cmp)(void *, void *, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !cmp || !depok(xp, srca) || !depok(xp, srcb)) {
		if (!err()) errmsg("xmerge: bad argument");
		return 0;
	}
	xrslt ta = srctype(xp, srca);
	xrslt tb = srctype(xp, srcb);
	if (ta != tb) {
		errmsg("xmerge: source type mismatch");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_merge, .type = ta, .dep = srca, .depb = srcb, .fn.cmp = cmp, .args = args});
}

int connex(int xpedt, int srca, int srcb, xrslt rs, void (*zippr)(arrst a, arrst b, arrst dst, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !zippr || !item_size(rs) || !depok(xp, srca) || !depok(xp, srcb)) {
		if (!err()) errmsg("connex: bad argument");
		return 0;
	}
	return add_node(xp,
	                (xnode) {.op = xop_connex, .type = rs, .dep = srca, .depb = srcb, .fn.zip = zippr, .args = args});
}

int pare(int xpedt, int src, xrslt rs, void *init, void (*red)(arrst acc, arrst i, arrst dst, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !init || !red || !item_size(rs) || !depok(xp, src)) {
		if (!err()) errmsg("pare: bad argument");
		return 0;
	}
	void *seed = node_copy(xp, rs, init);
	if (!seed) return 0;
	return add_node(xp, (xnode) {.op = xop_pare, .type = rs, .dep = src, .init = seed, .fn.red = red, .args = args});
}

int peruse(int xpedt, int src, xrslt rs, void *init, void (*red)(arrst acc, arrst i, arrst dst, void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !init || !red || !item_size(rs) || !depok(xp, src)) {
		if (!err()) errmsg("peruse: bad argument");
		return 0;
	}
	void *seed = node_copy(xp, rs, init);
	if (!seed) return 0;
	return add_node(xp, (xnode) {.op = xop_peruse, .type = rs, .dep = src, .init = seed, .fn.red = red, .args = args});
}

int diffuse(int xpedt, int src, xrslt rs) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !item_size(rs) || !depok(xp, src)) {
		if (!err()) errmsg("diffuse: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_diffuse, .type = rs, .dep = src});
}

int flatten(int xpedt, int src) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !depok(xp, src)) {
		if (!err()) errmsg("flatten: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_flatten, .type = xrtarrst, .dep = src});
}

int flatmap(int xpedt, int src, xrslt rs, void (*fn)(arrst i, void (*yield)(arrst x), void *), void *args) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !fn || !item_size(rs) || !depok(xp, src)) {
		if (!err()) errmsg("flatmap: bad argument");
		return 0;
	}
	return add_node(xp, (xnode) {.op = xop_flatmap, .type = rs, .dep = src, .fn.flatmap = fn, .args = args});
}

static bool collect_pos(xp_t *xp, int sd, int **out, int *n, int *cap) {
	if (external(sd)) {
		for (int i = 0; i < *n; i++)
			if ((*out) [i] == sd) return true;
		if (*n == *cap) {
			uint ucap = nextpow2(( uint ) (*n + 1));
			int  nc   = ( int ) ucap;
			if (nc < 8) nc = 8;
			int *p = ( int * ) realloc(*out, ( size_t ) nc * sizeof(int));
			if (!p) {
				errmsg("xpedt: out of memory");
				return false;
			}
			*out = p;
			*cap = nc;
		}
		(*out) [(*n)++] = sd;
		return true;
	}
	if (!nodeok(xp, sd)) {
		errmsg("xpedt: bad source");
		return false;
	}
	xnode *nd = &xp->nodes [nodeidx(sd)];
	if (!collect_pos(xp, nd->dep, out, n, cap)) return false;
	if (nd->depb && !collect_pos(xp, nd->depb, out, n, cap)) return false;
	return true;
}

static int cmp_int(void const *a, void const *b) {
	int x = *( int const * ) a;
	int y = *( int const * ) b;
	return (x > y) - (x < y);
}

static xbind *make_binds(xp_t *xp, int target, va_list inputs, int *nbindp) {
	int *sds = null;
	int  n = 0, cap = 0;
	*nbindp = 0;
	if (!collect_pos(xp, target, &sds, &n, &cap)) {
		free(sds);
		return null;
	}
	qsort(sds, ( size_t ) n, sizeof(int), cmp_int);
	if (!n) {
		free(sds);
		errmsg("xpedt: no source bindings");
		return null;
	}
	xbind *bs = ( xbind * ) calloc(( size_t ) n, sizeof(xbind));
	if (!bs) {
		free(sds);
		errmsg("xpedt: out of memory");
		return null;
	}
	for (int i = 0; i < n; i++) {
		bs [i].sd = sds [i];
		bs [i].p  = va_arg(inputs, xprod);
	}
	free(sds);
	*nbindp = n;
	return bs;
}

static xprod *make_input_products(xp_t *xp, int target, va_list inputs, int *nprod) {
	*nprod       = 0;
	int    nbind = 0;
	xbind *bs    = make_binds(xp, target, inputs, &nbind);
	if (!bs && err()) return null;
	xprod *ps = ( xprod * ) calloc(( size_t ) nbind, sizeof(xprod));
	if (!ps) {
		free(bs);
		errmsg("xpedt: out of memory");
		return null;
	}
	for (int i = 0; i < nbind; i++) ps [i] = bs [i].p;
	free(bs);
	*nprod = nbind;
	return ps;
}

static xbind *find_bind(xbind *bs, int n, int sd) {
	for (int i = 0; i < n; i++)
		if (bs [i].sd == sd) return &bs [i];
	return null;
}

static bool materialize_silo(int d, xbuf *out) {
	int t = silotype_of(d);
	if (!t) return false;
	int it = mkiter(d);
	if (it < 0) return false;
	switch (t) {
	case silo_stack :
	case silo_seq :
	case silo_queue : {
		out->type = xrtuvlong;
		uvlong u;
		while (next(it, &u))
			if (!xbuf_append(out, &u)) {
				rmiter(it);
				return false;
			}
		break;
	}
	case silo_set : {
		out->type = xrtarrst;
		uvlong row [2];
		while (next(it, row)) {
			arrst a = mkarrst(row [0], ( void * ) row [1]);
			if (!xbuf_append(out, &a)) {
				rmiter(it);
				return false;
			}
		}
		break;
	}
	case silo_map : {
		out->type = xrtpair;
		uvlong row [4];
		while (next(it, row)) {
			pair p = mkpair(mkarrst(row [0], ( void * ) row [1]), mkarrst(row [2], ( void * ) row [3]));
			if (!xbuf_append(out, &p)) {
				rmiter(it);
				return false;
			}
		}
		break;
	}
	case silo_multiset : {
		out->type = xrtarrst;
		uvlong row [3];
		while (next(it, row)) {
			arrst a = mkarrst(row [0], ( void * ) row [1]);
			for (uvlong i = 0; i < row [2]; i++)
				if (!xbuf_append(out, &a)) {
					rmiter(it);
					return false;
				}
		}
		break;
	}
	default : rmiter(it); return false;
	}
	rmiter(it);
	return true;
}

static bool materialize_product(xprod p, xbuf *out) { return materialize_silo(p.d, out); }

static bool eval_source(xp_t *xp, int sd, xbind *bs, int nbind, xbuf *mats);

static bool eval_dep(xp_t *xp, int sd, xbind *bs, int nbind, xbuf *mats, xbuf **out) {
	if (external(sd)) {
		xbind *b = find_bind(bs, nbind, sd);
		if (!b) {
			errmsg("xpedt: missing source binding");
			return false;
		}
		if (!b->mat.done) {
			if (!materialize_product(b->p, &b->mat)) return false;
			b->mat.done = true;
		}
		*out = &b->mat;
		return true;
	}
	if (!eval_source(xp, sd, bs, nbind, mats)) return false;
	*out = &mats [nodeidx(sd)];
	return true;
}

static bool eval_dep2(xp_t *xp, xnode *nd, xbind *bs, int nbind, xbuf *mats, xbuf **a, xbuf **b) {
	if (!eval_dep(xp, nd->dep, bs, nbind, mats, a)) return false;
	return eval_dep(xp, nd->depb, bs, nbind, mats, b);
}

static bool eval_map(xnode *nd, xbuf *in, xbuf *out) {
	out->type = nd->type;
	uvlong sz = item_size(out->type);
	for (uvlong i = 0; i < in->n; i++) {
		if (!xbuf_reserve(out, out->n + 1)) return false;
		arrst src = xbuf_item(in, i);
		arrst dst = mkarrst(sz, out->data + out->n * sz);
		memset(dst.x, 0, ( size_t ) sz);
		nd->fn.map(mkpair(src, dst), nd->args);
		out->n++;
	}
	return true;
}

static bool eval_filt(xnode *nd, xbuf *in, xbuf *out) {
	out->type = in->type;
	for (uvlong i = 0; i < in->n; i++) {
		arrst item = xbuf_item(in, i);
		if (nd->fn.pred(item, nd->args) && !xbuf_append(out, item.x)) return false;
	}
	return true;
}

static bool eval_take(xnode *nd, xbuf *in, xbuf *out) {
	out->type = in->type;
	uvlong n  = nd->n < in->n ? nd->n : in->n;
	for (uvlong i = 0; i < n; i++) {
		arrst item = xbuf_item(in, i);
		if (!xbuf_append(out, item.x)) return false;
	}
	return true;
}

static bool eval_take_recur(xnode *take, xbuf *in, xbuf *out) {
	out->type = in->type;
	if (in->n == 0) return true;
	for (uvlong i = 0; i < take->n; i++) {
		arrst item = xbuf_item(in, i % in->n);
		if (!xbuf_append(out, item.x)) return false;
	}
	return true;
}

static bool eval_drop(xnode *nd, xbuf *in, xbuf *out) {
	out->type    = in->type;
	uvlong start = nd->n < in->n ? nd->n : in->n;
	for (uvlong i = start; i < in->n; i++) {
		arrst item = xbuf_item(in, i);
		if (!xbuf_append(out, item.x)) return false;
	}
	return true;
}

static bool eval_recur(xnode *nd, xbuf *in, xbuf *out) {
	( void ) nd;
	if (in->n) errmsg("xrecur: unbounded recurrent source needs bounded consumer");
	out->type = in->type;
	return in->n == 0;
}

static bool eval_orama(xnode *nd, xbuf *in, xbuf *out) {
	out->type = xrtarrst;
	uvlong sz = item_size(in->type);
	if (in->n < nd->n) return true;
	uvlong last = in->n - nd->n;
	for (uvlong i = 0;;) {
		arrst w = mkarrst(nd->n * sz, in->data + i * sz);
		if (!xbuf_append(out, &w)) return false;
		if (i > last - nd->stride) break;
		i += nd->stride;
	}
	return true;
}

static bool eval_any(xnode *nd, xbuf *in, xbuf *out) {
	uvlong found = 0;
	out->type    = xrtuvlong;
	for (uvlong i = 0; i < in->n && !found; i++) found = nd->fn.pred(xbuf_item(in, i), nd->args) ? 1 : 0;
	return xbuf_append(out, &found);
}

static bool eval_all(xnode *nd, xbuf *in, xbuf *out) {
	uvlong ok = 1;
	out->type = xrtuvlong;
	for (uvlong i = 0; i < in->n && ok; i++) ok = nd->fn.pred(xbuf_item(in, i), nd->args) ? 1 : 0;
	return xbuf_append(out, &ok);
}

static bool eval_find(xnode *nd, xbuf *in, xbuf *out) {
	out->type = in->type;
	for (uvlong i = 0; i < in->n; i++) {
		arrst item = xbuf_item(in, i);
		if (nd->fn.pred(item, nd->args)) return xbuf_append(out, item.x);
	}
	return true;
}

static bool append_range(xbuf *dst, xbuf *src, uvlong start) {
	for (uvlong i = start; i < src->n; i++) {
		arrst item = xbuf_item(src, i);
		if (!xbuf_append(dst, item.x)) return false;
	}
	return true;
}

static bool copy_buf(xbuf *src, xbuf *dst) {
	dst->type = src->type;
	return append_range(dst, src, 0);
}

static int ntz_size(size_t x) {
	if (!x) return ( int ) (8 * sizeof(size_t));
	return ctz64(( uvlong ) x);
}

static int pntz(size_t p [2]) {
	int r = ntz_size(p [0] - 1);
	if (r != 0 || (r = ( int ) (8 * sizeof(size_t)) + ntz_size(p [1])) != ( int ) (8 * sizeof(size_t))) return r;
	return 0;
}

static void pshl(size_t p [2], int n) {
	int bits = ( int ) (8 * sizeof(size_t));
	if (n <= 0) return;
	if (n >= 2 * bits) {
		p [0] = 0;
		p [1] = 0;
		return;
	}
	if (n >= bits) {
		n     -= bits;
		p [1]  = p [0];
		p [0]  = 0;
	}
	if (n == 0) return;
	p [1] <<= n;
	p [1]  |= p [0] >> (bits - n);
	p [0] <<= n;
}

static void pshr(size_t p [2], int n) {
	int bits = ( int ) (8 * sizeof(size_t));
	if (n <= 0) return;
	if (n >= 2 * bits) {
		p [0] = 0;
		p [1] = 0;
		return;
	}
	if (n >= bits) {
		n     -= bits;
		p [0]  = p [1];
		p [1]  = 0;
	}
	if (n == 0) return;
	p [0] >>= n;
	p [0]  |= p [1] << (bits - n);
	p [1] >>= n;
}

static void cycle_items(size_t width, uchar *ar [], int n) {
	uchar  tmp [256];
	size_t l;
	if (n < 2) return;
	ar [n] = tmp;
	while (width) {
		l = sizeof(tmp) < width ? sizeof(tmp) : width;
		memcpy(ar [n], ar [0], l);
		for (int i = 0; i < n; i++) {
			memcpy(ar [i], ar [i + 1], l);
			ar [i] += l;
		}
		width -= l;
	}
}

static void smooth_sift(uchar *head, size_t width, int (*cmp)(void *, void *, void *), void *arg, int pshift,
                        size_t lp []) {
	uchar *rt;
	uchar *lf;
	uchar *ar [14 * sizeof(size_t) + 1];
	int    i = 1;
	ar [0]   = head;
	while (pshift > 1) {
		rt = head - width;
		lf = head - width - lp [pshift - 2];
		if (cmp(ar [0], lf, arg) >= 0 && cmp(ar [0], rt, arg) >= 0) break;
		if (cmp(lf, rt, arg) >= 0) {
			ar [i++] = lf;
			head     = lf;
			pshift--;
		}
		else {
			ar [i++]  = rt;
			head      = rt;
			pshift   -= 2;
		}
	}
	cycle_items(width, ar, i);
}

static void smooth_trinkle(uchar *head, size_t width, int (*cmp)(void *, void *, void *), void *arg, size_t pp [2],
                           int pshift, bool trusty, size_t lp []) {
	uchar *stepson;
	uchar *rt;
	uchar *lf;
	size_t p [2];
	uchar *ar [14 * sizeof(size_t) + 1];
	int    i = 1;
	p [0]    = pp [0];
	p [1]    = pp [1];
	ar [0]   = head;
	while (p [0] != 1 || p [1] != 0) {
		stepson = head - lp [pshift];
		if (cmp(stepson, ar [0], arg) <= 0) break;
		if (!trusty && pshift > 1) {
			rt = head - width;
			lf = head - width - lp [pshift - 2];
			if (cmp(rt, stepson, arg) >= 0 || cmp(lf, stepson, arg) >= 0) break;
		}
		ar [i++]  = stepson;
		head      = stepson;
		int trail = pntz(p);
		pshr(p, trail);
		pshift += trail;
		trusty  = false;
	}
	if (!trusty) {
		cycle_items(width, ar, i);
		smooth_sift(head, width, cmp, arg, pshift, lp);
	}
}

static bool smoothsort(void *base, size_t nel, size_t width, int (*cmp)(void *, void *, void *), void *arg) {
	if (!nel || !width) return true;
	if (nel > SIZE_MAX / width) {
		errmsg("sort: source too large");
		return false;
	}
	size_t size = width * nel;
	size_t lp [12 * sizeof(size_t)];
	uchar *head   = ( uchar * ) base;
	uchar *high   = head + size - width;
	size_t p [2]  = {1, 0};
	int    pshift = 1;
	lp [0] = lp [1] = width;
	size_t i;
	for (i = 2; i < arrlen(lp); i++) {
		if (lp [i - 2] > SIZE_MAX - lp [i - 1] || lp [i - 2] + lp [i - 1] > SIZE_MAX - width) {
			errmsg("sort: source too large");
			return false;
		}
		lp [i] = lp [i - 2] + lp [i - 1] + width;
		if (lp [i] >= size) break;
	}
	if (i == arrlen(lp)) {
		errmsg("sort: source too large");
		return false;
	}
	while (head < high) {
		if ((p [0] & 3) == 3) {
			smooth_sift(head, width, cmp, arg, pshift, lp);
			pshr(p, 2);
			pshift += 2;
		}
		else {
			if (lp [pshift - 1] >= ( size_t ) (high - head))
				smooth_trinkle(head, width, cmp, arg, p, pshift, false, lp);
			else smooth_sift(head, width, cmp, arg, pshift, lp);
			if (pshift == 1) {
				pshl(p, 1);
				pshift = 0;
			}
			else {
				pshl(p, pshift - 1);
				pshift = 1;
			}
		}
		p [0] |= 1;
		head  += width;
	}
	smooth_trinkle(head, width, cmp, arg, p, pshift, false, lp);
	while (pshift != 1 || p [0] != 1 || p [1] != 0) {
		if (pshift <= 1) {
			int trail = pntz(p);
			pshr(p, trail);
			pshift += trail;
		}
		else {
			pshl(p, 2);
			pshift -= 2;
			p [0]  ^= 7;
			pshr(p, 1);
			smooth_trinkle(head - lp [pshift] - width, width, cmp, arg, p, pshift + 1, true, lp);
			pshl(p, 1);
			p [0] |= 1;
			smooth_trinkle(head - width, width, cmp, arg, p, pshift, true, lp);
		}
		head -= width;
	}
	return true;
}

static bool eval_sort(xnode *nd, xbuf *in, xbuf *out) {
	if (!copy_buf(in, out)) return false;
	uvlong sz = item_size(out->type);
	if (out->n && out->n > ( uvlong ) (SIZE_MAX / sz)) {
		errmsg("sort: source too large");
		return false;
	}
	return smoothsort(out->data, ( size_t ) out->n, ( size_t ) sz, nd->fn.cmp, nd->args);
}

static bool same_arrst(arrst a, arrst b) {
	return a.len == b.len && (!a.len || memcmp(a.x, b.x, ( size_t ) a.len) == 0);
}

static bool eval_aggroup(xp_t *xp, xnode *nd, xbuf *in, xbuf *out) {
	out->type = xrtpair;
	for (uvlong i = 0; i < in->n; i++) {
		arrst   item = xbuf_item(in, i);
		uvlong  cls  = ( uvlong ) nd->fn.equiv(item, nd->args);
		uvlong *key  = ( uvlong * ) aden(xp->arena, sizeof(*key));
		if (!key) {
			errmsg("xpedt: out of memory");
			return false;
		}
		*key   = cls;
		pair p = mkpair(mkarrst(sizeof(*key), key), item);
		if (!xbuf_append(out, &p)) return false;
	}
	return true;
}

static bool eval_uniq(xnode *nd, xbuf *in, xbuf *out) {
	out->type = xrtpair;
	for (uvlong i = 0; i < in->n; i++) {
		pair p    = *( pair * ) xbuf_item(in, i).x;
		bool seen = false;
		for (uvlong j = 0; j < out->n; j++) {
			pair *q = ( pair * ) xbuf_item(out, j).x;
			if (!same_arrst(p.a, q->a)) continue;
			seen = true;
			if (nd->fn.resolv) {
				arrst rv = q->b;
				nd->fn.resolv(q->a, q->b, p.b, rv, nd->args);
			}
			break;
		}
		if (!seen && !xbuf_append(out, &p)) return false;
	}
	return true;
}

static bool eval_catena(xnode *nd, xbuf *a, xbuf *b, xbuf *out) {
	if (a->type != b->type) {
		errmsg("catena: source type mismatch");
		return false;
	}
	out->type = a->type;
	( void ) nd;
	return append_range(out, a, 0) && append_range(out, b, 0);
}

static bool eval_immix(xnode *nd, xbuf *a, xbuf *b, xbuf *out) {
	if (a->type != b->type) {
		errmsg("ximmix: source type mismatch");
		return false;
	}
	out->type = a->type;
	uvlong n  = a->n > b->n ? a->n : b->n;
	for (uvlong i = 0; i < n; i++) {
		if (i < a->n) {
			arrst item = xbuf_item(a, i);
			if (!xbuf_append(out, item.x)) return false;
		}
		if (i < b->n) {
			arrst item = xbuf_item(b, i);
			if (!xbuf_append(out, item.x)) return false;
		}
	}
	( void ) nd;
	return true;
}

static bool eval_merge(xnode *nd, xbuf *a, xbuf *b, xbuf *out) {
	if (a->type != b->type) {
		errmsg("xmerge: source type mismatch");
		return false;
	}
	out->type = a->type;
	uvlong i  = 0;
	uvlong j  = 0;
	while (i < a->n && j < b->n) {
		arrst av = xbuf_item(a, i);
		arrst bv = xbuf_item(b, j);
		if (nd->fn.cmp(av.x, bv.x, nd->args) <= 0) {
			if (!xbuf_append(out, av.x)) return false;
			i++;
		}
		else {
			if (!xbuf_append(out, bv.x)) return false;
			j++;
		}
	}
	return append_range(out, a, i) && append_range(out, b, j);
}

static bool eval_connex(xnode *nd, xbuf *a, xbuf *b, xbuf *out) {
	out->type = nd->type;
	uvlong n  = a->n < b->n ? a->n : b->n;
	uvlong sz = item_size(out->type);
	for (uvlong i = 0; i < n; i++) {
		if (!xbuf_reserve(out, out->n + 1)) return false;
		arrst av  = xbuf_item(a, i);
		arrst bv  = xbuf_item(b, i);
		arrst dst = mkarrst(sz, out->data + out->n * sz);
		memset(dst.x, 0, ( size_t ) sz);
		nd->fn.zip(av, bv, dst, nd->args);
		out->n++;
	}
	return true;
}

static bool eval_fold(xnode *nd, xbuf *in, xbuf *out, bool each) {
	out->type  = nd->type;
	uvlong sz  = item_size(out->type);
	uchar *acc = ( uchar * ) malloc(( size_t ) sz);
	uchar *dst = ( uchar * ) malloc(( size_t ) sz);
	if (!acc || !dst) {
		free(acc);
		free(dst);
		errmsg("xpedt: out of memory");
		return false;
	}
	memcpy(acc, nd->init, ( size_t ) sz);
	for (uvlong i = 0; i < in->n; i++) {
		arrst av = mkarrst(sz, acc);
		arrst iv = xbuf_item(in, i);
		arrst dv = mkarrst(sz, dst);
		memset(dst, 0, ( size_t ) sz);
		nd->fn.red(av, iv, dv, nd->args);
		memcpy(acc, dst, ( size_t ) sz);
		if (each && !xbuf_append(out, acc)) {
			free(acc);
			free(dst);
			return false;
		}
	}
	if (!each && !xbuf_append(out, acc)) {
		free(acc);
		free(dst);
		return false;
	}
	free(acc);
	free(dst);
	return true;
}

static bool eval_diffuse(xnode *nd, xbuf *in, xbuf *out) {
	out->type    = nd->type;
	uvlong dstsz = item_size(out->type);
	uvlong srcsz = item_size(in->type);
	if (in->n == 0) return true;
	if (dstsz != srcsz) {
		errmsg("diffuse: source/result type mismatch");
		return false;
	}
	arrst first = xbuf_item(in, 0);
	for (uvlong i = 0; i < in->n; i++)
		if (!xbuf_append(out, first.x)) return false;
	return true;
}

static bool append_buf(xbuf *dst, xbuf *src) {
	if (src->n == 0) return true;
	if (dst->n == 0 && !dst->data) dst->type = src->type;
	if (dst->type != src->type) {
		errmsg("flatten: source type mismatch");
		return false;
	}
	for (uvlong i = 0; i < src->n; i++) {
		arrst item = xbuf_item(src, i);
		if (!xbuf_append(dst, item.x)) return false;
	}
	return true;
}

static bool eval_flatten(xbuf *in, xbuf *out) {
	out->type = xrtarrst;
	for (uvlong i = 0; i < in->n; i++) {
		arrst item = xbuf_item(in, i);
		if (in->type != xrtuvlong) {
			errmsg("flatten: source elements must be silo descriptors");
			return false;
		}
		int  d     = ( int ) *( uvlong * ) item.x;
		xbuf child = {0};
		bool ok    = materialize_silo(d, &child) && append_buf(out, &child);
		xbuf_free(&child);
		if (!ok) return false;
	}
	return true;
}

static xyctx *yield_ctx;

static void   flatmap_yield(arrst x) {
	if (yield_ctx && !xbuf_append(yield_ctx->dst, x.x)) yield_ctx->ok = false;
}

static bool eval_flatmap(xnode *nd, xbuf *in, xbuf *out) {
	out->type   = nd->type;
	xyctx  ctx  = {.dst = out, .ok = true};
	xyctx *prev = yield_ctx;
	for (uvlong i = 0; i < in->n; i++) {
		yield_ctx = &ctx;
		nd->fn.flatmap(xbuf_item(in, i), flatmap_yield, nd->args);
		yield_ctx = prev;
		if (!ctx.ok || err()) return false;
	}
	yield_ctx = prev;
	return ctx.ok;
}

static bool mold_seq(xbuf *in, int arena, xprod *out) {
	int s = mkseq(arena);
	if (s < 0) return false;
	for (uvlong i = 0; i < in->n; i++) {
		arrst  item = xbuf_item(in, i);
		uvlong u    = 0;
		if (in->type == xrtuvlong) u = *( uvlong * ) item.x;
		else if (in->type == xrtrune) u = *( rune * ) item.x;
		else {
			errmsg("mold: cannot mold item to sequence");
			return false;
		}
		atch(s, u);
	}
	out->d = s;
	return true;
}

static bool mold_setlike(xbuf *in, int d, bool ms) {
	for (uvlong i = 0; i < in->n; i++) {
		arrst item = xbuf_item(in, i);
		arrst key  = item;
		if (in->type == xrtarrst) key = *( arrst * ) item.x;
		else if (in->type == xrtpair) {
			pair p = *( pair * ) item.x;
			key    = p.a;
		}
		if (ms) addms(d, key.x, key.len);
		else adds(d, key.x, key.len);
		if (err()) return false;
	}
	return true;
}

static bool mold_map(xbuf *in, int map) {
	if (in->type != xrtpair) {
		errmsg("mold: map requires pair source");
		return false;
	}
	for (uvlong i = 0; i < in->n; i++) {
		pair p = *( pair * ) xbuf_item(in, i).x;
		insert(map, p.a.x, p.a.len, p.b.x, p.b.len);
		if (err()) return false;
	}
	return true;
}

static bool eval_mold(xp_t *xp, xnode *nd, xbuf *in, xbuf *out) {
	xprod p = {0};
	switch (nd->stype) {
	case silo_seq :
		if (!mold_seq(in, xp->arena, &p)) return false;
		break;
	case silo_set :
		p.d = mkset(xp->arena);
		if (p.d < 0 || !mold_setlike(in, p.d, false)) return false;
		break;
	case silo_multiset :
		p.d = mkmultiset(xp->arena);
		if (p.d < 0 || !mold_setlike(in, p.d, true)) return false;
		break;
	case silo_map :
		p.d = mkmap(xp->arena);
		if (p.d < 0 || !mold_map(in, p.d)) return false;
		break;
	default : errmsg("mold: bad silo type"); return false;
	}
	out->type = xrtuvlong;
	uvlong d  = ( uvlong ) p.d;
	return xbuf_append(out, &d);
}

static bool eval_source(xp_t *xp, int sd, xbind *bs, int nbind, xbuf *mats) {
	if (external(sd)) return true;
	int idx = nodeidx(sd);
	if (mats [idx].done) return true;
	xnode *nd = &xp->nodes [idx];
	xbuf  *in;
	xbuf  *inb;
	bool   ok;
	if (nd->depb) {
		if (!eval_dep2(xp, nd, bs, nbind, mats, &in, &inb)) return false;
		switch (nd->op) {
		case xop_catena : ok = eval_catena(nd, in, inb, &mats [idx]); break;
		case xop_immix  : ok = eval_immix(nd, in, inb, &mats [idx]); break;
		case xop_merge  : ok = eval_merge(nd, in, inb, &mats [idx]); break;
		case xop_connex : ok = eval_connex(nd, in, inb, &mats [idx]); break;
		default         : errmsg("xpedt: bad binary op"); return false;
		}
		mats [idx].done = ok;
		return ok;
	}
	if (nd->op == xop_take && nodeok(xp, nd->dep) && xp->nodes [nodeidx(nd->dep)].op == xop_recur) {
		xnode *rec = &xp->nodes [nodeidx(nd->dep)];
		if (!eval_dep(xp, rec->dep, bs, nbind, mats, &in)) return false;
		ok              = eval_take_recur(nd, in, &mats [idx]);
		mats [idx].done = ok;
		return ok;
	}
	if (!eval_dep(xp, nd->dep, bs, nbind, mats, &in)) return false;
	switch (nd->op) {
	case xop_map     : ok = eval_map(nd, in, &mats [idx]); break;
	case xop_filt    : ok = eval_filt(nd, in, &mats [idx]); break;
	case xop_mold    : ok = eval_mold(xp, nd, in, &mats [idx]); break;
	case xop_take    : ok = eval_take(nd, in, &mats [idx]); break;
	case xop_drop    : ok = eval_drop(nd, in, &mats [idx]); break;
	case xop_recur   : ok = eval_recur(nd, in, &mats [idx]); break;
	case xop_orama   : ok = eval_orama(nd, in, &mats [idx]); break;
	case xop_any     : ok = eval_any(nd, in, &mats [idx]); break;
	case xop_all     : ok = eval_all(nd, in, &mats [idx]); break;
	case xop_find    : ok = eval_find(nd, in, &mats [idx]); break;
	case xop_sort    : ok = eval_sort(nd, in, &mats [idx]); break;
	case xop_aggroup : ok = eval_aggroup(xp, nd, in, &mats [idx]); break;
	case xop_uniq    : ok = eval_uniq(nd, in, &mats [idx]); break;
	case xop_pare    : ok = eval_fold(nd, in, &mats [idx], false); break;
	case xop_peruse  : ok = eval_fold(nd, in, &mats [idx], true); break;
	case xop_diffuse : ok = eval_diffuse(nd, in, &mats [idx]); break;
	case xop_flatten : ok = eval_flatten(in, &mats [idx]); break;
	case xop_flatmap : ok = eval_flatmap(nd, in, &mats [idx]); break;
	default          : errmsg("xpedt: bad op"); return false;
	}
	mats [idx].done = ok;
	return ok;
}

static bool product_from_item(xrslt type, arrst item, xprod *out) {
	memset(out, 0, sizeof(*out));
	switch (type) {
	case xrtuvlong : out->u = *( uvlong * ) item.x; return true;
	case xrtrune   : out->r = *( rune * ) item.x; return true;
	case xrtarrst  : out->a = *( arrst * ) item.x; return true;
	case xrtpair   : out->p = *( pair * ) item.x; return true;
	default        : errmsg("xpedt: bad result type"); return false;
	}
}

static bool product_from_mat(xbuf *m, xprod *out) {
	memset(out, 0, sizeof(*out));
	if (m->n == 0) return true;
	return product_from_item(m->type, xbuf_item(m, 0), out);
}

static void eval_close(xeval *ev, int nmat) {
	for (int i = 0; i < ev->nbind; i++) xbuf_free(&ev->binds [i].mat);
	for (int i = 0; i < nmat; i++) xbuf_free(&ev->mats [i]);
	free(ev->binds);
	free(ev->mats);
	*ev = (xeval) {0};
}

static bool eval_open(xp_t *xp, int target, xprod *inputs, int ninput, xeval *ev) {
	*ev = (xeval) {0};
	if (!nodeok(xp, target)) {
		errmsg("xpedt: no batch job");
		return false;
	}
	ev->binds = ( xbind * ) calloc(( size_t ) ninput, sizeof(xbind));
	ev->mats  = ( xbuf * ) calloc(( size_t ) xp->nnode, sizeof(xbuf));
	ev->nbind = ninput;
	if ((!ev->binds && ninput) || (!ev->mats && xp->nnode)) {
		eval_close(ev, xp->nnode);
		errmsg("xpedt: out of memory");
		return false;
	}
	int *sds = null;
	int  ns = 0, cap = 0;
	if (!collect_pos(xp, target, &sds, &ns, &cap)) {
		free(sds);
		eval_close(ev, xp->nnode);
		return false;
	}
	qsort(sds, ( size_t ) ns, sizeof(int), cmp_int);
	if (ns != ninput) {
		free(sds);
		eval_close(ev, xp->nnode);
		errmsg("xpedt: wrong binding count");
		return false;
	}
	for (int i = 0; i < ninput; i++) {
		ev->binds [i].sd = sds [i];
		ev->binds [i].p  = inputs [i];
	}
	free(sds);
	return true;
}

static bool run_target(xp_t *xp, int target, xprod *out, xprod *binds, int nbind) {
	xeval ev;
	bool  ok;
	if (!eval_open(xp, target, binds, nbind, &ev)) return false;
	ok = eval_source(xp, target, ev.binds, ev.nbind, ev.mats) && product_from_mat(&ev.mats [nodeidx(target)], out);
	eval_close(&ev, xp->nnode);
	return ok;
}

static bool run_each(xp_t *xp, xjob *j) {
	xeval ev;
	if (!eval_open(xp, j->target, j->binds, j->nbind, &ev)) return false;
	if (!eval_source(xp, j->target, ev.binds, ev.nbind, ev.mats)) {
		eval_close(&ev, xp->nnode);
		return false;
	}
	xbuf *m = &ev.mats [nodeidx(j->target)];
	for (uvlong i = 0; i < m->n; i++) {
		xprod elem;
		if (!product_from_item(m->type, xbuf_item(m, i), &elem)) {
			eval_close(&ev, xp->nnode);
			return false;
		}
		j->fn(&elem, j->args);
	}
	eval_close(&ev, xp->nnode);
	return true;
}

void vxact(int xpedt, xprod *out, va_list inputs) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !out || !nodeok(xp, xp->leaf)) {
		errmsg("xact: bad argument");
		return;
	}
	int    nbind = 0;
	xprod *ps    = make_input_products(xp, xp->leaf, inputs, &nbind);
	if (!ps && err()) return;
	run_target(xp, xp->leaf, out, ps, nbind);
	free(ps);
}

void xact(int xpedt, xprod *out, ...) {
	va_list inputs;
	va_start(inputs, out);
	vxact(xpedt, out, inputs);
	va_end(inputs);
}

static void add_job_va(int xpedt, xprod *out, jobkind kind, void (*fn)(xprod *, void *), void *args, va_list inputs) {
	xp_t *xp = xp_get(xpedt);
	if (!xp || !nodeok(xp, xp->leaf) || (kind == xjob_pass && !out) || (kind != xjob_pass && !fn) || !grow_jobs(xp)) {
		if (!err()) errmsg("xpedt: bad job");
		return;
	}
	int    nbind = 0;
	xprod *ps    = make_input_products(xp, xp->leaf, inputs, &nbind);
	if (!ps && err()) return;
	xjob *j = &xp->jobs [xp->njob++];
	*j = (xjob) {.kind = kind, .target = xp->leaf, .out = out, .nbind = nbind, .binds = ps, .fn = fn, .args = args};
	if (!j->out) j->out = &j->cell;
}

void vxpass(int xpedt, xprod *out, va_list inputs) { add_job_va(xpedt, out, xjob_pass, null, null, inputs); }

void xpass(int xpedt, xprod *out, ...) {
	va_list inputs;
	va_start(inputs, out);
	vxpass(xpedt, out, inputs);
	va_end(inputs);
}

void vxcall(int xpedt, void (*fn)(xprod *out, void *), void *args, va_list inputs) {
	add_job_va(xpedt, null, xjob_call, fn, args, inputs);
}

void xcall(int xpedt, void (*fn)(xprod *out, void *), void *args, ...) {
	va_list inputs;
	va_start(inputs, args);
	vxcall(xpedt, fn, args, inputs);
	va_end(inputs);
}

void vxcalleach(int xpedt, void (*fn)(xprod *out, void *), void *args, va_list inputs) {
	add_job_va(xpedt, null, xjob_calleach, fn, args, inputs);
}

void xcalleach(int xpedt, void (*fn)(xprod *out, void *), void *args, ...) {
	va_list inputs;
	va_start(inputs, args);
	vxcalleach(xpedt, fn, args, inputs);
	va_end(inputs);
}

void xrun(int xpedt) {
	xp_t *xp = xp_get(xpedt);
	if (!xp) {
		errmsg("xrun: bad xpedt");
		return;
	}
	for (int i = 0; i < xp->njob; i++) {
		xjob *j = &xp->jobs [i];
		if (j->kind == xjob_calleach) {
			if (j->fn && !run_each(xp, j)) return;
			continue;
		}
		xprod out;
		if (!run_target(xp, j->target, &out, j->binds, j->nbind)) return;
		if (j->out) *j->out = out;
		if (j->kind == xjob_call && j->fn) j->fn(&out, j->args);
	}
}

void rmxpedt(int xpedt) {
	xp_t *xp = xp_get(xpedt);
	if (!xp) return;
	for (int i = 0; i < xp->njob; i++) free(xp->jobs [i].binds);
	free(xp->jobs);
	free(xp->nodes);
	rmdag(xp->dag);
	*xp = (xp_t) {0};
}
