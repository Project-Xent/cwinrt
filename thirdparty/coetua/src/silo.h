#pragma once
#include "atom.h"
#include "config.h"

/* ═══════════════════════════════════════════════════════
   Silo — handle-indexed containers.
   Linear silos store uvlong elements; hash silos copy byte-string keys
   and map/multiset values into internal arena-backed storage.
   Descriptor storage grows from seed sizes in config.h; only
   simultaneous live containers count.

   Threading: descriptor creation/destruction and all mutation are not
   internally synchronized.  Confine a silo to one thread or serialize
   access externally.

   Pointers returned by swath(), spew() on hash silos, iterator layouts,
   cartesprod(), and cartprodms() are borrowed internal addresses.  They
   remain valid only until the referenced source silo is mutated, compacted,
   destroyed, or its arena is destroyed.

   Mutation invalidates outstanding ordinary iterators and all borrowed
   pointers for that silo.  Obliterating iterators mutate as they yield;
   do not use previously yielded borrowed pointers after requesting the
   next element.
   ═══════════════════════════════════════════════════════ */

/* ── Stack ───────────────────────────────────────────── */
int    mkstack(int arena);
void   push(int stack, uvlong data);
uvlong pop(int stack);
uvlong peep(int stack); /* peek at top without popping */
void   rmstack(int stack);

/* ── Queue ───────────────────────────────────────────── */
int    mkqueue(int arena);
void   enqueue(int queue, uvlong data);
uvlong dequeue(int queue);
uvlong peekq(int queue); /* peek at front without dequeuing */
void   rmqueue(int queue);

/* ── Sequence (dynamic array) ────────────────────────── */
int    mkseq(int arena);
void   atch(int seq, uvlong data);             /* append */
void   place(int seq, vlong pos, uvlong data); /* insert/replace at pos */
uvlong drop(int seq, vlong pos);               /* remove and return at pos */
void   swap(int seq, vlong pa, vlong pb);      /* swap two positions */
uvlong pick(int seq, vlong pos);               /* read at pos */
uvlong slen(int seq);                          /* number of elements */
/* Return a borrowed pointer to a contiguous sequence span.  Invalidated
   by any later mutation of seq. */
uvlong swath(int seq, vlong start, vlong end, uvlong **data);
void   rmseq(int seq);

/* ── Common ──────────────────────────────────────────── */
/* Return the number of elements in a stack, queue, or sequence. */
uvlong cize(int desc);

/* Return the cardinality of a set or map. */
uvlong carten(int desc);

/* Empty the silo (keep allocated capacity) */
void   teem(int desc);

/* Rebuild hash table to clear tombstones (set/map/multiset only).
   Invalidates borrowed pointers for that hash silo.  For linear silos this is a no-op. */
void   compact(int desc);

/* ── Set (hash set, stores byte strings) ──────────────── */
int    mkset(int arena);
void   adds(int set, void *data, uvlong len);
void   dels(int set, void *data, uvlong len);
bool   mems(int set, void *data, uvlong len);

static void inline addsa(int set, arrst data) { adds(set, data.x, data.len); }

static void inline delsa(int set, arrst data) { dels(set, data.x, data.len); }

static bool inline memsa(int set, arrst data) { return mems(set, data.x, data.len); }

int  cartesprod(int arena, int a, int b);
void rmset(int set);

/* ── Map (hash map, key→value byte strings) ──────────── */
int  mkmap(int arena);
/* Insert or update; copies both key and val */
void insert(int map, void *key, uvlong klen, void *val, uvlong vlen);
/* Look up; returns true and copies val to buf (up to *vlen bytes) if found.
   On entry *vlen is buffer capacity; on return *vlen is actual value length. */
bool lookup(int map, void *key, uvlong klen, void *buf, uvlong *vlen);
/* Delete; returns true if key existed */
bool oblit(int map, void *key, uvlong klen);
/* Update an existing key only; absent keys are unchanged. */
void revamp(int map, void *key, uvlong klen, void *val, uvlong vlen);

static void inline inserta(int map, arrst key, arrst val) { insert(map, key.x, key.len, val.x, val.len); }

static bool inline lookupa(int map, arrst key, arrst val, uvlong *vlen) {
	return lookup(map, key.x, key.len, val.x, vlen);
}

static bool inline oblita(int map, arrst key) { return oblit(map, key.x, key.len); }

static void inline revampa(int map, arrst key, arrst val) { revamp(map, key.x, key.len, val.x, val.len); }

/* Join src into dst: 0 inner, 1 full/outer, 2 left/self.
   Shared keys take src's value.  Mutates dst and invalidates dst pointers/iterators. */
void   conjoin(int dst, int src, int method);
void   rmmap(int map);

/* ── Multiset (byte strings with multiplicity) ───────── */
int    mkmultiset(int arena);
void   addms(int ms, void *data, uvlong len);
void   delms(int ms, void *data, uvlong len); /* delete one instance */
void   prgms(int ms, void *data, uvlong len); /* purge all instances */
bool   memms(int ms, void *data, uvlong len);
uvlong cntms(int ms, void *data, uvlong len);

static void inline addmsa(int ms, arrst data) { addms(ms, data.x, data.len); }

static void inline delmsa(int ms, arrst data) { delms(ms, data.x, data.len); }

static void inline prgmsa(int ms, arrst data) { prgms(ms, data.x, data.len); }

static bool inline memmsa(int ms, arrst data) { return memms(ms, data.x, data.len); }

static uvlong inline cntmsa(int ms, arrst data) { return cntms(ms, data.x, data.len); }

void   addtums(int dst, int src);    /* add multiplicities */
void   unionms(int dst, int src);    /* max multiplicity */
void   intxnms(int dst, int src);    /* min multiplicity */
void   diffms(int dst, int src);     /* subtract multiplicity */
void   symmdiffms(int dst, int src); /* absolute multiplicity difference */
bool   submultisets(int a, int b);
/* Approximate submultiset: every key in a may exceed b by at most
   deviat per key and excess in total; -1 means unbounded. */
bool   simsubmss(int a, int b, int deviat, int excess);
/* Cartesian product of two multisets.  Each result key is four uvlongs:
   {alen, aptr, blen, bptr}; multiplicity is count(a) * count(b).
   Pointers reference the source multisets' copied key storage and inherit
   their lifetime/mutation restrictions. */
int    cartprodms(int arena, int a, int b);
void   rmmultiset(int ms);

/* ── Set operations ──────────────────────────────────── */
/* Union: add all elements from src into dst */
void   unions(int dst, int src);
/* Intersection: keep only elements present in both */
void   intxns(int dst, int src);
/* Difference: remove elements from dst that also appear in src */
void   diffs(int dst, int src);
/* Symmetric difference: dst = (dst \ src) ∪ (src \ dst) */
void   symmdiffs(int dst, int src);
/* Check if seta is a subset of setb */
bool   subsets(int seta, int setb);

/* ── Common silo functions ───────────────────────────── */
/* Reserve capacity for at least 'size' elements (linear silos
   only; hash tables grow automatically and ignore this call). */
void   efflate(int desc, uvlong size);
/* Shrink to fit current element count (linear silos realloc to
   len; hash tables compact to remove tombstones). */
void   tamp(int desc);
/* Create a shallow copy of the silo in the given arena.  Linear uvlong data
   and hash key/value bytes are copied; product-pair pointer values remain
   shallow.  Returns the new descriptor, or -1 on failure. */
int    replica(int arena, int desc);
/* Swap an element with the silo:
   Stack — swap with top element
   Sequence — swap with last element
   Queue — enqueue and dequeue (atomically exchange one)
   Set — swap with an arbitrary element
   Map — swap with an arbitrary key/value pair
   *data must point to a buffer matching the silo's element type
   (uvlong for linear, key bytes for set, key+val for map). */
void   swop(int desc, void *data);
/* Batch-add n elements to a linear silo (stack/queue/seq).
   For hash containers use adds()/insert() individually. */
void   cram(int desc, uvlong *data, uvlong n);
/* Batch-remove up to n elements from a linear silo into buf.
   Returns the actual number of elements removed.
   For hash containers this picks arbitrary elements. */
uvlong spew(int desc, uvlong *buf, uvlong n);

/* ── Silo type enumeration ───────────────────────────── */
typedef enum silotype
{
	silo_stack = 1,
	silo_seq,
	silo_queue,
	silo_set,
	silo_map,
	silo_multiset,
} silotype;

/* Return the type of the silo, or 0 if invalid. */
int  silotype_of(int desc);

/* ── Iterators ────────────────────────────────────────── */
/* Create a read-only iterator for the silo.  Returns iterator
   descriptor or -1 on failure. */
int  mkiter(int desc);
/* Create an obliterating iterator (removes each element as
   it is yielded).  Returns iterator descriptor or -1. */
int  mkoter(int desc);
/* Get the next element from the iterator.
   For linear silos (stack/queue/seq): writes a uvlong into *data.
   For sets: writes {klen, key_ptr} as two uvlongs into data[0..1].
   For maps: writes {klen, key_ptr, vlen, val_ptr} into data[0..3].
   For multisets: writes {klen, key_ptr, count} into data[0..2].
   Pointers reference the silo's internal storage and are valid until the
   silo is next mutated, compacted, destroyed, or its arena is destroyed.
   Returns true if an element was obtained, false when exhausted. */
bool next(int iter, void *data);
/* Destroy an iterator.  For obliterating iterators, any
   remaining elements are left in the silo. */
void rmiter(int iter);
