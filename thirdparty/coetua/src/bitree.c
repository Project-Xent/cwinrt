#include "nexus.h"
#include "err.h"
#include "internal/bitree_internal.h"
#include <stddef.h>

static bool bad_side(int side, char *who) {
	if (side == 0 || side == 1) return false;
	errmsg(who);
	return true;
}

static knod *new_knod(int arena, uvlong ref) { return bt_new_knod(arena, ref, "bitree: out of memory"); }

static bool  ancestor(uvlong anc, uvlong kid) {
	for (uvlong k = kid; k; k = bt_par(k))
		if (k == anc) return true;
	return false;
}

static void attach_at(uvlong par, int side, uvlong kid) {
	bt_set_kid(par, side, kid);
	if (kid) bt_set_par(kid, par);
}

static void walk_pre(uvlong root, uvlong *buf, uvlong cap, uvlong *n) {
	if (!root) return;
	if (buf && *n < cap) buf [*n] = root;
	(*n)++;
	walk_pre(bt_kid(root, 0), buf, cap, n);
	walk_pre(bt_kid(root, 1), buf, cap, n);
}

static void walk_in(uvlong root, uvlong *buf, uvlong cap, uvlong *n) {
	if (!root) return;
	walk_in(bt_kid(root, 0), buf, cap, n);
	if (buf && *n < cap) buf [*n] = root;
	(*n)++;
	walk_in(bt_kid(root, 1), buf, cap, n);
}

static void walk_post(uvlong root, uvlong *buf, uvlong cap, uvlong *n) {
	if (!root) return;
	walk_post(bt_kid(root, 0), buf, cap, n);
	walk_post(bt_kid(root, 1), buf, cap, n);
	if (buf && *n < cap) buf [*n] = root;
	(*n)++;
}

static uvlong height(uvlong root) {
	if (!root) return 0;
	uvlong z = height(bt_kid(root, 0));
	uvlong o = height(bt_kid(root, 1));
	return 1 + (z > o ? z : o);
}

static void walk_level_at(uvlong root, uvlong level, uvlong *buf, uvlong cap, uvlong *n) {
	if (!root) return;
	if (level == 0) {
		if (buf && *n < cap) buf [*n] = root;
		(*n)++;
		return;
	}
	walk_level_at(bt_kid(root, 0), level - 1, buf, cap, n);
	walk_level_at(bt_kid(root, 1), level - 1, buf, cap, n);
}

static void walk_level(uvlong root, uvlong *buf, uvlong cap, uvlong *n) {
	uvlong h = height(root);
	for (uvlong i = 0; i < h; i++) walk_level_at(root, i, buf, cap, n);
}

static void clear_tree(uvlong root) {
	if (!root) return;
	knod  *k = bt_raw(root);
	uvlong z = bt_kid(root, 0);
	uvlong o = bt_kid(root, 1);
	clear_tree(z);
	clear_tree(o);
	k->par      = 0;
	k->ref      = 0;
	k->kids [0] = 0;
	k->kids [1] = 0;
}

uvlong mkbitree(int arena, uvlong ref) {
	knod *k = new_knod(arena, ref);
	return k ? bt_id(k) : 0;
}

void rmbitree(uvlong root) {
	if (!root) {
		errmsg("rmbitree: bad root");
		return;
	}
	uvlong p = bt_par(root);
	if (p) {
		if (bt_kid(p, 0) == root) bt_set_kid(p, 0, 0);
		else if (bt_kid(p, 1) == root) bt_set_kid(p, 1, 0);
		else {
			errmsg("rmbitree: bad shape");
			return;
		}
	}
	clear_tree(root);
}

uvlong btref(uvlong k) {
	if (!k) {
		errmsg("btref: bad knod");
		return 0;
	}
	return bt_raw(k)->ref;
}

void rbtref(uvlong k, uvlong ref) {
	if (!k) {
		errmsg("rbtref: bad knod");
		return;
	}
	bt_raw(k)->ref = ref;
}

uvlong btpar(uvlong k) {
	if (!k) {
		errmsg("btpar: bad knod");
		return 0;
	}
	return bt_par(k);
}

bool btside(uvlong k) {
	if (!k) {
		errmsg("btside: bad knod");
		return true;
	}
	uvlong p = bt_par(k);
	if (!p) {
		errmsg("btside: root");
		return false;
	}
	if (bt_kid(p, 0) == k) return false;
	if (bt_kid(p, 1) == k) return true;
	errmsg("btside: bad shape");
	return true;
}

void btkids(uvlong k, uvlong *zero, uvlong *one) {
	if (!zero && !one) {
		errmsg("btkids: bad output");
		return;
	}
	if (!k) {
		errmsg("btkids: bad knod");
		return;
	}
	if (zero) *zero = bt_kid(k, 0);
	if (one) *one = bt_kid(k, 1);
}

void rbtkids(int arena, uvlong k, uvlong *zero, uvlong *one) {
	if (!zero && !one) {
		errmsg("rbtkids: bad input");
		return;
	}
	if (!k) {
		errmsg("rbtkids: bad knod");
		return;
	}
	if ((zero && bt_kid(k, 0)) || (one && bt_kid(k, 1))) {
		errmsg("rbtkids: occupied slot");
		return;
	}

	knod *z = zero ? new_knod(arena, *zero) : null;
	if (zero && !z) return;
	knod *o = one ? new_knod(arena, *one) : null;
	if (one && !o) return;

	if (z) {
		bt_set_kid(k, 0, bt_id(z));
		bt_set_par(bt_id(z), k);
		*zero = bt_id(z);
	}
	if (o) {
		bt_set_kid(k, 1, bt_id(o));
		bt_set_par(bt_id(o), k);
		*one = bt_id(o);
	}
}

uvlong btdrop(uvlong par, int side) {
	if (!par) {
		errmsg("btdrop: bad par");
		return 0;
	}
	if (bad_side(side, "btdrop: bad side")) return 0;
	uvlong old = bt_kid(par, side);
	if (!old) return 0;
	bt_set_kid(par, side, 0);
	bt_set_par(old, 0);
	return old;
}

uvlong btplace(uvlong par, int side, uvlong kid) {
	if (!par) {
		errmsg("btplace: bad par");
		return 0;
	}
	if (bad_side(side, "btplace: bad side")) return 0;
	if (!kid) {
		errmsg("btplace: bad kid");
		return 0;
	}
	if (bt_par(kid)) {
		errmsg("btplace: parented kid");
		return 0;
	}
	if (par == kid || ancestor(kid, par)) {
		errmsg("btplace: cycle");
		return 0;
	}
	uvlong old = bt_kid(par, side);
	if (old) bt_set_par(old, 0);
	attach_at(par, side, kid);
	return old;
}

uvlong btrot(uvlong k, int side) {
	if (!k) {
		errmsg("btrot: bad knod");
		return 0;
	}
	if (bad_side(side, "btrot: bad side")) return 0;
	uvlong yid = bt_kid(k, side);
	if (!yid) {
		errmsg("btrot: missing kid");
		return 0;
	}

	int    other = side ^ 1;
	uvlong p     = bt_par(k);
	uvlong beta  = bt_kid(yid, other);
	int    pside = -1;
	if (p) {
		if (bt_kid(p, 0) == k) pside = 0;
		else if (bt_kid(p, 1) == k) pside = 1;
		else {
			errmsg("btrot: bad shape");
			return 0;
		}
	}
	bt_set_kid(k, side, beta);
	if (beta) bt_set_par(beta, k);
	bt_set_kid(yid, other, k);
	bt_set_par(yid, p);
	bt_set_par(k, yid);
	if (p) bt_set_kid(p, pside, yid);
	return yid;
}

uvlong btrrotte(uvlong k, int first, int second) {
	if (!k) {
		errmsg("btrrotte: bad knod");
		return 0;
	}
	if (bad_side(first, "btrrotte: bad side")) return 0;
	if (bad_side(second, "btrrotte: bad side")) return 0;
	uvlong child = bt_kid(k, second);
	if (!child) {
		errmsg("btrrotte: missing kid");
		return 0;
	}
	if (!btrot(child, first)) return 0;
	return btrot(k, second);
}

uvlong btwalk(uvlong root, int order, uvlong *buf, uvlong cap) {
	if (!root) {
		errmsg("btwalk: bad root");
		return 0;
	}
	if (!buf && cap) {
		errmsg("btwalk: bad buffer");
		return 0;
	}
	uvlong n = 0;
	switch (order) {
	case 0  : walk_pre(root, buf, cap, &n); break;
	case 1  : walk_in(root, buf, cap, &n); break;
	case 2  : walk_post(root, buf, cap, &n); break;
	case 3  : walk_level(root, buf, cap, &n); break;
	default : errmsg("btwalk: bad order"); return 0;
	}
	return n;
}
