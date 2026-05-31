#include "nexus.h"
#include "config.h"
#include "err.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if LONG_MAX != INT_MAX
  #define LT_VLONG_MIN LONG_MIN
  #define LT_VLONG_MAX LONG_MAX
#else
  #define LT_VLONG_MIN LLONG_MIN
  #define LT_VLONG_MAX LLONG_MAX
#endif

typedef struct lattice_t lattice_t;

struct lattice_t {
	vlong   xmin;
	vlong   xmax;
	vlong   ymin;
	vlong   ymax;
	uvlong  init;
	uvlong *phis;
};

static lattice_t *lts;
static int        ltcap;

static bool       lt_table_init(void) {
	if (lts) return true;
	ltcap = COETUA_LATTICE_TABLE_SEED > 0 ? COETUA_LATTICE_TABLE_SEED : 1;
	lts   = ( lattice_t * ) calloc(( size_t ) ltcap, sizeof(lattice_t));
	if (!lts) {
		errmsg("lattice: out of memory");
		ltcap = 0;
		return false;
	}
	return true;
}

static bool lt_table_grow(void) {
	int  need   = ltcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_LATTICE_TABLE_SEED) newcap = COETUA_LATTICE_TABLE_SEED;
	lattice_t *p = ( lattice_t * ) realloc(lts, ( size_t ) newcap * sizeof(lattice_t));
	if (!p) {
		errmsg("lattice: out of memory");
		return false;
	}
	memset(p + ltcap, 0, ( size_t ) (newcap - ltcap) * sizeof(lattice_t));
	lts   = p;
	ltcap = newcap;
	return true;
}

static lattice_t *lt_get(int lattice) {
	if (!lt_table_init() || lattice < 0 || lattice >= ltcap || !lts [lattice].phis) return null;
	return &lts [lattice];
}

static bool badbuf(void *buf, uvlong cap) { return !buf && cap; }

static bool spanok(vlong min, vlong max, uvlong *span) {
	if (max < min) return false;
	uvlong n = ( uvlong ) max - ( uvlong ) min + 1;
	if (!n) return false;
	*span = n;
	return true;
}

static uvlong span(vlong min, vlong max) { return ( uvlong ) max - ( uvlong ) min + 1; }

static uvlong ltwidth(lattice_t *lt) { return span(lt->xmin, lt->xmax); }

static uvlong ltheight(lattice_t *lt) { return span(lt->ymin, lt->ymax); }

static uvlong ltncell(lattice_t *lt) { return ltwidth(lt) * ltheight(lt); }

static uvlong ltside(lattice_t *lt) {
	uvlong w = ltwidth(lt), h = ltheight(lt);
	return nextpow2_64(w > h ? w : h);
}

static bool inbounds(lattice_t *lt, vlong x, vlong y) {
	return lt && x >= lt->xmin && x <= lt->xmax && y >= lt->ymin && y <= lt->ymax;
}

static bool normcoord(lattice_t *lt, vlong x, vlong y, uvlong *nx, uvlong *ny) {
	if (!inbounds(lt, x, y)) return false;
	*nx = ( uvlong ) x - ( uvlong ) lt->xmin;
	*ny = ( uvlong ) y - ( uvlong ) lt->ymin;
	return true;
}

static bool area(uvlong x, uvlong y, uvlong side, uvlong width, uvlong height, uvlong *out) {
	if (x >= width || y >= height) {
		*out = 0;
		return true;
	}
	uvlong xe = x + side;
	uvlong ye = y + side;
	if (xe > width) xe = width;
	if (ye > height) ye = height;
	return mulok64(xe - x, ye - y, out);
}

static uvlong coordoff(lattice_t *lt, uvlong nx, uvlong ny) {
	uvlong rank = 0;
	uvlong w = ltwidth(lt), h = ltheight(lt);
	uvlong sx = 0, sy = 0, side = ltside(lt);
	while (side > 1) {
		uvlong half   = side >> 1;
		uvlong qx [4] = {sx, sx + half, sx, sx + half};
		uvlong qy [4] = {sy, sy, sy + half, sy + half};
		for (uint q = 0; q < 4; q++) {
			bool hit = nx >= qx [q] && nx < qx [q] + half && ny >= qy [q] && ny < qy [q] + half;
			if (hit) {
				sx   = qx [q];
				sy   = qy [q];
				side = half;
				break;
			}
			uvlong n;
			area(qx [q], qy [q], half, w, h, &n);
			rank += n;
		}
	}
	return rank;
}

static void offcoord(lattice_t *lt, uvlong off, uvlong *nx, uvlong *ny) {
	uvlong w = ltwidth(lt), h = ltheight(lt);
	uvlong sx = 0, sy = 0, side = ltside(lt);
	while (side > 1) {
		uvlong half   = side >> 1;
		uvlong qx [4] = {sx, sx + half, sx, sx + half};
		uvlong qy [4] = {sy, sy, sy + half, sy + half};
		for (uint q = 0; q < 4; q++) {
			uvlong n;
			area(qx [q], qy [q], half, w, h, &n);
			if (off < n) {
				sx   = qx [q];
				sy   = qy [q];
				side = half;
				break;
			}
			off -= n;
		}
	}
	*nx = sx;
	*ny = sy;
}

static bool coordoff_checked(lattice_t *lt, vlong x, vlong y, uvlong *off) {
	uvlong nx, ny;
	if (!normcoord(lt, x, y, &nx, &ny)) return false;
	*off = coordoff(lt, nx, ny);
	return true;
}

static bool adddelta(vlong x, int d, vlong *out) {
	if (d > 0) {
		if (x == LT_VLONG_MAX) return false;
		*out = x + 1;
		return true;
	}
	if (d < 0) {
		if (x == LT_VLONG_MIN) return false;
		*out = x - 1;
		return true;
	}
	*out = x;
	return true;
}

static uvlong nearphis(lattice_t *lt, vlong x, vlong y, uvlong *buf, uvlong cap, bool surround) {
	static int odx [4] = {1, 0, -1, 0};
	static int ody [4] = {0, 1, 0, -1};
	static int sdx [8] = {1, 1, 0, -1, -1, -1, 0, 1};
	static int sdy [8] = {0, 1, 1, 1, 0, -1, -1, -1};
	int       *dx      = surround ? sdx : odx;
	int       *dy      = surround ? sdy : ody;
	uvlong     nd      = surround ? 8 : 4;
	uvlong     n       = 0;
	for (uvlong i = 0; i < nd; i++) {
		vlong nx, ny;
		if (!adddelta(x, dx [i], &nx) || !adddelta(y, dy [i], &ny)) continue;
		if (!inbounds(lt, nx, ny)) continue;
		uvlong off;
		coordoff_checked(lt, nx, ny, &off);
		if (buf && n < cap) buf [n] = lt->phis [off];
		n++;
	}
	return n;
}

int mklattice(int arena, vlong xmin, vlong xmax, vlong ymin, vlong ymax, uvlong init) {
	( void ) arena;
	if (!lt_table_init()) return -1;
	uvlong width, height, ncell;
	if (!spanok(xmin, xmax, &width) || !spanok(ymin, ymax, &height)) {
		errmsg("mklattice: bad bounds");
		return -1;
	}
	if (!mulok64(width, height, &ncell) || ncell > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
		errmsg("mklattice: capacity overflow");
		return -1;
	}
	uvlong maxdim = width > height ? width : height;
	uvlong side   = nextpow2_64(maxdim);
	if (side == ( uvlong ) -1) {
		errmsg("mklattice: capacity overflow");
		return -1;
	}
	uvlong *phis = ( uvlong * ) malloc(( size_t ) ncell * sizeof(uvlong));
	if (!phis) {
		errmsg("lattice: out of memory");
		return -1;
	}
	for (uvlong i = 0; i < ncell; i++) phis [i] = init;
	for (;;) {
		for (int i = 0; i < ltcap; i++) {
			if (!lts [i].phis) {
				lts [i]
				  = (lattice_t) {.xmin = xmin, .xmax = xmax, .ymin = ymin, .ymax = ymax, .init = init, .phis = phis};
				return i;
			}
		}
		if (!lt_table_grow()) {
			free(phis);
			return -1;
		}
	}
}

void rmlattice(int lattice) {
	lattice_t *lt = lt_get(lattice);
	if (!lt) return;
	free(lt->phis);
	*lt = (lattice_t) {0};
}

void ltbounds(int lattice, vlong *xmin, vlong *xmax, vlong *ymin, vlong *ymax) {
	lattice_t *lt = lt_get(lattice);
	if (!lt) {
		errmsg("ltbounds: bad lattice");
		return;
	}
	if (xmin) *xmin = lt->xmin;
	if (xmax) *xmax = lt->xmax;
	if (ymin) *ymin = lt->ymin;
	if (ymax) *ymax = lt->ymax;
}

uvlong ltbase(int lattice) {
	lattice_t *lt = lt_get(lattice);
	if (!lt) {
		errmsg("ltbase: bad lattice");
		return 0;
	}
	return lt->init;
}

uvlong ltphi(int lattice, vlong x, vlong y) {
	lattice_t *lt = lt_get(lattice);
	uvlong     off;
	if (!coordoff_checked(lt, x, y, &off)) {
		errmsg("ltphi: bad coordinate");
		return 0;
	}
	return lt->phis [off];
}

void rltphi(int lattice, vlong x, vlong y, uvlong phi) {
	lattice_t *lt = lt_get(lattice);
	uvlong     off;
	if (!coordoff_checked(lt, x, y, &off)) {
		errmsg("rltphi: bad coordinate");
		return;
	}
	lt->phis [off] = phi;
}

void ltclear(int lattice) {
	lattice_t *lt = lt_get(lattice);
	if (!lt) {
		errmsg("ltclear: bad lattice");
		return;
	}
	for (uvlong i = 0, n = ltncell(lt); i < n; i++) lt->phis [i] = lt->init;
}

uvlong ltorth(int lattice, vlong x, vlong y, uvlong *buf, uvlong cap) {
	lattice_t *lt = lt_get(lattice);
	if (!lt || !inbounds(lt, x, y) || badbuf(buf, cap)) {
		errmsg("ltorth: bad coordinate");
		return 0;
	}
	return nearphis(lt, x, y, buf, cap, false);
}

uvlong ltsurr(int lattice, vlong x, vlong y, uvlong *buf, uvlong cap) {
	lattice_t *lt = lt_get(lattice);
	if (!lt || !inbounds(lt, x, y) || badbuf(buf, cap)) {
		errmsg("ltsurr: bad coordinate");
		return 0;
	}
	return nearphis(lt, x, y, buf, cap, true);
}

uvlong ltphis(int lattice, ltcell *buf, uvlong cap) {
	lattice_t *lt = lt_get(lattice);
	if (!lt || badbuf(buf, cap)) {
		errmsg("ltphis: bad lattice");
		return 0;
	}
	uvlong n = 0;
	for (uvlong off = 0, nc = ltncell(lt); off < nc; off++) {
		uvlong phi = lt->phis [off];
		if (phi == lt->init) continue;
		if (buf && n < cap) {
			uvlong nx, ny;
			offcoord(lt, off, &nx, &ny);
			buf [n] = (ltcell) {.x = lt->xmin + ( vlong ) nx, .y = lt->ymin + ( vlong ) ny, .phi = phi};
		}
		n++;
	}
	return n;
}
