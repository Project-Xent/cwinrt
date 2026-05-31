#include "nexus.h"
#include "config.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

#define RXNONE (( uvlong ) -1)

enum
{
	RXNODE_CAP = 64,
	RXNODE_MIN = RXNODE_CAP / 2,
};

typedef int            (*rxcmpfn)(uvlong, uvlong, void *);
typedef struct rxnode  rxnode;
typedef struct rxentry rxentry;
typedef struct rxtree  rxtree;
typedef struct rxplan  rxplan;

struct rxentry {
	uvlong  key;
	uvlong  ref;
	rxnode *leaf;
	uvlong  slot;
	bool    live;
};

struct rxnode {
	bool    leaf;
	rxnode *parent;
	uvlong  n;
	uvlong  keys [RXNODE_CAP + 1];

	union
	{
		uvlong  ents [RXNODE_CAP + 1];
		rxnode *kids [RXNODE_CAP + 2];
	};

	rxnode *prev;
	rxnode *next;
};

struct rxtree {
	int      arena;
	rxnode  *root;
	rxentry *ents;
	uvlong   nent;
	uvlong   nlent;
	uvlong   capent;
	bool     live;
};

struct rxplan {
	rxnode **nodes;
	uvlong   n;
	uvlong   at;
};

static rxtree *rxs;
static int     rxcap;

static bool    table_init(void) {
	if (rxs) return true;
	rxcap = COETUA_RETRXTREE_TABLE_SEED > 0 ? COETUA_RETRXTREE_TABLE_SEED : 1;
	rxs   = ( rxtree * ) calloc(( size_t ) rxcap, sizeof(rxtree));
	if (!rxs) {
		errmsg("retrxtree: out of memory");
		rxcap = 0;
		return false;
	}
	return true;
}

static bool table_grow(void) {
	int  need   = rxcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_RETRXTREE_TABLE_SEED) newcap = COETUA_RETRXTREE_TABLE_SEED;
	rxtree *p = ( rxtree * ) realloc(rxs, ( size_t ) newcap * sizeof(rxtree));
	if (!p) {
		errmsg("retrxtree: out of memory");
		return false;
	}
	memset(p + rxcap, 0, ( size_t ) (newcap - rxcap) * sizeof(rxtree));
	rxs   = p;
	rxcap = newcap;
	return true;
}

static rxtree *rx_get(int tree) {
	if (!table_init() || tree < 0 || tree >= rxcap || !rxs [tree].live) return null;
	return &rxs [tree];
}

static bool    entlive(rxtree *t, uvlong ent) { return t && ent < t->nent && t->ents [ent].live; }

static bool    badbuf(uvlong *buf, uvlong cap) { return !buf && cap; }

static rxnode *new_node(bool leaf) {
	rxnode *n = ( rxnode * ) calloc(1, sizeof(rxnode));
	if (!n) errmsg("retrxtree: out of memory");
	else n->leaf = leaf;
	return n;
}

static void free_plan(rxplan *p) {
	while (p->at < p->n) free(p->nodes [p->at++]);
	free(p->nodes);
	*p = (rxplan) {0};
}

static rxnode *take_node(rxplan *p, bool leaf) {
	rxnode *n = p->nodes [p->at++];
	n->leaf   = leaf;
	return n;
}

static bool prepare_insert_plan(rxnode *leaf, rxplan *p) {
	*p = (rxplan) {0};
	if (!leaf || leaf->n < RXNODE_CAP) return true;
	p->n = 1;
	for (rxnode *n = leaf->parent; n && n->n >= RXNODE_CAP; n = n->parent) p->n++;
	rxnode *top = leaf;
	while (top->parent && top->parent->n >= RXNODE_CAP) top = top->parent;
	if (!top->parent) p->n++;
	p->nodes = ( rxnode ** ) calloc(( size_t ) p->n, sizeof(rxnode *));
	if (!p->nodes) {
		errmsg("retrxtree: out of memory");
		return false;
	}
	for (uvlong i = 0; i < p->n; i++) {
		p->nodes [i] = ( rxnode * ) calloc(1, sizeof(rxnode));
		if (!p->nodes [i]) {
			errmsg("retrxtree: out of memory");
			free_plan(p);
			return false;
		}
	}
	return true;
}

static void free_nodes(rxnode *n) {
	if (!n) return;
	if (!n->leaf)
		for (uvlong i = 0; i <= n->n; i++) free_nodes(n->kids [i]);
	free(n);
}

static bool ensure_ent_cap(rxtree *t) {
	if (t->nent < t->capent) return true;
	uvlong newcap = nextpow2_64(t->nent + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(rxentry))) {
		errmsg("retrxtree: entry capacity overflow");
		return false;
	}
	rxentry *p = ( rxentry * ) realloc(t->ents, ( size_t ) newcap * sizeof(rxentry));
	if (!p) {
		errmsg("retrxtree: out of memory");
		return false;
	}
	memset(p + t->capent, 0, ( size_t ) (newcap - t->capent) * sizeof(rxentry));
	t->ents   = p;
	t->capent = newcap;
	return true;
}

static uvlong first_key(rxtree *t, rxnode *n) {
	while (n && !n->leaf) n = n->kids [0];
	return n && n->n ? t->ents [n->ents [0]].key : 0;
}

static void refresh_keys(rxtree *t, rxnode *n) {
	if (!n || n->leaf) return;
	for (uvlong i = 0; i <= n->n; i++) refresh_keys(t, n->kids [i]);
	for (uvlong i = 0; i < n->n; i++) n->keys [i] = first_key(t, n->kids [i + 1]);
}

static rxnode *leftmost_leaf(rxnode *n) {
	while (n && !n->leaf) n = n->kids [0];
	return n;
}

static rxnode *rightmost_leaf(rxnode *n) {
	while (n && !n->leaf) n = n->kids [n->n];
	return n;
}

static void update_leaf_positions(rxtree *t, rxnode *leaf) {
	for (uvlong i = 0; leaf && i < leaf->n; i++) {
		uvlong e         = leaf->ents [i];
		t->ents [e].leaf = leaf;
		t->ents [e].slot = i;
	}
}

static void update_all_positions(rxtree *t) {
	for (rxnode *l = leftmost_leaf(t->root); l; l = l->next) update_leaf_positions(t, l);
}

static int child_index(rxnode *p, rxnode *c) {
	if (!p) return -1;
	for (uvlong i = 0; i <= p->n; i++)
		if (p->kids [i] == c) return ( int ) i;
	return -1;
}

static rxnode *find_leaf(rxtree *t, uvlong chet, rxcmpfn cmp, void *arg, bool upper_route) {
	rxnode *n = t->root;
	while (n && !n->leaf) {
		uvlong i = 0;
		while (i < n->n) {
			int c = cmp(n->keys [i], chet, arg);
			if (upper_route ? c > 0 : c >= 0) break;
			i++;
		}
		n = n->kids [i];
	}
	return n;
}

static bool lower_pos(rxtree *t, uvlong chet, rxcmpfn cmp, void *arg, bool upper, rxnode **leafp, uvlong *slotp) {
	rxnode *l = find_leaf(t, chet, cmp, arg, upper);
	while (l) {
		for (uvlong i = 0; i < l->n; i++) {
			int c = cmp(t->ents [l->ents [i]].key, chet, arg);
			if (upper ? c > 0 : c >= 0) {
				if (leafp) *leafp = l;
				if (slotp) *slotp = i;
				return true;
			}
		}
		l = l->next;
	}
	return false;
}

static void insert_child_after(rxtree *t, rxnode *left, rxnode *right, rxplan *plan);

static void split_internal(rxtree *t, rxnode *n, rxplan *plan) {
	uvlong  total = n->n + 1;
	uvlong  lch   = (total + 1) / 2;
	uvlong  rch   = total - lch;
	rxnode *r     = take_node(plan, false);
	r->parent     = n->parent;
	for (uvlong i = 0; i < rch; i++) {
		r->kids [i] = n->kids [lch + i];
		if (r->kids [i]) r->kids [i]->parent = r;
		n->kids [lch + i] = null;
	}
	n->n = lch - 1;
	r->n = rch - 1;
	refresh_keys(t, n);
	refresh_keys(t, r);
	insert_child_after(t, n, r, plan);
}

static void insert_child_after(rxtree *t, rxnode *left, rxnode *right, rxplan *plan) {
	rxnode *p = left->parent;
	if (!p) {
		rxnode *root   = take_node(plan, false);
		root->kids [0] = left;
		root->kids [1] = right;
		root->n        = 1;
		left->parent   = root;
		right->parent  = root;
		t->root        = root;
		refresh_keys(t, root);
		return;
	}
	int idx = child_index(p, left);
	if (idx < 0) return;
	for (uvlong i = p->n + 1; i > ( uvlong ) idx + 1; i--) p->kids [i] = p->kids [i - 1];
	p->kids [idx + 1] = right;
	right->parent     = p;
	p->n++;
	refresh_keys(t, p);
	if (p->n > RXNODE_CAP) split_internal(t, p, plan);
}

static void split_leaf(rxtree *t, rxnode *l, rxplan *plan) {
	uvlong  total = l->n;
	uvlong  leftn = total / 2;
	rxnode *r     = take_node(plan, true);
	r->parent     = l->parent;
	r->n          = total - leftn;
	for (uvlong i = 0; i < r->n; i++) r->ents [i] = l->ents [leftn + i];
	l->n    = leftn;
	r->next = l->next;
	r->prev = l;
	if (r->next) r->next->prev = r;
	l->next = r;
	update_leaf_positions(t, l);
	update_leaf_positions(t, r);
	insert_child_after(t, l, r, plan);
}

static void leaf_insert_at(rxtree *t, rxnode *l, uvlong slot, uvlong ent, rxplan *plan) {
	for (uvlong i = l->n; i > slot; i--) l->ents [i] = l->ents [i - 1];
	l->ents [slot] = ent;
	l->n++;
	update_leaf_positions(t, l);
	if (l->n > RXNODE_CAP) split_leaf(t, l, plan);
	refresh_keys(t, t->root);
	update_all_positions(t);
	free_plan(plan);
}

static bool insert_pos_after_equals(rxtree *t, uvlong key, rxcmpfn cmp, void *arg, rxnode **leafp, uvlong *slotp) {
	if (!t->root) return false;
	rxnode *l = null;
	uvlong  s = 0;
	if (!lower_pos(t, key, cmp, arg, false, &l, &s)) {
		l = rightmost_leaf(t->root);
		if (!l) return false;
		*leafp = l;
		*slotp = l->n;
		return true;
	}
	for (;;) {
		while (s < l->n) {
			int c = cmp(t->ents [l->ents [s]].key, key, arg);
			if (c > 0) {
				*leafp = l;
				*slotp = s;
				return true;
			}
			s++;
		}
		if (!l->next) {
			*leafp = l;
			*slotp = l->n;
			return true;
		}
		l = l->next;
		s = 0;
	}
}

static void remove_child_at(rxtree *t, rxnode *p, uvlong idx);

static void shrink_root(rxtree *t) {
	if (!t->root || t->root->leaf || t->root->n) return;
	rxnode *old = t->root;
	t->root     = old->kids [0];
	if (t->root) t->root->parent = null;
	free(old);
}

static void rebalance_internal(rxtree *t, rxnode *n) {
	if (!n || n == t->root) {
		shrink_root(t);
		return;
	}
	if (n->n >= RXNODE_MIN) return;
	rxnode *p   = n->parent;
	int     idx = child_index(p, n);
	rxnode *l   = idx > 0 ? p->kids [idx - 1] : null;
	rxnode *r   = (idx >= 0 && ( uvlong ) idx < p->n) ? p->kids [idx + 1] : null;
	if (l && l->n > RXNODE_MIN) {
		for (uvlong i = n->n + 1; i > 0; i--) n->kids [i] = n->kids [i - 1];
		n->kids [0] = l->kids [l->n];
		if (n->kids [0]) n->kids [0]->parent = n;
		l->kids [l->n] = null;
		l->n--;
		n->n++;
	}
	else if (r && r->n > RXNODE_MIN) {
		n->kids [n->n + 1] = r->kids [0];
		if (n->kids [n->n + 1]) n->kids [n->n + 1]->parent = n;
		n->n++;
		for (uvlong i = 0; i < r->n; i++) r->kids [i] = r->kids [i + 1];
		r->kids [r->n] = null;
		r->n--;
	}
	else if (l) {
		uvlong base = l->n + 1;
		for (uvlong i = 0; i <= n->n; i++) {
			l->kids [base + i] = n->kids [i];
			if (l->kids [base + i]) l->kids [base + i]->parent = l;
		}
		l->n += n->n + 1;
		free(n);
		remove_child_at(t, p, ( uvlong ) idx);
	}
	else if (r) {
		uvlong base = n->n + 1;
		for (uvlong i = 0; i <= r->n; i++) {
			n->kids [base + i] = r->kids [i];
			if (n->kids [base + i]) n->kids [base + i]->parent = n;
		}
		n->n += r->n + 1;
		free(r);
		remove_child_at(t, p, ( uvlong ) idx + 1);
	}
}

static void remove_child_at(rxtree *t, rxnode *p, uvlong idx) {
	if (!p) return;
	for (uvlong i = idx; i < p->n; i++) p->kids [i] = p->kids [i + 1];
	p->kids [p->n] = null;
	if (p->n) p->n--;
	if (p == t->root) shrink_root(t);
	else rebalance_internal(t, p);
}

static void rebalance_leaf(rxtree *t, rxnode *l) {
	if (!l || l == t->root || l->n >= RXNODE_MIN) return;
	rxnode *p   = l->parent;
	int     idx = child_index(p, l);
	rxnode *z   = idx > 0 ? p->kids [idx - 1] : null;
	rxnode *o   = (idx >= 0 && ( uvlong ) idx < p->n) ? p->kids [idx + 1] : null;
	if (z && z->n > RXNODE_MIN) {
		for (uvlong i = l->n; i > 0; i--) l->ents [i] = l->ents [i - 1];
		l->ents [0] = z->ents [z->n - 1];
		z->n--;
		l->n++;
		update_leaf_positions(t, z);
		update_leaf_positions(t, l);
	}
	else if (o && o->n > RXNODE_MIN) {
		l->ents [l->n++] = o->ents [0];
		for (uvlong i = 0; i + 1 < o->n; i++) o->ents [i] = o->ents [i + 1];
		o->n--;
		update_leaf_positions(t, l);
		update_leaf_positions(t, o);
	}
	else if (z) {
		for (uvlong i = 0; i < l->n; i++) z->ents [z->n + i] = l->ents [i];
		z->n    += l->n;
		z->next  = l->next;
		if (z->next) z->next->prev = z;
		update_leaf_positions(t, z);
		free(l);
		remove_child_at(t, p, ( uvlong ) idx);
	}
	else if (o) {
		for (uvlong i = 0; i < o->n; i++) l->ents [l->n + i] = o->ents [i];
		l->n    += o->n;
		l->next  = o->next;
		if (l->next) l->next->prev = l;
		update_leaf_positions(t, l);
		free(o);
		remove_child_at(t, p, ( uvlong ) idx + 1);
	}
}

static void remove_entry_at(rxtree *t, rxnode *l, uvlong slot) {
	for (uvlong i = slot; i + 1 < l->n; i++) l->ents [i] = l->ents [i + 1];
	if (l->n) l->n--;
	update_leaf_positions(t, l);
	if (l == t->root && l->n == 0) {
		free(l);
		t->root = null;
		return;
	}
	rebalance_leaf(t, l);
	refresh_keys(t, t->root);
	update_all_positions(t);
}

int mkrxtree(int arena) {
	if (!table_init()) return -1;
	for (;;) {
		for (int i = 0; i < rxcap; i++) {
			if (rxs [i].live) continue;
			rxs [i] = (rxtree) {.arena = arena, .live = true};
			return i;
		}
		if (!table_grow()) return -1;
	}
}

void rmrxtree(int tree) {
	rxtree *t = rx_get(tree);
	if (!t) return;
	free_nodes(t->root);
	free(t->ents);
	*t = (rxtree) {0};
}

uvlong rxput(int tree, uvlong key, uvlong ref, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg) {
	rxtree *t = rx_get(tree);
	if (!t || !cmp) {
		errmsg("rxput: bad argument");
		return RXNONE;
	}
	if (!ensure_ent_cap(t)) return RXNONE;
	if (!t->root) {
		t->root = new_node(true);
		if (!t->root) return RXNONE;
	}
	rxnode *l = null;
	uvlong  s = 0;
	if (!insert_pos_after_equals(t, key, cmp, arg, &l, &s)) {
		l = t->root;
		s = 0;
	}
	rxplan plan = {0};
	if (!prepare_insert_plan(l, &plan)) return RXNONE;
	uvlong ent    = t->nent;
	t->ents [ent] = (rxentry) {.key = key, .ref = ref, .live = true};
	t->nent++;
	t->nlent++;
	leaf_insert_at(t, l, s, ent, &plan);
	return ent;
}

bool rxdel(int tree, uvlong ent) {
	rxtree *t = rx_get(tree);
	if (!entlive(t, ent)) {
		errmsg("rxdel: bad entry");
		return false;
	}
	rxentry *e = &t->ents [ent];
	rxnode  *l = e->leaf;
	uvlong   s = e->slot;
	e->live    = false;
	e->leaf    = null;
	e->slot    = 0;
	t->nlent--;
	remove_entry_at(t, l, s);
	return true;
}

uvlong rxdels(int tree, uvlong ent, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg) {
	rxtree *t = rx_get(tree);
	if (!entlive(t, ent) || !cmp) {
		errmsg("rxdels: bad argument");
		return 0;
	}
	uvlong key = t->ents [ent].key;
	uvlong n   = 0;
	uvlong cur = ent;
	while (entlive(t, cur) && cmp(t->ents [cur].key, key, arg) == 0) {
		uvlong next = 0;
		bool   has  = rxnext(tree, cur, &next);
		rxdel(tree, cur);
		n++;
		if (!has) break;
		cur = next;
	}
	return n;
}

uvlong rxkey(int tree, uvlong ent) {
	rxtree *t = rx_get(tree);
	if (!entlive(t, ent)) {
		errmsg("rxkey: bad entry");
		return 0;
	}
	return t->ents [ent].key;
}

uvlong rxref(int tree, uvlong ent) {
	rxtree *t = rx_get(tree);
	if (!entlive(t, ent)) {
		errmsg("rxref: bad entry");
		return 0;
	}
	return t->ents [ent].ref;
}

void rrxref(int tree, uvlong ent, uvlong ref) {
	rxtree *t = rx_get(tree);
	if (!entlive(t, ent)) {
		errmsg("rrxref: bad entry");
		return;
	}
	t->ents [ent].ref = ref;
}

uvlong rxnentry(int tree) {
	rxtree *t = rx_get(tree);
	return t ? t->nlent : 0;
}

uvlong rxentries(int tree, uvlong *buf, uvlong cap) {
	rxtree *t = rx_get(tree);
	if (!t || badbuf(buf, cap)) {
		errmsg("rxentries: bad retrxtree");
		return 0;
	}
	uvlong n = 0;
	for (rxnode *l = leftmost_leaf(t->root); l; l = l->next) {
		for (uvlong i = 0; i < l->n; i++) {
			if (buf && n < cap) buf [n] = l->ents [i];
			n++;
		}
	}
	return n;
}

bool rxfind(int tree, uvlong chet, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg, uvlong *ent) {
	rxtree *t = rx_get(tree);
	if (!t || !cmp) {
		errmsg("rxfind: bad argument");
		return false;
	}
	rxnode *l = null;
	uvlong  s = 0;
	if (!lower_pos(t, chet, cmp, arg, false, &l, &s)) return false;
	if (cmp(t->ents [l->ents [s]].key, chet, arg) != 0) return false;
	if (ent) *ent = l->ents [s];
	return true;
}

uvlong rxlower(int tree, uvlong chet, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg) {
	rxtree *t = rx_get(tree);
	if (!t || !cmp) {
		errmsg("rxlower: bad argument");
		return RXNONE;
	}
	rxnode *l = null;
	uvlong  s = 0;
	if (!lower_pos(t, chet, cmp, arg, false, &l, &s)) {
		errmsg("rxlower: no entry");
		return RXNONE;
	}
	return l->ents [s];
}

uvlong rxupper(int tree, uvlong chet, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg) {
	rxtree *t = rx_get(tree);
	if (!t || !cmp) {
		errmsg("rxupper: bad argument");
		return RXNONE;
	}
	rxnode *l = null;
	uvlong  s = 0;
	if (!lower_pos(t, chet, cmp, arg, true, &l, &s)) {
		errmsg("rxupper: no entry");
		return RXNONE;
	}
	return l->ents [s];
}

uvlong rxfirst(int tree) {
	rxtree *t = rx_get(tree);
	if (!t) {
		errmsg("rxfirst: bad retrxtree");
		return RXNONE;
	}
	rxnode *l = leftmost_leaf(t->root);
	if (!l || !l->n) {
		errmsg("rxfirst: empty retrxtree");
		return RXNONE;
	}
	return l->ents [0];
}

uvlong rxlast(int tree) {
	rxtree *t = rx_get(tree);
	if (!t) {
		errmsg("rxlast: bad retrxtree");
		return RXNONE;
	}
	rxnode *l = rightmost_leaf(t->root);
	if (!l || !l->n) {
		errmsg("rxlast: empty retrxtree");
		return RXNONE;
	}
	return l->ents [l->n - 1];
}

bool rxnext(int tree, uvlong ent, uvlong *next) {
	rxtree *t = rx_get(tree);
	if (!entlive(t, ent)) {
		errmsg("rxnext: bad entry");
		return false;
	}
	rxnode *l = t->ents [ent].leaf;
	uvlong  s = t->ents [ent].slot + 1;
	if (s < l->n) {
		if (next) *next = l->ents [s];
		return true;
	}
	l = l->next;
	if (!l || !l->n) return false;
	if (next) *next = l->ents [0];
	return true;
}

bool rxprev(int tree, uvlong ent, uvlong *prev) {
	rxtree *t = rx_get(tree);
	if (!entlive(t, ent)) {
		errmsg("rxprev: bad entry");
		return false;
	}
	rxnode *l = t->ents [ent].leaf;
	uvlong  s = t->ents [ent].slot;
	if (s) {
		if (prev) *prev = l->ents [s - 1];
		return true;
	}
	l = l->prev;
	if (!l || !l->n) return false;
	if (prev) *prev = l->ents [l->n - 1];
	return true;
}

uvlong rxrange(int tree, uvlong lo, uvlong hi, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg, uvlong *buf,
               uvlong cap) {
	rxtree *t = rx_get(tree);
	if (!t || !cmp || badbuf(buf, cap)) {
		errmsg("rxrange: bad argument");
		return 0;
	}
	rxnode *l = null;
	uvlong  s = 0;
	if (!lower_pos(t, lo, cmp, arg, false, &l, &s)) return 0;
	uvlong n = 0;
	for (; l; l = l->next, s = 0) {
		for (; s < l->n; s++) {
			uvlong e = l->ents [s];
			if (cmp(t->ents [e].key, hi, arg) >= 0) return n;
			if (buf && n < cap) buf [n] = e;
			n++;
		}
	}
	return n;
}
