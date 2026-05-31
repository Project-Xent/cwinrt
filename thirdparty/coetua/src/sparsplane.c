#include "nexus.h"
#include "config.h"
#include "err.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SPNONE (( uvlong ) -1)

#if LONG_MAX != INT_MAX
  #define SP_VLONG_MIN LONG_MIN
  #define SP_VLONG_MAX LONG_MAX
#else
  #define SP_VLONG_MIN LLONG_MIN
  #define SP_VLONG_MAX LLONG_MAX
#endif

enum
{
	SPFAN       = 64,
	SPBUCKETCAP = 64,
	SPLEVELS    = 22,
};

typedef struct spbranch   spbranch;
typedef struct spbucket   spbucket;
typedef struct spchild    spchild;
typedef struct sppoint    sppoint;
typedef struct sparsplane sparsplane;

typedef struct spbounds {
	double xmin, xmax, ymin, ymax;
	bool   any;
} spbounds;

typedef struct spboxq {
	double xmin, xmax, ymin, ymax;
} spboxq;

typedef struct spcircq {
	double x, y, r2;
} spcircq;

struct spchild {
	bool branch;

	union
	{
		spbranch *br;
		spbucket *bk;
	};
};

struct spbranch {
	uvlong   map;
	spchild *kids;
	spbounds bounds;
};

struct spbucket {
	uvlong  *pts;
	uvlong   n;
	uvlong   cap;
	spbounds bounds;
};

struct sppoint {
	double    x;
	double    y;
	uvlong    phi;
	vlong     tx;
	vlong     ty;
	uvlong    ux;
	uvlong    uy;
	spbucket *bucket;
	uvlong    slot;
	bool      live;
};

struct sparsplane {
	double   block;
	spchild  root;
	sppoint *pts;
	uvlong   npt;
	uvlong   nlpt;
	uvlong   cappt;
	bool     live;
};

static sparsplane *sps;
static int         spcap;

static bool        table_init(void) {
	if (sps) return true;
	spcap = COETUA_SPARSPLANE_TABLE_SEED > 0 ? COETUA_SPARSPLANE_TABLE_SEED : 1;
	sps   = ( sparsplane * ) calloc(( size_t ) spcap, sizeof(sparsplane));
	if (!sps) {
		errmsg("sparsplane: out of memory");
		spcap = 0;
		return false;
	}
	return true;
}

static bool table_grow(void) {
	int  need   = spcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_SPARSPLANE_TABLE_SEED) newcap = COETUA_SPARSPLANE_TABLE_SEED;
	sparsplane *p = ( sparsplane * ) realloc(sps, ( size_t ) newcap * sizeof(sparsplane));
	if (!p) {
		errmsg("sparsplane: out of memory");
		return false;
	}
	memset(p + spcap, 0, ( size_t ) (newcap - spcap) * sizeof(sparsplane));
	sps   = p;
	spcap = newcap;
	return true;
}

static sparsplane *sp_lookup(int plane) {
	if (!sps || plane < 0 || plane >= spcap || !sps [plane].live) return null;
	return &sps [plane];
}

static sparsplane *sp_get(int plane) {
	if (!table_init()) return null;
	return sp_lookup(plane);
}

static bool   valid_block(double block) { return isfinite(block) && block > 0.0; }

static bool   valid_coord(double x) { return !isnan(x); }

static double canon(double x) { return x == 0.0 ? 0.0 : x; }

static bool   same_coord(double a, double b) { return canon(a) == canon(b); }

static bool   ptlive(sparsplane *sp, uvlong pt) { return sp && pt < sp->npt && sp->pts [pt].live; }

static bool   badsoa(uvlong *ids, double *xs, double *ys, uvlong *phis, uvlong cap) {
	return cap && !ids && !xs && !ys && !phis;
}

static bool ensure_pt_cap(sparsplane *sp) {
	if (sp->npt < sp->cappt) return true;
	uvlong newcap = nextpow2_64(sp->npt + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(sppoint))) {
		errmsg("sparsplane: point capacity overflow");
		return false;
	}
	sppoint *p = ( sppoint * ) realloc(sp->pts, ( size_t ) newcap * sizeof(sppoint));
	if (!p) {
		errmsg("sparsplane: out of memory");
		return false;
	}
	memset(p + sp->cappt, 0, ( size_t ) (newcap - sp->cappt) * sizeof(sppoint));
	sp->pts   = p;
	sp->cappt = newcap;
	return true;
}

static uint   rank64(uvlong map, uint bit) { return ( uint ) popcnt64(map & ((( uvlong ) 1 << bit) - 1)); }

static uvlong bias(vlong x) { return (( uvlong ) x) ^ ((( uvlong ) 1) << 63); }

static void   tile_of(double x, double block, vlong *tx, uvlong *ux) {
	if (isinf(x)) {
		*tx = x < 0 ? SP_VLONG_MIN : SP_VLONG_MAX;
		*ux = bias(*tx);
		return;
	}
	double q = floor(x / block);
	if (q <= ( double ) SP_VLONG_MIN) *tx = SP_VLONG_MIN;
	else if (q >= ( double ) SP_VLONG_MAX) *tx = SP_VLONG_MAX;
	else *tx = ( vlong ) q;
	*ux = bias(*tx);
}

static uvlong tile_key(double x, double block) {
	vlong  tx;
	uvlong ux;
	tile_of(x, block, &tx, &ux);
	return ux;
}

static void set_point_coords(sparsplane *sp, sppoint *p, double x, double y) {
	x    = canon(x);
	y    = canon(y);
	p->x = x;
	p->y = y;
	tile_of(x, sp->block, &p->tx, &p->ux);
	tile_of(y, sp->block, &p->ty, &p->uy);
}

static uint axisfrag(uvlong u, uint level) {
	if (level >= 21) return ( uint ) ((u & 1u) << 2);
	uint shift = 61 - level * 3;
	return ( uint ) ((u >> shift) & 7u);
}

static uint ptfrag(sparsplane *sp, uvlong pt, uint level) {
	sppoint *p = &sp->pts [pt];
	return axisfrag(p->ux, level) | (axisfrag(p->uy, level) << 3);
}

static uint      keyfrag(uvlong ux, uvlong uy, uint level) { return axisfrag(ux, level) | (axisfrag(uy, level) << 3); }

static spbucket *find_bucket(spchild c, uvlong ux, uvlong uy, uint level) {
	while (c.branch) {
		spbranch *br  = c.br;
		uint      f   = keyfrag(ux, uy, level++);
		uvlong    bit = ( uvlong ) 1 << f;
		if (!(br->map & bit)) return null;
		c = br->kids [rank64(br->map, f)];
	}
	return c.bk;
}

static spbranch *new_branch(void) {
	spbranch *b = ( spbranch * ) calloc(1, sizeof(spbranch));
	if (!b) errmsg("sparsplane: out of memory");
	return b;
}

static spbucket *new_bucket(void) {
	spbucket *b = ( spbucket * ) calloc(1, sizeof(spbucket));
	if (!b) errmsg("sparsplane: out of memory");
	return b;
}

static void free_child(spchild c) {
	if (c.branch) {
		spbranch *b = c.br;
		if (!b) return;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++) free_child(b->kids [i]);
		free(b->kids);
		free(b);
	}
	else if (c.bk) {
		free(c.bk->pts);
		free(c.bk);
	}
}

static bool ensure_bucket_cap(spbucket *b) {
	if (b->n < b->cap) return true;
	uvlong newcap = nextpow2_64(b->n + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
		errmsg("sparsplane: bucket capacity overflow");
		return false;
	}
	uvlong *p = ( uvlong * ) realloc(b->pts, ( size_t ) newcap * sizeof(uvlong));
	if (!p) {
		errmsg("sparsplane: out of memory");
		return false;
	}
	b->pts = p;
	b->cap = newcap;
	return true;
}

static void update_bucket(sparsplane *sp, spbucket *b) {
	for (uvlong i = 0; b && i < b->n; i++) {
		uvlong pt           = b->pts [i];
		sp->pts [pt].bucket = b;
		sp->pts [pt].slot   = i;
	}
}

static bool bucket_append(sparsplane *sp, spbucket *b, uvlong pt) {
	if (!ensure_bucket_cap(b)) return false;
	b->pts [b->n++] = pt;
	update_bucket(sp, b);
	return true;
}

static spbounds empty_bounds(void) { return (spbounds) {0}; }

static void     bounds_add_point(spbounds *b, sppoint *p) {
	if (!b->any) {
		*b = (spbounds) {.xmin = p->x, .xmax = p->x, .ymin = p->y, .ymax = p->y, .any = true};
		return;
	}
	if (p->x < b->xmin) b->xmin = p->x;
	if (p->x > b->xmax) b->xmax = p->x;
	if (p->y < b->ymin) b->ymin = p->y;
	if (p->y > b->ymax) b->ymax = p->y;
}

static void bounds_add_bounds(spbounds *b, spbounds c) {
	if (!c.any) return;
	if (!b->any) {
		*b = c;
		return;
	}
	if (c.xmin < b->xmin) b->xmin = c.xmin;
	if (c.xmax > b->xmax) b->xmax = c.xmax;
	if (c.ymin < b->ymin) b->ymin = c.ymin;
	if (c.ymax > b->ymax) b->ymax = c.ymax;
}

static spbounds refresh_bounds(sparsplane *sp, spchild c) {
	spbounds b = empty_bounds();
	if (c.branch) {
		spbranch *br = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(br->map); i++) bounds_add_bounds(&b, refresh_bounds(sp, br->kids [i]));
		br->bounds = b;
	}
	else if (c.bk) {
		for (uvlong i = 0; i < c.bk->n; i++) bounds_add_point(&b, &sp->pts [c.bk->pts [i]]);
		c.bk->bounds = b;
	}
	return b;
}

static spbounds child_bounds(spchild c) {
	if (c.branch) return c.br ? c.br->bounds : empty_bounds();
	return c.bk ? c.bk->bounds : empty_bounds();
}

static bool bucket_append_raw(spbucket *b, uvlong pt) {
	if (!ensure_bucket_cap(b)) return false;
	b->pts [b->n++] = pt;
	return true;
}

static bool branch_insert_child(spbranch *b, uint f, spchild c) {
	uvlong bit = ( uvlong ) 1 << f;
	uint   idx = rank64(b->map, f);
	uint   n   = ( uint ) popcnt64(b->map);
	if (b->map & bit) {
		b->kids [idx] = c;
		return true;
	}
	spchild *p = ( spchild * ) realloc(b->kids, ( size_t ) (n + 1) * sizeof(spchild));
	if (!p) {
		errmsg("sparsplane: out of memory");
		return false;
	}
	b->kids = p;
	for (uint i = n; i > idx; i--) b->kids [i] = b->kids [i - 1];
	b->kids [idx]  = c;
	b->map        |= bit;
	return true;
}

static bool same_tiles(sparsplane *sp, uvlong *pts, uvlong n) {
	if (n < 2) return true;
	vlong tx = sp->pts [pts [0]].tx;
	vlong ty = sp->pts [pts [0]].ty;
	for (uvlong i = 1; i < n; i++)
		if (sp->pts [pts [i]].tx != tx || sp->pts [pts [i]].ty != ty) return false;
	return true;
}

static bool same_tiles_plus(sparsplane *sp, uvlong *pts, uvlong n, uvlong pt) {
	return !n
	    || (same_tiles(sp, pts, n)
	        && sp->pts [pt].tx == sp->pts [pts [0]].tx
	        && sp->pts [pt].ty == sp->pts [pts [0]].ty);
}

static bool build_bucket(uvlong *pts, uvlong n, spchild *out) {
	spbucket *b = new_bucket();
	if (!b) return false;
	for (uvlong i = 0; i < n; i++) {
		if (!bucket_append_raw(b, pts [i])) {
			free_child((spchild) {.bk = b});
			return false;
		}
	}
	*out = (spchild) {.bk = b};
	return true;
}

static bool build_subtree(sparsplane *sp, uvlong *pts, uvlong n, uint level, spchild *out) {
	*out = (spchild) {0};
	if (level >= SPLEVELS || same_tiles(sp, pts, n)) return build_bucket(pts, n, out);
	spbranch *br = new_branch();
	if (!br) return false;
	uvlong counts [SPFAN] = {0};
	for (uvlong i = 0; i < n; i++) counts [ptfrag(sp, pts [i], level)]++;
	for (uint f = 0; f < SPFAN; f++) {
		if (!counts [f]) continue;
		uvlong *grp = ( uvlong * ) malloc(( size_t ) counts [f] * sizeof(uvlong));
		if (!grp) {
			errmsg("sparsplane: out of memory");
			free_child((spchild) {.branch = true, .br = br});
			return false;
		}
		uvlong at = 0;
		for (uvlong i = 0; i < n; i++)
			if (ptfrag(sp, pts [i], level) == f) grp [at++] = pts [i];
		spchild c  = {0};
		bool    ok = build_subtree(sp, grp, counts [f], level + 1, &c);
		free(grp);
		if (!ok || !branch_insert_child(br, f, c)) {
			free_child(c);
			free_child((spchild) {.branch = true, .br = br});
			return false;
		}
	}
	*out = (spchild) {.branch = true, .br = br};
	return true;
}

static void update_child_positions(sparsplane *sp, spchild c) {
	if (c.branch) {
		spbranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++) update_child_positions(sp, b->kids [i]);
	}
	else update_bucket(sp, c.bk);
}

static bool insert_into_child(sparsplane *sp, spchild *cp, uint level, uvlong pt);

static bool split_bucket_insert(sparsplane *sp, spchild *cp, uint level, uvlong pt) {
	spbucket *old = cp->bk;
	if (old->n < SPBUCKETCAP || level >= SPLEVELS || same_tiles_plus(sp, old->pts, old->n, pt))
		return bucket_append(sp, old, pt);
	uvlong  n  = old->n + 1;
	uvlong *xs = ( uvlong * ) malloc(( size_t ) n * sizeof(uvlong));
	if (!xs) {
		errmsg("sparsplane: out of memory");
		return false;
	}
	for (uvlong i = 0; i < old->n; i++) xs [i] = old->pts [i];
	xs [old->n]  = pt;
	spchild repl = {0};
	if (!build_subtree(sp, xs, n, level, &repl)) {
		free(xs);
		return false;
	}
	free(xs);
	*cp = repl;
	update_child_positions(sp, repl);
	free(old->pts);
	free(old);
	return true;
}

static bool insert_into_branch(sparsplane *sp, spbranch *br, uint level, uvlong pt) {
	uint   f   = ptfrag(sp, pt, level);
	uvlong bit = ( uvlong ) 1 << f;
	uint   idx = rank64(br->map, f);
	if (br->map & bit) return insert_into_child(sp, &br->kids [idx], level + 1, pt);
	spbucket *b = new_bucket();
	if (!b) return false;
	if (!bucket_append_raw(b, pt)) {
		free(b);
		return false;
	}
	if (!branch_insert_child(br, f, (spchild) {.bk = b})) {
		free_child((spchild) {.bk = b});
		return false;
	}
	update_bucket(sp, b);
	return true;
}

static bool insert_into_child(sparsplane *sp, spchild *cp, uint level, uvlong pt) {
	if (cp->branch) return insert_into_branch(sp, cp->br, level, pt);
	if (cp->bk) return split_bucket_insert(sp, cp, level, pt);
	spbucket *b = new_bucket();
	if (!b) return false;
	if (!bucket_append(sp, b, pt)) {
		free(b);
		return false;
	}
	*cp = (spchild) {.bk = b};
	return true;
}

static void remove_child_at(spbranch *b, uint f) {
	uvlong bit = ( uvlong ) 1 << f;
	if (!(b->map & bit)) return;
	uint idx = rank64(b->map, f);
	uint n   = ( uint ) popcnt64(b->map);
	for (uint i = idx; i + 1 < n; i++) b->kids [i] = b->kids [i + 1];
	b->map &= ~bit;
	if (n == 1) {
		free(b->kids);
		b->kids = null;
	}
	else {
		spchild *p = ( spchild * ) realloc(b->kids, ( size_t ) (n - 1) * sizeof(spchild));
		if (p) b->kids = p;
	}
}

static bool prune_child(spchild *cp, uvlong ux, uvlong uy, uint level) {
	if (!cp->branch) return cp->bk && cp->bk->n == 0;
	spbranch *br  = cp->br;
	uint      f   = keyfrag(ux, uy, level);
	uvlong    bit = ( uvlong ) 1 << f;
	if (br->map & bit) {
		uint idx = rank64(br->map, f);
		if (prune_child(&br->kids [idx], ux, uy, level + 1)) {
			free_child(br->kids [idx]);
			remove_child_at(br, f);
		}
	}
	return br->map == 0;
}

static void bucket_remove(sparsplane *sp, spbucket *b, uvlong slot) {
	for (uvlong i = slot; i + 1 < b->n; i++) b->pts [i] = b->pts [i + 1];
	if (b->n) b->n--;
	update_bucket(sp, b);
}

static void remove_from_shape(sparsplane *sp, uvlong pt) {
	sppoint  *p  = &sp->pts [pt];
	spbucket *b  = p->bucket;
	uvlong    ux = p->ux;
	uvlong    uy = p->uy;
	bucket_remove(sp, b, p->slot);
	p->bucket = null;
	p->slot   = 0;
	if (prune_child(&sp->root, ux, uy, 0)) {
		free_child(sp->root);
		sp->root = (spchild) {0};
	}
	else refresh_bounds(sp, sp->root);
}

static bool move_point_raw(sparsplane *sp, uvlong pt, double x, double y) {
	sppoint old = sp->pts [pt];
	remove_from_shape(sp, pt);
	set_point_coords(sp, &sp->pts [pt], x, y);
	if (!insert_into_child(sp, &sp->root, 0, pt)) {
		sp->pts [pt] = old;
		if (!insert_into_child(sp, &sp->root, 0, pt)) errmsg("sparsplane move: rollback failed");
		else refresh_bounds(sp, sp->root);
		return false;
	}
	refresh_bounds(sp, sp->root);
	return true;
}

static void soa_put(sparsplane *sp, uvlong pt, uvlong row, uvlong *ids, double *xs, double *ys, uvlong *phis) {
	sppoint *p = &sp->pts [pt];
	if (ids) ids [row] = pt;
	if (xs) xs [row] = p->x;
	if (ys) ys [row] = p->y;
	if (phis) phis [row] = p->phi;
}

static void enum_child(sparsplane *sp, spchild c, bool (*match)(sparsplane *, uvlong, void *), void *arg, uvlong *ids,
                       double *xs, double *ys, uvlong *phis, uvlong cap, uvlong *n) {
	if (c.branch) {
		spbranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++)
			enum_child(sp, b->kids [i], match, arg, ids, xs, ys, phis, cap, n);
		return;
	}
	if (!c.bk) return;
	for (uvlong i = 0; i < c.bk->n; i++) {
		uvlong pt = c.bk->pts [i];
		if (match && !match(sp, pt, arg)) continue;
		if (*n < cap) soa_put(sp, pt, *n, ids, xs, ys, phis);
		(*n)++;
	}
}

static bool box_match(sparsplane *sp, uvlong pt, void *arg);
static bool circ_match(sparsplane *sp, uvlong pt, void *arg);

static bool box_hits_bounds(spbounds b, spboxq *q) {
	return b.any && b.xmax >= q->xmin && b.xmin <= q->xmax && b.ymax >= q->ymin && b.ymin <= q->ymax;
}

static double axis_box_dist(double x, double lo, double hi) {
	if (x < lo) return lo - x;
	if (x > hi) return x - hi;
	return 0.0;
}

static double bounds_dist2(spbounds b, double x, double y) {
	double dx = axis_box_dist(x, b.xmin, b.xmax);
	double dy = axis_box_dist(y, b.ymin, b.ymax);
	return dx * dx + dy * dy;
}

static bool circ_hits_bounds(spbounds b, spcircq *q) {
	if (!b.any || !isfinite(b.xmin) || !isfinite(b.xmax) || !isfinite(b.ymin) || !isfinite(b.ymax)) return b.any;
	return bounds_dist2(b, q->x, q->y) <= q->r2;
}

static void enum_box_child(sparsplane *sp, spchild c, spboxq *q, uvlong *ids, double *xs, double *ys, uvlong *phis,
                           uvlong cap, uvlong *n) {
	if (!box_hits_bounds(child_bounds(c), q)) return;
	if (c.branch) {
		spbranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++)
			enum_box_child(sp, b->kids [i], q, ids, xs, ys, phis, cap, n);
		return;
	}
	enum_child(sp, c, box_match, q, ids, xs, ys, phis, cap, n);
}

static void enum_circ_child(sparsplane *sp, spchild c, spcircq *q, uvlong *ids, double *xs, double *ys, uvlong *phis,
                            uvlong cap, uvlong *n) {
	if (!circ_hits_bounds(child_bounds(c), q)) return;
	if (c.branch) {
		spbranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++)
			enum_circ_child(sp, b->kids [i], q, ids, xs, ys, phis, cap, n);
		return;
	}
	enum_child(sp, c, circ_match, q, ids, xs, ys, phis, cap, n);
}

typedef struct spnearq {
	double x, y;
	uvlong best [2];
	double dist [2];
	uvlong n;
} spnearq;

static void near_consider(sparsplane *sp, spnearq *q, uvlong pt) {
	sppoint *p = &sp->pts [pt];
	if (!isfinite(p->x) || !isfinite(p->y)) return;
	double dx = p->x - q->x;
	double dy = p->y - q->y;
	double d  = dx * dx + dy * dy;
	if (q->n == 0 || d < q->dist [0]) {
		q->best [1] = q->best [0];
		q->dist [1] = q->dist [0];
		q->best [0] = pt;
		q->dist [0] = d;
		if (q->n < 2) q->n++;
	}
	else if (q->n == 1 || d < q->dist [1]) {
		q->best [1] = pt;
		q->dist [1] = d;
		if (q->n < 2) q->n++;
	}
}

static void near_child(sparsplane *sp, spchild c, spnearq *q) {
	spbounds b = child_bounds(c);
	if (!b.any) return;
	if (q->n >= 2 && bounds_dist2(b, q->x, q->y) > q->dist [1]) return;
	if (c.branch) {
		typedef struct {
			uint   idx;
			double dist;
		} nearord;

		spbranch *br = c.br;
		uint      n  = ( uint ) popcnt64(br->map);
		nearord   ord [SPFAN];
		for (uint i = 0; i < n; i++)
			ord [i] = (nearord) {.idx = i, .dist = bounds_dist2(child_bounds(br->kids [i]), q->x, q->y)};
		for (uint i = 1; i < n; i++) {
			nearord v = ord [i];
			uint    j = i;
			while (j && v.dist < ord [j - 1].dist) {
				ord [j] = ord [j - 1];
				j--;
			}
			ord [j] = v;
		}
		for (uint i = 0; i < n; i++) near_child(sp, br->kids [ord [i].idx], q);
		return;
	}
	if (!c.bk) return;
	for (uvlong i = 0; i < c.bk->n; i++) near_consider(sp, q, c.bk->pts [i]);
}

int mksparsplane(int arena, double block) {
	( void ) arena;
	if (!valid_block(block)) {
		errmsg("mksparsplane: bad block");
		return -1;
	}
	if (!table_init()) return -1;
	for (;;) {
		for (int i = 0; i < spcap; i++) {
			if (sps [i].live) continue;
			sps [i] = (sparsplane) {.block = block, .live = true};
			return i;
		}
		if (!table_grow()) return -1;
	}
}

void rmsparsplane(int plane) {
	sparsplane *sp = sp_lookup(plane);
	if (!sp) return;
	free_child(sp->root);
	free(sp->pts);
	*sp = (sparsplane) {0};
}

uvlong spput(int plane, double x, double y, uvlong phi) {
	sparsplane *sp = sp_get(plane);
	if (!sp || !valid_coord(x) || !valid_coord(y)) {
		errmsg("spput: bad argument");
		return SPNONE;
	}
	if (!ensure_pt_cap(sp)) return SPNONE;
	uvlong pt    = sp->npt++;
	sp->pts [pt] = (sppoint) {.phi = phi, .live = true};
	set_point_coords(sp, &sp->pts [pt], x, y);
	if (!insert_into_child(sp, &sp->root, 0, pt)) {
		sp->pts [pt] = (sppoint) {0};
		sp->npt--;
		return SPNONE;
	}
	refresh_bounds(sp, sp->root);
	sp->nlpt++;
	return pt;
}

bool spdel(int plane, uvlong pt) {
	sparsplane *sp = sp_get(plane);
	if (!ptlive(sp, pt)) {
		errmsg("spdel: bad point");
		return false;
	}
	remove_from_shape(sp, pt);
	sp->pts [pt].live = false;
	sp->nlpt--;
	return true;
}

uvlong spphi(int plane, uvlong pt) {
	sparsplane *sp = sp_get(plane);
	if (!ptlive(sp, pt)) {
		errmsg("spphi: bad point");
		return 0;
	}
	return sp->pts [pt].phi;
}

void rspphi(int plane, uvlong pt, uvlong phi) {
	sparsplane *sp = sp_get(plane);
	if (!ptlive(sp, pt)) {
		errmsg("rspphi: bad point");
		return;
	}
	sp->pts [pt].phi = phi;
}

void sploctn(int plane, uvlong pt, double *x, double *y) {
	sparsplane *sp = sp_get(plane);
	if (!ptlive(sp, pt) || (!x && !y)) {
		errmsg("sploctn: bad argument");
		return;
	}
	if (x) *x = sp->pts [pt].x;
	if (y) *y = sp->pts [pt].y;
}

bool spmov(int plane, uvlong pt, double x, double y) {
	sparsplane *sp = sp_get(plane);
	if (!ptlive(sp, pt) || !valid_coord(x) || !valid_coord(y)) {
		errmsg("spmov: bad argument");
		return false;
	}
	return move_point_raw(sp, pt, x, y);
}

uvlong spat(int plane, double x, double y) {
	sparsplane *sp = sp_get(plane);
	if (!sp || !valid_coord(x) || !valid_coord(y)) {
		errmsg("spat: bad argument");
		return SPNONE;
	}
	uvlong found = SPNONE;
	x            = canon(x);
	y            = canon(y);
	uvlong    ux = tile_key(x, sp->block);
	uvlong    uy = tile_key(y, sp->block);
	spbucket *b  = find_bucket(sp->root, ux, uy, 0);
	for (uvlong i = 0; b && i < b->n; i++) {
		uvlong pt = b->pts [i];
		if (!same_coord(sp->pts [pt].x, x) || !same_coord(sp->pts [pt].y, y)) continue;
		if (found != SPNONE) {
			errmsg("spat: duplicate point");
			return SPNONE;
		}
		found = pt;
	}
	if (found == SPNONE) errmsg("spat: missing point");
	return found;
}

uvlong spnpt(int plane) {
	sparsplane *sp = sp_get(plane);
	if (!sp) {
		errmsg("spnpt: bad descriptor");
		return 0;
	}
	return sp->nlpt;
}

uvlong sppts(int plane, uvlong *ids, double *xs, double *ys, uvlong *phis, uvlong cap) {
	sparsplane *sp = sp_get(plane);
	if (!sp || badsoa(ids, xs, ys, phis, cap)) {
		errmsg("sppts: bad argument");
		return 0;
	}
	uvlong n = 0;
	enum_child(sp, sp->root, null, null, ids, xs, ys, phis, cap, &n);
	return n;
}

static bool box_match(sparsplane *sp, uvlong pt, void *arg) {
	spboxq  *q = ( spboxq * ) arg;
	sppoint *p = &sp->pts [pt];
	return p->x >= q->xmin && p->x <= q->xmax && p->y >= q->ymin && p->y <= q->ymax;
}

static bool circ_match(sparsplane *sp, uvlong pt, void *arg) {
	spcircq *q = ( spcircq * ) arg;
	sppoint *p = &sp->pts [pt];
	if (!isfinite(p->x) || !isfinite(p->y)) return false;
	double dx = p->x - q->x;
	double dy = p->y - q->y;
	return dx * dx + dy * dy <= q->r2;
}

static bool collect_push(uvlong pt, uvlong **out, uvlong *n, uvlong *cap) {
	if (*n >= *cap) {
		uvlong newcap = nextpow2_64(*n + 1);
		if (newcap < 16) newcap = 16;
		if (newcap > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
			errmsg("sparsplane: collection capacity overflow");
			return false;
		}
		uvlong *p = ( uvlong * ) realloc(*out, ( size_t ) newcap * sizeof(uvlong));
		if (!p) {
			errmsg("sparsplane: out of memory");
			return false;
		}
		*out = p;
		*cap = newcap;
	}
	(*out) [(*n)++] = pt;
	return true;
}

static bool collect_child(sparsplane *sp, spchild c, bool (*match)(sparsplane *, uvlong, void *), void *arg,
                          uvlong **out, uvlong *n, uvlong *cap) {
	if (c.branch) {
		spbranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++)
			if (!collect_child(sp, b->kids [i], match, arg, out, n, cap)) return false;
		return true;
	}
	if (!c.bk) return true;
	for (uvlong i = 0; i < c.bk->n; i++) {
		uvlong pt = c.bk->pts [i];
		if (match && !match(sp, pt, arg)) continue;
		if (!collect_push(pt, out, n, cap)) return false;
	}
	return true;
}

static bool collect_box_child(sparsplane *sp, spchild c, spboxq *q, uvlong **out, uvlong *n, uvlong *cap) {
	if (!box_hits_bounds(child_bounds(c), q)) return true;
	if (c.branch) {
		spbranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++)
			if (!collect_box_child(sp, b->kids [i], q, out, n, cap)) return false;
		return true;
	}
	return collect_child(sp, c, box_match, q, out, n, cap);
}

static bool collect_circ_child(sparsplane *sp, spchild c, spcircq *q, uvlong **out, uvlong *n, uvlong *cap) {
	if (!circ_hits_bounds(child_bounds(c), q)) return true;
	if (c.branch) {
		spbranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++)
			if (!collect_circ_child(sp, b->kids [i], q, out, n, cap)) return false;
		return true;
	}
	return collect_child(sp, c, circ_match, q, out, n, cap);
}

static bool move_many(sparsplane *sp, uvlong *pts, uvlong n, double dx, double dy) {
	if (!isfinite(dx) || !isfinite(dy)) {
		errmsg("sparsplane move: bad delta");
		return false;
	}
	for (uvlong i = 0; i < n; i++) {
		sppoint *p  = &sp->pts [pts [i]];
		double   nx = p->x + dx;
		double   ny = p->y + dy;
		if (isnan(nx) || isnan(ny)) {
			errmsg("sparsplane move: nan result");
			return false;
		}
	}
	for (uvlong i = 0; i < n; i++) {
		sppoint *p = &sp->pts [pts [i]];
		if (!move_point_raw(sp, pts [i], p->x + dx, p->y + dy)) return false;
	}
	return true;
}

uvlong spbox(int plane, double xmin, double xmax, double ymin, double ymax, uvlong *ids, double *xs, double *ys,
             uvlong *phis, uvlong cap) {
	sparsplane *sp = sp_get(plane);
	if (!sp
	    || isnan(xmin)
	    || isnan(xmax)
	    || isnan(ymin)
	    || isnan(ymax)
	    || xmin > xmax
	    || ymin > ymax
	    || badsoa(ids, xs, ys, phis, cap))
	{
		errmsg("spbox: bad argument");
		return 0;
	}
	spboxq q = {.xmin = canon(xmin), .xmax = canon(xmax), .ymin = canon(ymin), .ymax = canon(ymax)};
	uvlong n = 0;
	enum_box_child(sp, sp->root, &q, ids, xs, ys, phis, cap, &n);
	return n;
}

uvlong spcirc(int plane, double x, double y, double r, uvlong *ids, double *xs, double *ys, uvlong *phis, uvlong cap) {
	sparsplane *sp = sp_get(plane);
	if (!sp || !isfinite(x) || !isfinite(y) || !isfinite(r) || r < 0.0 || badsoa(ids, xs, ys, phis, cap)) {
		errmsg("spcirc: bad argument");
		return 0;
	}
	spcircq q = {.x = canon(x), .y = canon(y), .r2 = r * r};
	uvlong  n = 0;
	enum_circ_child(sp, sp->root, &q, ids, xs, ys, phis, cap, &n);
	return n;
}

uvlong spnear(int plane, double x, double y, uvlong *first, uvlong *second) {
	sparsplane *sp = sp_get(plane);
	if (!sp || !isfinite(x) || !isfinite(y) || !first) {
		errmsg("spnear: bad argument");
		return 0;
	}
	spnearq q = {
	  .x = canon(x), .y = canon(y), .best = {SPNONE, SPNONE},
              .dist = {0.0,    0.0   },
              .n = 0
    };
	near_child(sp, sp->root, &q);
	if (!q.n) {
		errmsg("spnear: no point");
		return 0;
	}
	*first = q.best [0];
	if (second) {
		if (q.n < 2) {
			errmsg("spnear: no second point");
			return 1;
		}
		*second = q.best [1];
	}
	return second ? 2 : 1;
}

uvlong spboxmove(int plane, double xmin, double xmax, double ymin, double ymax, double dx, double dy) {
	sparsplane *sp = sp_get(plane);
	if (!sp || isnan(xmin) || isnan(xmax) || isnan(ymin) || isnan(ymax) || xmin > xmax || ymin > ymax) {
		errmsg("spboxmove: bad argument");
		return 0;
	}
	spboxq  q   = {.xmin = canon(xmin), .xmax = canon(xmax), .ymin = canon(ymin), .ymax = canon(ymax)};
	uvlong *pts = null;
	uvlong  n = 0, cap = 0;
	if (!collect_box_child(sp, sp->root, &q, &pts, &n, &cap)) {
		free(pts);
		return 0;
	}
	bool ok = move_many(sp, pts, n, dx, dy);
	free(pts);
	return ok ? n : 0;
}

uvlong spcircmove(int plane, double x, double y, double r, double dx, double dy) {
	sparsplane *sp = sp_get(plane);
	if (!sp || !isfinite(x) || !isfinite(y) || !isfinite(r) || r < 0.0) {
		errmsg("spcircmove: bad argument");
		return 0;
	}
	spcircq q   = {.x = canon(x), .y = canon(y), .r2 = r * r};
	uvlong *pts = null;
	uvlong  n = 0, cap = 0;
	if (!collect_circ_child(sp, sp->root, &q, &pts, &n, &cap)) {
		free(pts);
		return 0;
	}
	bool ok = move_many(sp, pts, n, dx, dy);
	free(pts);
	return ok ? n : 0;
}
