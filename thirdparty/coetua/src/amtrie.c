#include "nexus.h"
#include "config.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

#define AMNONE (( uvlong ) -1)

enum
{
	AMSHIFT  = 6,
	AMFAN    = 64,
	AMLEVELS = 11,
};

typedef struct ambranch ambranch;
typedef struct ambucket ambucket;
typedef struct amchild  amchild;
typedef struct amentry  amentry;
typedef struct amtrie   amtrie;

struct amchild {
	bool branch;

	union
	{
		ambranch *br;
		ambucket *bk;
	};
};

struct ambranch {
	uvlong   map;
	amchild *kids;
};

struct ambucket {
	uvlong *ents;
	uvlong  n;
	uvlong  cap;
};

struct amentry {
	uvlong    hash;
	uvlong    key;
	uvlong    ref;
	ambucket *bucket;
	uvlong    slot;
	bool      live;
};

struct amtrie {
	int      arena;
	amchild  root;
	amentry *ents;
	uvlong   nent;
	uvlong   nlent;
	uvlong   capent;
	bool     live;
};

static amtrie *ams;
static int     amcap;

static bool    table_init(void) {
	if (ams) return true;
	amcap = COETUA_AMTRIE_TABLE_SEED > 0 ? COETUA_AMTRIE_TABLE_SEED : 1;
	ams   = ( amtrie * ) calloc(( size_t ) amcap, sizeof(amtrie));
	if (!ams) {
		errmsg("amtrie: out of memory");
		amcap = 0;
		return false;
	}
	return true;
}

static bool table_grow(void) {
	int  need   = amcap + 1;
	uint ucap   = nextpow2(( uint ) need);
	int  newcap = ( int ) ucap;
	if (newcap < COETUA_AMTRIE_TABLE_SEED) newcap = COETUA_AMTRIE_TABLE_SEED;
	amtrie *p = ( amtrie * ) realloc(ams, ( size_t ) newcap * sizeof(amtrie));
	if (!p) {
		errmsg("amtrie: out of memory");
		return false;
	}
	memset(p + amcap, 0, ( size_t ) (newcap - amcap) * sizeof(amtrie));
	ams   = p;
	amcap = newcap;
	return true;
}

static amtrie *am_get(int trie) {
	if (!table_init() || trie < 0 || trie >= amcap || !ams [trie].live) return null;
	return &ams [trie];
}

static bool entlive(amtrie *t, uvlong ent) { return t && ent < t->nent && t->ents [ent].live; }

static bool badbuf(uvlong *buf, uvlong cap) { return !buf && cap; }

static uint frag(uvlong hash, uint level) {
	return level >= AMLEVELS ? 0 : ( uint ) ((hash >> (level * AMSHIFT)) & 0x3full);
}

static uint      rank64(uvlong map, uint bit) { return ( uint ) popcnt64(map & ((( uvlong ) 1 << bit) - 1)); }

static ambranch *new_branch(void) {
	ambranch *b = ( ambranch * ) calloc(1, sizeof(ambranch));
	if (!b) errmsg("amtrie: out of memory");
	return b;
}

static ambucket *new_bucket(void) {
	ambucket *b = ( ambucket * ) calloc(1, sizeof(ambucket));
	if (!b) errmsg("amtrie: out of memory");
	return b;
}

static void free_child(amchild c) {
	if (c.branch) {
		ambranch *b = c.br;
		if (!b) return;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++) free_child(b->kids [i]);
		free(b->kids);
		free(b);
	}
	else if (c.bk) {
		free(c.bk->ents);
		free(c.bk);
	}
}

static bool ensure_ent_cap(amtrie *t) {
	if (t->nent < t->capent) return true;
	uvlong newcap = nextpow2_64(t->nent + 1);
	if (newcap < 16) newcap = 16;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(amentry))) {
		errmsg("amtrie: entry capacity overflow");
		return false;
	}
	amentry *p = ( amentry * ) realloc(t->ents, ( size_t ) newcap * sizeof(amentry));
	if (!p) {
		errmsg("amtrie: out of memory");
		return false;
	}
	memset(p + t->capent, 0, ( size_t ) (newcap - t->capent) * sizeof(amentry));
	t->ents   = p;
	t->capent = newcap;
	return true;
}

static bool ensure_bucket_cap(ambucket *b) {
	if (b->n < b->cap) return true;
	uvlong newcap = nextpow2_64(b->n + 1);
	if (newcap < 4) newcap = 4;
	if (newcap > ( uvlong ) (SIZE_MAX / sizeof(uvlong))) {
		errmsg("amtrie: bucket capacity overflow");
		return false;
	}
	uvlong *p = ( uvlong * ) realloc(b->ents, ( size_t ) newcap * sizeof(uvlong));
	if (!p) {
		errmsg("amtrie: out of memory");
		return false;
	}
	b->ents = p;
	b->cap  = newcap;
	return true;
}

static void update_bucket(amtrie *t, ambucket *b) {
	for (uvlong i = 0; b && i < b->n; i++) {
		uvlong e           = b->ents [i];
		t->ents [e].bucket = b;
		t->ents [e].slot   = i;
	}
}

static bool bucket_append(amtrie *t, ambucket *b, uvlong ent) {
	if (!ensure_bucket_cap(b)) return false;
	b->ents [b->n++] = ent;
	update_bucket(t, b);
	return true;
}

static bool bucket_append_raw(ambucket *b, uvlong ent) {
	if (!ensure_bucket_cap(b)) return false;
	b->ents [b->n++] = ent;
	return true;
}

static bool branch_insert_child(ambranch *b, uint f, amchild c) {
	uvlong bit = ( uvlong ) 1 << f;
	uint   idx = rank64(b->map, f);
	uint   n   = ( uint ) popcnt64(b->map);
	if (b->map & bit) {
		b->kids [idx] = c;
		return true;
	}
	amchild *p = ( amchild * ) realloc(b->kids, ( size_t ) (n + 1) * sizeof(amchild));
	if (!p) {
		errmsg("amtrie: out of memory");
		return false;
	}
	b->kids = p;
	for (uint i = n; i > idx; i--) b->kids [i] = b->kids [i - 1];
	b->kids [idx]  = c;
	b->map        |= bit;
	return true;
}

static bool insert_into_child(amtrie *t, amchild *cp, uint level, uvlong ent);

static bool same_hashes(amtrie *t, uvlong *xs, uvlong n) {
	if (n < 2) return true;
	uvlong h = t->ents [xs [0]].hash;
	for (uvlong i = 1; i < n; i++)
		if (t->ents [xs [i]].hash != h) return false;
	return true;
}

static bool build_bucket(uvlong *xs, uvlong n, amchild *out) {
	ambucket *b = new_bucket();
	if (!b) return false;
	for (uvlong i = 0; i < n; i++) {
		if (!bucket_append_raw(b, xs [i])) {
			free_child((amchild) {.bk = b});
			return false;
		}
	}
	*out = (amchild) {.bk = b};
	return true;
}

static bool build_subtree(amtrie *t, uvlong *xs, uvlong n, uint level, amchild *out) {
	*out = (amchild) {0};
	if (level >= AMLEVELS || same_hashes(t, xs, n)) return build_bucket(xs, n, out);

	ambranch *br = new_branch();
	if (!br) return false;
	uvlong counts [AMFAN] = {0};
	for (uvlong i = 0; i < n; i++) counts [frag(t->ents [xs [i]].hash, level)]++;
	for (uint f = 0; f < AMFAN; f++) {
		if (!counts [f]) continue;
		uvlong *grp = ( uvlong * ) malloc(( size_t ) counts [f] * sizeof(uvlong));
		if (!grp) {
			errmsg("amtrie: out of memory");
			free_child((amchild) {.branch = true, .br = br});
			return false;
		}
		uvlong at = 0;
		for (uvlong i = 0; i < n; i++)
			if (frag(t->ents [xs [i]].hash, level) == f) grp [at++] = xs [i];
		amchild c  = {0};
		bool    ok = build_subtree(t, grp, counts [f], level + 1, &c);
		free(grp);
		if (!ok || !branch_insert_child(br, f, c)) {
			free_child(c);
			free_child((amchild) {.branch = true, .br = br});
			return false;
		}
	}
	*out = (amchild) {.branch = true, .br = br};
	return true;
}

static void update_child_positions(amtrie *t, amchild c) {
	if (c.branch) {
		ambranch *b = c.br;
		for (uint i = 0; i < ( uint ) popcnt64(b->map); i++) update_child_positions(t, b->kids [i]);
	}
	else update_bucket(t, c.bk);
}

static bool split_bucket_insert(amtrie *t, amchild *cp, uint level, uvlong ent) {
	ambucket *old = cp->bk;
	if (level >= AMLEVELS
	    || (old->n && same_hashes(t, old->ents, old->n) && t->ents [old->ents [0]].hash == t->ents [ent].hash))
		return bucket_append(t, old, ent);
	uvlong  n  = old->n + 1;
	uvlong *xs = ( uvlong * ) malloc(( size_t ) n * sizeof(uvlong));
	if (!xs) {
		errmsg("amtrie: out of memory");
		return false;
	}
	for (uvlong i = 0; i < old->n; i++) xs [i] = old->ents [i];
	xs [old->n]  = ent;
	amchild repl = {0};
	if (!build_subtree(t, xs, n, level, &repl)) {
		free(xs);
		return false;
	}
	free(xs);
	*cp = repl;
	update_child_positions(t, repl);
	free(old->ents);
	free(old);
	return true;
}

static bool insert_into_branch(amtrie *t, ambranch *br, uint level, uvlong ent) {
	uint   f   = frag(t->ents [ent].hash, level);
	uvlong bit = ( uvlong ) 1 << f;
	uint   idx = rank64(br->map, f);
	if (br->map & bit) return insert_into_child(t, &br->kids [idx], level + 1, ent);
	ambucket *b = new_bucket();
	if (!b) return false;
	if (!bucket_append(t, b, ent)) {
		free(b);
		return false;
	}
	if (!branch_insert_child(br, f, (amchild) {.bk = b})) {
		free_child((amchild) {.bk = b});
		return false;
	}
	return true;
}

static bool insert_into_child(amtrie *t, amchild *cp, uint level, uvlong ent) {
	if (cp->branch) return insert_into_branch(t, cp->br, level, ent);
	if (cp->bk) return split_bucket_insert(t, cp, level, ent);
	ambucket *b = new_bucket();
	if (!b) return false;
	if (!bucket_append(t, b, ent)) {
		free(b);
		return false;
	}
	*cp = (amchild) {.bk = b};
	return true;
}

static void remove_child_at(ambranch *b, uint f) {
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
		amchild *p = ( amchild * ) realloc(b->kids, ( size_t ) (n - 1) * sizeof(amchild));
		if (p) b->kids = p;
	}
}

static bool prune_child(amchild *cp, uvlong hash, uint level) {
	if (!cp->branch) return cp->bk && cp->bk->n == 0;
	ambranch *br  = cp->br;
	uint      f   = frag(hash, level);
	uvlong    bit = ( uvlong ) 1 << f;
	if (br->map & bit) {
		uint idx = rank64(br->map, f);
		if (prune_child(&br->kids [idx], hash, level + 1)) {
			free_child(br->kids [idx]);
			remove_child_at(br, f);
		}
	}
	return br->map == 0;
}

static void bucket_remove(amtrie *t, ambucket *b, uvlong slot) {
	for (uvlong i = slot; i + 1 < b->n; i++) b->ents [i] = b->ents [i + 1];
	if (b->n) b->n--;
	update_bucket(t, b);
}

static ambucket *find_bucket(amtrie *t, uvlong hash) {
	amchild *c = &t->root;
	for (uint level = 0; c && c->branch; level++) {
		ambranch *b   = c->br;
		uint      f   = frag(hash, level);
		uvlong    bit = ( uvlong ) 1 << f;
		if (!(b->map & bit)) return null;
		c = &b->kids [rank64(b->map, f)];
	}
	return c ? c->bk : null;
}

static uvlong enum_child(amchild c, uvlong *buf, uvlong cap, uvlong n) {
	if (c.branch) {
		ambranch *b    = c.br;
		uint      nkid = ( uint ) popcnt64(b->map);
		for (uint i = 0; i < nkid; i++) n = enum_child(b->kids [i], buf, cap, n);
	}
	else if (c.bk) {
		for (uvlong i = 0; i < c.bk->n; i++) {
			if (buf && n < cap) buf [n] = c.bk->ents [i];
			n++;
		}
	}
	return n;
}

static bool prefix_match(uvlong hash, uvlong prefix, uint nbits) {
	if (!nbits) return true;
	uvlong mask = nbits == 64 ? ( uvlong ) -1 : ((( uvlong ) 1 << nbits) - 1);
	return (hash & mask) == (prefix & mask);
}

static uvlong enum_prefix(amtrie *t, amchild c, uvlong prefix, uint total, uint rem, uint level, uvlong *buf,
                          uvlong cap, uvlong n) {
	if (!rem) return enum_child(c, buf, cap, n);
	if (c.branch) {
		ambranch *b = c.br;
		if (rem >= AMSHIFT) {
			uint   f   = frag(prefix, level);
			uvlong bit = ( uvlong ) 1 << f;
			if (!(b->map & bit)) return n;
			return enum_prefix(t, b->kids [rank64(b->map, f)], prefix, total, rem - AMSHIFT, level + 1, buf, cap, n);
		}
		uint mask = (( uint ) 1 << rem) - 1;
		uint want = ( uint ) ((prefix >> (level * AMSHIFT)) & mask);
		for (uint f = 0; f < AMFAN; f++) {
			if ((f & mask) != want) continue;
			uvlong bit = ( uvlong ) 1 << f;
			if (b->map & bit) n = enum_child(b->kids [rank64(b->map, f)], buf, cap, n);
		}
		return n;
	}
	if (!c.bk) return n;
	for (uvlong i = 0; i < c.bk->n; i++) {
		uvlong e = c.bk->ents [i];
		if (!prefix_match(t->ents [e].hash, prefix, total)) continue;
		if (buf && n < cap) buf [n] = e;
		n++;
	}
	return n;
}

int mkamtrie(int arena) {
	if (!table_init()) return -1;
	for (;;) {
		for (int i = 0; i < amcap; i++) {
			if (ams [i].live) continue;
			ams [i] = (amtrie) {.arena = arena, .live = true};
			return i;
		}
		if (!table_grow()) return -1;
	}
}

void rmamtrie(int trie) {
	amtrie *t = am_get(trie);
	if (!t) return;
	free_child(t->root);
	free(t->ents);
	*t = (amtrie) {0};
}

uvlong amput(int trie, uvlong hash, uvlong key, uvlong ref) {
	amtrie *t = am_get(trie);
	if (!t) {
		errmsg("amput: bad amtrie");
		return AMNONE;
	}
	if (!ensure_ent_cap(t)) return AMNONE;
	uvlong ent    = t->nent;
	t->ents [ent] = (amentry) {.hash = hash, .key = key, .ref = ref, .live = true};
	if (!insert_into_child(t, &t->root, 0, ent)) {
		t->ents [ent] = (amentry) {0};
		return AMNONE;
	}
	t->nent++;
	t->nlent++;
	return ent;
}

bool amdel(int trie, uvlong ent) {
	amtrie *t = am_get(trie);
	if (!entlive(t, ent)) {
		errmsg("amdel: bad entry");
		return false;
	}
	amentry  *e    = &t->ents [ent];
	ambucket *b    = e->bucket;
	uvlong    hash = e->hash;
	bucket_remove(t, b, e->slot);
	e->live   = false;
	e->bucket = null;
	e->slot   = 0;
	t->nlent--;
	if (prune_child(&t->root, hash, 0)) {
		free_child(t->root);
		t->root = (amchild) {0};
	}
	return true;
}

uvlong amdels(int trie, uvlong hash, uvlong chet, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg) {
	amtrie *t = am_get(trie);
	if (!t || !cmp) {
		errmsg("amdels: bad argument");
		return 0;
	}
	uvlong n = 0;
	for (;;) {
		ambucket *b   = find_bucket(t, hash);
		uvlong    ent = AMNONE;
		for (uvlong i = 0; b && i < b->n; i++) {
			uvlong e = b->ents [i];
			if (t->ents [e].hash == hash && cmp(t->ents [e].key, chet, arg) == 0) {
				ent = e;
				break;
			}
		}
		if (ent == AMNONE) break;
		amdel(trie, ent);
		n++;
	}
	return n;
}

uvlong amhash(int trie, uvlong ent) {
	amtrie *t = am_get(trie);
	if (!entlive(t, ent)) {
		errmsg("amhash: bad entry");
		return 0;
	}
	return t->ents [ent].hash;
}

uvlong amkey(int trie, uvlong ent) {
	amtrie *t = am_get(trie);
	if (!entlive(t, ent)) {
		errmsg("amkey: bad entry");
		return 0;
	}
	return t->ents [ent].key;
}

uvlong amref(int trie, uvlong ent) {
	amtrie *t = am_get(trie);
	if (!entlive(t, ent)) {
		errmsg("amref: bad entry");
		return 0;
	}
	return t->ents [ent].ref;
}

void ramref(int trie, uvlong ent, uvlong ref) {
	amtrie *t = am_get(trie);
	if (!entlive(t, ent)) {
		errmsg("ramref: bad entry");
		return;
	}
	t->ents [ent].ref = ref;
}

uvlong amnentry(int trie) {
	amtrie *t = am_get(trie);
	return t ? t->nlent : 0;
}

uvlong amentries(int trie, uvlong *buf, uvlong cap) {
	amtrie *t = am_get(trie);
	if (!t || badbuf(buf, cap)) {
		errmsg("amentries: bad argument");
		return 0;
	}
	return enum_child(t->root, buf, cap, 0);
}

bool amfind(int trie, uvlong hash, uvlong chet, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg,
            uvlong *ent) {
	amtrie *t = am_get(trie);
	if (!t || !cmp) {
		errmsg("amfind: bad argument");
		return false;
	}
	ambucket *b = find_bucket(t, hash);
	for (uvlong i = 0; b && i < b->n; i++) {
		uvlong e = b->ents [i];
		if (t->ents [e].hash != hash || cmp(t->ents [e].key, chet, arg) != 0) continue;
		if (ent) *ent = e;
		return true;
	}
	return false;
}

uvlong ammatches(int trie, uvlong hash, uvlong chet, int (*cmp)(uvlong key, uvlong chet, void *arg), void *arg,
                 uvlong *buf, uvlong cap) {
	amtrie *t = am_get(trie);
	if (!t || !cmp || badbuf(buf, cap)) {
		errmsg("ammatches: bad argument");
		return 0;
	}
	uvlong    n = 0;
	ambucket *b = find_bucket(t, hash);
	for (uvlong i = 0; b && i < b->n; i++) {
		uvlong e = b->ents [i];
		if (t->ents [e].hash != hash || cmp(t->ents [e].key, chet, arg) != 0) continue;
		if (buf && n < cap) buf [n] = e;
		n++;
	}
	return n;
}

uvlong amprefix(int trie, uvlong prefix, uint nbits, uvlong *buf, uvlong cap) {
	amtrie *t = am_get(trie);
	if (!t || nbits > 64 || badbuf(buf, cap)) {
		errmsg("amprefix: bad argument");
		return 0;
	}
	return enum_prefix(t, t->root, prefix, nbits, nbits, 0, buf, cap, 0);
}
