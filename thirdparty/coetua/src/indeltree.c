#include "nexus.h"
#include "config.h"
#include "err.h"
#include "internal/bitree_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct indel_t indel_t;
typedef int            (*incmpfn)(uvlong, uvlong, void *);

typedef struct rawvec {
	uvlong *x;
	uvlong  n;
	uvlong  cap;
} rawvec;

struct indel_t {
	int    arena;
	uvlong root;
	uvlong nlive;
	uvlong salt;
	bool   live;
};

static indel_t *ins;
static int      incap;

static bool     in_table_init(void) {
	if (ins) return true;
	incap = COETUA_INDELTREE_TABLE_SEED > 0 ? COETUA_INDELTREE_TABLE_SEED : 1;
	ins   = ( indel_t * ) calloc(( size_t ) incap, sizeof(indel_t));
	if (!ins) {
		errmsg("indeltree: out of memory");
		incap = 0;
		return false;
	}
	return true;
}

static bool in_table_grow(void) {
	int  need   = incap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_INDELTREE_TABLE_SEED) newcap = COETUA_INDELTREE_TABLE_SEED;
	indel_t *p = ( indel_t * ) realloc(ins, ( size_t ) newcap * sizeof(indel_t));
	if (!p) {
		errmsg("indeltree: out of memory");
		return false;
	}
	memset(p + incap, 0, ( size_t ) (newcap - incap) * sizeof(indel_t));
	ins   = p;
	incap = newcap;
	return true;
}

static indel_t *in_get(int tree) {
	if (!in_table_init() || tree < 0 || tree >= incap || !ins [tree].live) return null;
	return &ins [tree];
}

static bool badbuf(uvlong *buf, uvlong cap) { return !buf && cap; }

static bool vec_push(rawvec *v, uvlong x) {
	if (v->n == v->cap) {
		uvlong newcap = nextpow2_64(v->n + 1);
		if (newcap < 16) newcap = 16;
		if (newcap > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
			errmsg("indeltree: node capacity overflow");
			return false;
		}
		uvlong *p = ( uvlong * ) realloc(v->x, ( size_t ) newcap * sizeof(uvlong));
		if (!p) {
			errmsg("indeltree: out of memory");
			return false;
		}
		v->x   = p;
		v->cap = newcap;
	}
	v->x [v->n++] = x;
	return true;
}

static uvlong salt_for(int tree) {
	uvlong s = (( uvlong ) ( uint ) (tree + 1) << 32) ^ ( uvlong ) 0x9e3779b97f4a7c15ull;
	return s & ~BT_TAG_MASK;
}

static uvlong enc(indel_t *t, uvlong raw) { return raw ? raw ^ t->salt : 0; }

static uvlong dec(indel_t *t, uvlong pub) { return pub ? pub ^ t->salt : 0; }

static uvlong minimum(uvlong r);
static uvlong maximum(uvlong r);
static uvlong successor(uvlong raw);

static bool   belongs(indel_t *t, uvlong raw) {
	if (!t || !raw || bt_deadp(raw)) return false;
	uvlong r = raw;
	while (bt_par(r)) r = bt_par(r);
	return r == t->root;
}

static uvlong live_raw(indel_t *t, uvlong pub, char *who) {
	uvlong raw = dec(t, pub);
	if (!belongs(t, raw)) {
		errmsg(who);
		return 0;
	}
	return raw;
}

static void set_child(indel_t *t, uvlong par, int side, uvlong kid) {
	if (!par) {
		t->root = kid;
		if (kid) bt_set_par(kid, 0);
		return;
	}
	bt_set_kid(par, side, kid);
	if (kid) bt_set_par(kid, par);
}

static int child_side(uvlong par, uvlong kid) {
	if (!par || !kid) return -1;
	if (bt_kid(par, 0) == kid) return 0;
	if (bt_kid(par, 1) == kid) return 1;
	return -1;
}

static uvlong rb_rotate(indel_t *t, uvlong h, int side) {
	uvlong x = btrot(h, side);
	if (x && !bt_par(x)) t->root = x;
	return x;
}

static uvlong rb_single(indel_t *t, uvlong h, int side) {
	bool   hred = bt_redp(h);
	uvlong x    = rb_rotate(t, h, side);
	bt_mark_red(x, hred);
	bt_mark_red(h, true);
	return x;
}

static uvlong rb_double(indel_t *t, uvlong h, int side) {
	uvlong c = bt_kid(h, side);
	rb_single(t, c, side ^ 1);
	return rb_single(t, h, side);
}

static void split_4node(uvlong h) {
	bt_mark_red(h, true);
	bt_mark_red(bt_kid(h, 0), false);
	bt_mark_red(bt_kid(h, 1), false);
}

static uvlong repair_red(indel_t *t, uvlong grand, uvlong par, uvlong kid) {
	int pside = child_side(grand, par);
	int kside = child_side(par, kid);
	if (pside < 0 || kside < 0) return kid;
	uvlong top = pside == kside ? rb_single(t, grand, pside) : rb_double(t, grand, pside);
	bt_mark_red(top, false);
	bt_mark_red(bt_kid(top, 0), true);
	bt_mark_red(bt_kid(top, 1), true);
	return top;
}

static void tree_insert(indel_t *t, uvlong node, incmpfn cmp, void *arg) {
	bt_set_par(node, 0);
	bt_set_kid(node, 0, 0);
	bt_set_kid(node, 1, 0);
	bt_mark_red(node, true);

	if (!t->root) {
		t->root = node;
		bt_mark_red(node, false);
		return;
	}

	uvlong grand = 0;
	uvlong par   = 0;
	uvlong cur   = t->root;

	while (cur) {
		if (bt_redp(bt_kid(cur, 0)) && bt_redp(bt_kid(cur, 1))) {
			split_4node(cur);
			if (par && bt_redp(par) && bt_redp(cur)) {
				cur = repair_red(t, grand, par, cur);
				par = bt_par(cur);
			}
		}

		int    side = cmp(bt_raw(node)->ref, bt_raw(cur)->ref, arg) < 0 ? 0 : 1;
		uvlong next = bt_kid(cur, side);
		if (!next) {
			set_child(t, cur, side, node);
			if (bt_redp(cur)) repair_red(t, par, cur, node);
			break;
		}
		grand = par;
		par   = cur;
		cur   = next;
	}
	bt_mark_red(t->root, false);
}

static int path_side(uvlong anc, uvlong target) {
	if (anc == target) return -1;
	uvlong k = target;
	uvlong p = bt_par(k);
	while (p && p != anc) {
		k = p;
		p = bt_par(k);
	}
	if (p != anc) return -1;
	return child_side(anc, k);
}

static int choose_delete_side(uvlong q, uvlong target, bool found, int replacement_side) {
	if (!found) return path_side(q, target);
	if (q == target) {
		if (bt_kid(q, 1)) return 1;
		if (bt_kid(q, 0)) return 0;
		return 0;
	}
	return replacement_side ? 0 : 1;
}

static uvlong remove_single_child(uvlong q) {
	uvlong z = bt_kid(q, 0);
	return z ? z : bt_kid(q, 1);
}

static void replace_at_parent(uvlong par, uvlong old, uvlong new) {
	if (!par) return;
	if (bt_kid(par, 0) == old) bt_set_kid(par, 0, new);
	else bt_set_kid(par, 1, new);
	if (new) bt_set_par(new, par);
}

static void detach_deleted(uvlong raw) {
	bt_set_par(raw, 0);
	bt_set_kid(raw, 0, 0);
	bt_set_kid(raw, 1, 0);
	bt_mark_red(raw, false);
	bt_mark_dead(raw);
}

static void move_replacement_into_deleted_slot(uvlong head, uvlong deleted, uvlong repl, uvlong repl_child) {
	uvlong dpar = bt_par(deleted);
	uvlong dz   = bt_kid(deleted, 0);
	uvlong do_  = bt_kid(deleted, 1);
	bool   dred = bt_redp(deleted);

	if (dz == repl) dz = repl_child;
	if (do_ == repl) do_ = repl_child;

	replace_at_parent(dpar, deleted, repl);
	bt_set_kid(repl, 0, dz);
	bt_set_kid(repl, 1, do_);
	if (dz) bt_set_par(dz, repl);
	if (do_) bt_set_par(do_, repl);
	bt_mark_red(repl, dred);

	if (!dpar) bt_set_kid(head, 1, repl);
}

static void delete_raw(indel_t *t, uvlong raw) {
	if (!t->root || !raw) return;

	knod   headv = {0};
	uvlong head  = bt_id(&headv);
	bt_set_kid(head, 1, t->root);
	bt_set_par(t->root, head);

	uvlong q                = head;
	uvlong p                = 0;
	uvlong g                = 0;
	uvlong f                = 0;
	int    dir              = 1;
	int    replacement_side = -1;

	while (bt_kid(q, dir)) {
		int last = dir;
		g        = p;
		p        = q;
		q        = bt_kid(q, dir);

		if (q == raw) {
			f                = q;
			replacement_side = bt_kid(q, 1) ? 1 : 0;
		}

		dir = choose_delete_side(q, raw, f != 0, replacement_side);
		if (dir < 0) break;

		if (!bt_redp(q) && !bt_redp(bt_kid(q, dir))) {
			uvlong other = bt_kid(q, dir ^ 1);
			if (bt_redp(other)) {
				uvlong top = btrot(q, dir ^ 1);
				bt_mark_red(top, false);
				bt_mark_red(q, true);
				if (p == head) bt_set_kid(head, last, top);
				p = top;
			}
			else {
				uvlong s = bt_kid(p, last ^ 1);
				if (s) {
					if (!bt_redp(bt_kid(s, last ^ 1)) && !bt_redp(bt_kid(s, last))) {
						bt_mark_red(p, false);
						bt_mark_red(s, true);
						bt_mark_red(q, true);
					}
					else {
						int gside = g ? child_side(g, p) : -1;
						if (bt_redp(bt_kid(s, last))) {
							uvlong top = rb_double(t, p, last ^ 1);
							if (g) bt_set_kid(g, gside, top);
						}
						else if (bt_redp(bt_kid(s, last ^ 1))) {
							uvlong top = rb_single(t, p, last ^ 1);
							if (g) bt_set_kid(g, gside, top);
						}
						uvlong top = g ? bt_kid(g, gside) : bt_kid(head, 1);
						bt_mark_red(q, true);
						bt_mark_red(top, true);
						bt_mark_red(bt_kid(top, 0), false);
						bt_mark_red(bt_kid(top, 1), false);
					}
				}
			}
		}
	}

	if (!f) {
		t->root = bt_kid(head, 1);
		if (t->root) {
			bt_set_par(t->root, 0);
			bt_mark_red(t->root, false);
		}
		return;
	}

	uvlong repl       = q;
	uvlong repl_par   = bt_par(repl);
	uvlong repl_child = remove_single_child(repl);
	replace_at_parent(repl_par, repl, repl_child);

	if (f != repl) move_replacement_into_deleted_slot(head, f, repl, repl_child);
	detach_deleted(f);

	t->root = bt_kid(head, 1);
	if (t->root) {
		bt_set_par(t->root, 0);
		bt_mark_red(t->root, false);
	}
}

static uvlong minimum(uvlong r) {
	while (r && bt_kid(r, 0)) r = bt_kid(r, 0);
	return r;
}

static uvlong maximum(uvlong r) {
	while (r && bt_kid(r, 1)) r = bt_kid(r, 1);
	return r;
}

static uvlong successor(uvlong raw) {
	if (bt_kid(raw, 1)) return minimum(bt_kid(raw, 1));
	uvlong p = bt_par(raw);
	while (p && bt_kid(p, 1) == raw) {
		raw = p;
		p   = bt_par(p);
	}
	return p;
}

static uvlong predecessor(uvlong raw) {
	if (bt_kid(raw, 0)) return maximum(bt_kid(raw, 0));
	uvlong p = bt_par(raw);
	while (p && bt_kid(p, 0) == raw) {
		raw = p;
		p   = bt_par(p);
	}
	return p;
}

static uvlong lower_raw(indel_t *t, uvlong chet, incmpfn cmp, void *arg, bool upper) {
	uvlong cur  = t->root;
	uvlong best = 0;
	while (cur) {
		int c = cmp(bt_raw(cur)->ref, chet, arg);
		if (upper ? c > 0 : c >= 0) {
			best = cur;
			cur  = bt_kid(cur, 0);
		}
		else cur = bt_kid(cur, 1);
	}
	return best;
}

static uvlong inorder_copy(indel_t *t, uvlong r, uvlong *buf, uvlong cap, uvlong n) {
	if (!r) return n;
	n = inorder_copy(t, bt_kid(r, 0), buf, cap, n);
	if (buf && n < cap) buf [n] = enc(t, r);
	n++;
	return inorder_copy(t, bt_kid(r, 1), buf, cap, n);
}

int mkindeltree(int arena) {
	if (!in_table_init()) return -1;
	for (;;) {
		for (int i = 0; i < incap; i++) {
			if (ins [i].live) continue;
			ins [i] = (indel_t) {.arena = arena, .salt = salt_for(i), .live = true};
			return i;
		}
		if (!in_table_grow()) return -1;
	}
}

void rmindeltree(int tree) {
	indel_t *t = in_get(tree);
	if (!t) return;
	*t = (indel_t) {0};
}

uvlong inplace(int tree, uvlong ref, int (*cmp)(uvlong ref, uvlong chet, void *arg), void *arg) {
	indel_t *t = in_get(tree);
	if (!t || !cmp) {
		errmsg("inplace: bad argument");
		return 0;
	}
	knod *k = bt_new_knod(t->arena, ref, "indeltree: out of memory");
	if (!k) return 0;
	uvlong raw = bt_id(k);
	tree_insert(t, raw, cmp, arg);
	t->nlive++;
	return enc(t, raw);
}

bool indrop(int tree, uvlong pub) {
	indel_t *t = in_get(tree);
	if (!t) {
		errmsg("indrop: bad tree");
		return false;
	}
	if (!t->root) {
		errmsg("indrop: bad knod");
		return false;
	}
	uvlong raw = live_raw(t, pub, "indrop: bad knod");
	if (!raw) return false;
	delete_raw(t, raw);
	t->nlive--;
	return true;
}

uvlong indels(int tree, uvlong chet, uvlong exceed, int (*cmp)(uvlong ref, uvlong chet, void *arg), void *arg) {
	indel_t *t = in_get(tree);
	if (!t || !cmp) {
		errmsg("indels: bad argument");
		return 0;
	}
	rawvec victims = {0};
	for (uvlong cur = lower_raw(t, chet, cmp, arg, false); cur;) {
		uvlong next = successor(cur);
		if (cmp(bt_raw(cur)->ref, chet, arg) != 0) break;
		if (exceed) exceed--;
		else if (!vec_push(&victims, cur)) {
			free(victims.x);
			return 0;
		}
		cur = next;
	}
	uvlong n = victims.n;
	for (uvlong i = 0; i < victims.n; i++) {
		delete_raw(t, victims.x [i]);
		t->nlive--;
	}
	free(victims.x);
	return n;
}

uvlong inref(int tree, uvlong pub) {
	indel_t *t = in_get(tree);
	if (!t) {
		errmsg("inref: bad tree");
		return 0;
	}
	uvlong raw = live_raw(t, pub, "inref: bad knod");
	return raw ? bt_raw(raw)->ref : 0;
}

uvlong innknod(int tree) {
	indel_t *t = in_get(tree);
	return t ? t->nlive : 0;
}

uvlong inknods(int tree, uvlong *buf, uvlong cap) {
	indel_t *t = in_get(tree);
	if (!t || badbuf(buf, cap)) {
		errmsg("inknods: bad indeltree");
		return 0;
	}
	return inorder_copy(t, t->root, buf, cap, 0);
}

bool infind(int tree, uvlong chet, int (*cmp)(uvlong ref, uvlong chet, void *arg), void *arg, uvlong *knod) {
	indel_t *t = in_get(tree);
	if (!t || !cmp) {
		errmsg("infind: bad argument");
		return false;
	}
	uvlong raw = lower_raw(t, chet, cmp, arg, false);
	if (!raw || cmp(bt_raw(raw)->ref, chet, arg) != 0) return false;
	if (knod) *knod = enc(t, raw);
	return true;
}

uvlong inlower(int tree, uvlong chet, int (*cmp)(uvlong ref, uvlong chet, void *arg), void *arg) {
	indel_t *t = in_get(tree);
	if (!t || !cmp) {
		errmsg("inlower: bad argument");
		return 0;
	}
	uvlong raw = lower_raw(t, chet, cmp, arg, false);
	if (!raw) {
		errmsg("inlower: no knod");
		return 0;
	}
	return enc(t, raw);
}

uvlong inupper(int tree, uvlong chet, int (*cmp)(uvlong ref, uvlong chet, void *arg), void *arg) {
	indel_t *t = in_get(tree);
	if (!t || !cmp) {
		errmsg("inupper: bad argument");
		return 0;
	}
	uvlong raw = lower_raw(t, chet, cmp, arg, true);
	if (!raw) {
		errmsg("inupper: no knod");
		return 0;
	}
	return enc(t, raw);
}

uvlong infirst(int tree) {
	indel_t *t = in_get(tree);
	if (!t) {
		errmsg("infirst: bad indeltree");
		return 0;
	}
	uvlong raw = minimum(t->root);
	if (!raw) {
		errmsg("infirst: empty indeltree");
		return 0;
	}
	return enc(t, raw);
}

uvlong inlast(int tree) {
	indel_t *t = in_get(tree);
	if (!t) {
		errmsg("inlast: bad indeltree");
		return 0;
	}
	uvlong raw = maximum(t->root);
	if (!raw) {
		errmsg("inlast: empty indeltree");
		return 0;
	}
	return enc(t, raw);
}

bool innext(int tree, uvlong pub, uvlong *next) {
	indel_t *t = in_get(tree);
	if (!t) {
		errmsg("innext: bad indeltree");
		return false;
	}
	uvlong raw = live_raw(t, pub, "innext: bad knod");
	if (!raw) return false;
	uvlong n = successor(raw);
	if (!n) return false;
	if (next) *next = enc(t, n);
	return true;
}

bool inprev(int tree, uvlong pub, uvlong *prev) {
	indel_t *t = in_get(tree);
	if (!t) {
		errmsg("inprev: bad indeltree");
		return false;
	}
	uvlong raw = live_raw(t, pub, "inprev: bad knod");
	if (!raw) return false;
	uvlong p = predecessor(raw);
	if (!p) return false;
	if (prev) *prev = enc(t, p);
	return true;
}
