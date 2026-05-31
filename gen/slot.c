#include "slot.h"

#include "dispatch_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_type(cwinrt_raw_db const *raw, uint32_t token) {
	uint32_t i;
	for (i = 0; i < raw->type_count; i++)
		if (raw->types [i].token == token) return ( int ) i;
	return -1;
}

static uint32_t coded_to_typedef_token(uint32_t coded) {
	if (!coded || (coded & 3u) != 0) return 0;
	return 0x02000000u | (coded >> 2);
}

/* A "real" method is a named, non-special (".ctor"/".cctor") method declared
   directly on the given type. This filter recurs in every slot pass. */
static bool method_is_real_on(cwinrt_raw_method const *m, uint32_t type_token) {
	if (m->type_token != type_token) return false;
	if (!m->name || m->name [0] == '.') return false;
	return true;
}

static int method_match_on_type(
  cwinrt_raw_db const *raw, uint32_t type_token, char const *name, char const *class_params, bool static_overload
) {
	uint32_t i;
	int      found = -1;

	for (i = 0; i < raw->method_count; i++) {
		cwinrt_raw_method const *im = &raw->methods [i];
		if (im->type_token != type_token || !im->name || !name) continue;
		if (strcmp(im->name, name) != 0) continue;
		if (!static_overload) {
			if (found < 0) found = ( int ) i;
			continue;
		}
		if (cwinrt_params_match_iface_static(class_params, im->params_c)) return ( int ) i;
	}
	return found;
}

static uint32_t iface_flatten(cwinrt_raw_db const *raw, uint32_t iface_token, uint32_t *order, uint32_t cap) {
	cwinrt_raw_type const *t;
	uint32_t               n = 0;
	uint32_t               base_tok;

	if (!iface_token) return 0;
	t = &raw->types [find_type(raw, iface_token)];
	if (!t) return 0;
	base_tok = coded_to_typedef_token(t->extends_token);
	if (base_tok) n = iface_flatten(raw, base_tok, order, cap);
	if (n < cap) order [n++] = iface_token;
	return n;
}

static int assign_iface_slots(cwinrt_raw_db *raw, uint32_t iface_token) {
	uint32_t order [64];
	uint32_t n;
	uint32_t slot = CWINRT_VTBL_SLOT_FIRST;
	uint32_t i;
	uint32_t mi;

	n = iface_flatten(raw, iface_token, order, 64);
	for (i = 0; i < n; i++) {
		uint32_t tok = order [i];
		for (mi = 0; mi < raw->method_count; mi++) {
			cwinrt_raw_method *m = &raw->methods [mi];
			if (!method_is_real_on(m, tok) || m->vtable_slot) continue;
			m->vtable_slot    = slot++;
			m->dispatch_token = tok;
		}
	}
	return 0;
}

/* A classless runtimeclass exposes its instance methods directly, numbered from
   the first interface slot. */
static void assign_class_direct_slots(cwinrt_raw_db *raw, cwinrt_raw_type const *cls) {
	uint32_t slot = CWINRT_VTBL_SLOT_FIRST;
	uint32_t mi;
	for (mi = 0; mi < raw->method_count; mi++) {
		cwinrt_raw_method *m = &raw->methods [mi];
		if (!method_is_real_on(m, cls->token) || m->is_static) continue;
		m->vtable_slot    = slot++;
		m->dispatch_token = cls->token;
	}
}

/* Resolve a class instance method onto the interface that actually declares it
   (searching the class's implemented interfaces, default first), copying that
   interface's per-interface vtable slot + dispatch token. Methods on a
   non-default interface keep their own interface as dispatch_token so the
   emitted wrapper QIs to it (a flat slot on the default interface would call the
   wrong method — these are separate COM vtables). */
/* Transitive closure of a class's interfaces: its directly-implemented
   interfaces (default first) plus every interface those require, recursively.
   A class re-declares methods inherited from required interfaces, so the
   declaring interface of such a method is only reachable transitively. */
static uint32_t collect_class_ifaces(cwinrt_raw_db const *raw, cwinrt_raw_type const *cls, uint32_t *out, uint32_t cap) {
	uint32_t n    = 0;
	uint32_t head = 0;
	uint32_t i;
	for (i = 0; i < cls->iface_count && n < cap; i++) out [n++] = cls->iface_tokens [i];
	while (head < n) {
		uint32_t tok = out [head++];
		int      ti  = find_type(raw, tok);
		if (ti < 0) continue;
		cwinrt_raw_type const *it = &raw->types [ti];
		for (i = 0; i < it->iface_count && n < cap; i++) {
			uint32_t req  = it->iface_tokens [i];
			uint32_t j;
			bool     seen = false;
			for (j = 0; j < n; j++)
				if (out [j] == req) {
					seen = true;
					break;
				}
			if (!seen) out [n++] = req;
		}
	}
	return n;
}

/* Find the interface (in the class's transitive interface set) that declares
   `m`, returning its per-interface slot + token via out params. Prefers an exact
   arg-type match (disambiguates overloads), else the first name match. */
static bool find_declaring_iface(
  cwinrt_raw_db const *raw, cwinrt_raw_method const *m, uint32_t const *ifaces, uint32_t iface_n, uint32_t *out_slot,
  uint32_t *out_tok
) {
	uint32_t k;
	uint32_t im;
	int      name_only     = -1;
	uint32_t name_only_tok = 0;
	for (k = 0; k < iface_n; k++) {
		uint32_t tok = ifaces [k];
		for (im = 0; im < raw->method_count; im++) {
			cwinrt_raw_method const *cand = &raw->methods [im];
			if (cand->type_token != tok || !cand->name || !m->name) continue;
			if (strcmp(cand->name, m->name) != 0 || !cand->vtable_slot) continue;
			if (cwinrt_params_match_instance(m->params_c, cand->params_c)) {
				*out_slot = cand->vtable_slot;
				*out_tok  = tok;
				return true;
			}
			if (name_only < 0) {
				name_only     = ( int ) im;
				name_only_tok = tok;
			}
		}
	}
	if (name_only >= 0) {
		*out_slot = raw->methods [name_only].vtable_slot;
		*out_tok  = name_only_tok;
		return true;
	}
	return false;
}

static int assign_class_slots(cwinrt_raw_db *raw, cwinrt_raw_type const *cls) {
	uint32_t mi;
	uint32_t ifaces [256];
	uint32_t iface_n;
	uint32_t default_tok;

	/* Baseline: number every class method directly (cumulative), dispatch_token =
	 * class. This is the original behavior and guarantees no method is dropped. */
	assign_class_direct_slots(raw, cls);
	if (!cls->iface_count) return 0;

	/* Then OVERRIDE methods that actually live on a NON-default interface with that
	 * interface's per-interface slot + token, so the emitted wrapper QIs to it
	 * (a flat slot on the default-interface pointer would call the wrong method).
	 * Default-interface and unresolved methods keep the baseline (no QI). */
	iface_n     = collect_class_ifaces(raw, cls, ifaces, 256);
	default_tok = cls->iface_tokens [0];
	for (mi = 0; mi < raw->method_count; mi++) {
		cwinrt_raw_method *m = &raw->methods [mi];
		uint32_t           slot;
		uint32_t           tok;
		if (!method_is_real_on(m, cls->token) || m->is_static) continue;
		if (!find_declaring_iface(raw, m, ifaces, iface_n, &slot, &tok)) continue;
		if (tok == default_tok) continue; /* default interface: baseline direct call is correct */
		m->vtable_slot    = slot;
		m->dispatch_token = tok;
	}
	return 0;
}

/* Pre-number every static interface serving this class. */
static void assign_static_serving_ifaces(cwinrt_raw_db *raw, cwinrt_raw_type const *cls) {
	uint32_t ti;
	for (ti = 0; ti < raw->type_count; ti++) {
		cwinrt_raw_type const *st = &raw->types [ti];
		if (st->kind != CWINRT_RAW_IFACE) continue;
		if (!cwinrt_iface_serves_class_static(raw, st->short_name, cls->short_name)) continue;
		assign_iface_slots(raw, st->token);
	}
}

/* Pick the lowest-ranked static interface that already holds an assigned slot
   for this static method overload; report its slot/dispatch via out params. */
static bool find_best_static_slot(
  cwinrt_raw_db *raw, cwinrt_raw_type const *cls, cwinrt_raw_method const *m, uint32_t *out_slot, uint32_t *out_tok
) {
	int      best_im   = -1;
	uint32_t best_tok  = 0;
	int      best_rank = 100;
	uint32_t ti;

	for (ti = 0; ti < raw->type_count; ti++) {
		cwinrt_raw_type const *st = &raw->types [ti];
		int                    im;
		int                    rank;
		if (st->kind != CWINRT_RAW_IFACE) continue;
		if (!cwinrt_iface_serves_class_static(raw, st->short_name, cls->short_name)) continue;
		im = method_match_on_type(raw, st->token, m->name, m->params_c, true);
		if (im < 0 || !raw->methods [im].vtable_slot) continue;
		rank = cwinrt_static_iface_rank(st->short_name, cls->short_name);
		if (best_im >= 0 && rank >= best_rank) continue;
		best_im   = im;
		best_tok  = st->token;
		best_rank = rank;
	}
	if (best_im < 0) return false;
	*out_slot = raw->methods [best_im].vtable_slot;
	*out_tok  = best_tok;
	return true;
}

static int assign_static_slots(cwinrt_raw_db *raw, cwinrt_raw_type const *cls) {
	uint32_t mi;

	assign_static_serving_ifaces(raw, cls);

	for (mi = 0; mi < raw->method_count; mi++) {
		cwinrt_raw_method *m = &raw->methods [mi];
		uint32_t           slot;
		uint32_t           tok;
		if (!method_is_real_on(m, cls->token) || !m->is_static || m->vtable_slot) continue;
		if (find_best_static_slot(raw, cls, m, &slot, &tok)) {
			m->vtable_slot    = slot;
			m->dispatch_token = tok;
		}
	}
	return 0;
}

static uint32_t iface_depth(cwinrt_raw_db const *raw, uint32_t iface_token) {
	uint32_t order [64];
	return iface_flatten(raw, iface_token, order, 64);
}

/* Collect interface tokens, shallowest base chain first, so a base interface's
   methods are numbered before any derived interface reuses them. */
static uint32_t collect_iface_order(cwinrt_raw_db const *raw, uint32_t *iface_order) {
	uint32_t iface_n = 0;
	uint32_t i;
	uint32_t j;

	for (i = 0; i < raw->type_count; i++)
		if (raw->types [i].kind == CWINRT_RAW_IFACE) iface_order [iface_n++] = raw->types [i].token;

	for (i = 0; i + 1 < iface_n; i++) {
		for (j = i + 1; j < iface_n; j++) {
			if (iface_depth(raw, iface_order [j]) >= iface_depth(raw, iface_order [i])) continue;
			uint32_t t      = iface_order [i];
			iface_order [i] = iface_order [j];
			iface_order [j] = t;
		}
	}
	return iface_n;
}

/* Delegates are IUnknown-based COM objects: their single Invoke method sits at
   vtable slot 3 (right after QI/AddRef/Release), NOT slot 6 like IInspectable
   interface methods. */
static void assign_delegate_slots(cwinrt_raw_db *raw, cwinrt_raw_type const *d) {
	uint32_t mi;
	for (mi = 0; mi < raw->method_count; mi++) {
		cwinrt_raw_method *m = &raw->methods [mi];
		if (!method_is_real_on(m, d->token)) continue;
		m->vtable_slot    = 3;
		m->dispatch_token = d->token;
	}
}

int cwinrt_slot_assign(cwinrt_raw_db *raw) {
	uint32_t  ti;
	uint32_t *iface_order;
	uint32_t  iface_n;
	if (!raw) return -1;
	for (ti = 0; ti < raw->method_count; ti++) raw->methods [ti].vtable_slot = 0;

	iface_order = ( uint32_t * ) calloc(raw->type_count, sizeof(uint32_t));
	if (!iface_order) return -1;
	iface_n = collect_iface_order(raw, iface_order);
	for (ti = 0; ti < iface_n; ti++) assign_iface_slots(raw, iface_order [ti]);
	free(iface_order);

	for (ti = 0; ti < raw->type_count; ti++) {
		if (raw->types [ti].kind != CWINRT_RAW_CLASS) continue;
		assign_class_slots(raw, &raw->types [ti]);
		assign_static_slots(raw, &raw->types [ti]);
	}
	for (ti = 0; ti < raw->type_count; ti++) {
		if (raw->types [ti].kind != CWINRT_RAW_DELEGATE) continue;
		assign_delegate_slots(raw, &raw->types [ti]);
	}
	return 0;
}

static int slot_cmp_token(void const *a, void const *b) {
	uint32_t ta = (( cwinrt_raw_method const * ) a)->method_token;
	uint32_t tb = (( cwinrt_raw_method const * ) b)->method_token;
	return (ta > tb) - (ta < tb);
}

static void slot_report_bad(uint32_t *bad, uint32_t *inout_bad) {
	(*bad)++;
	if (inout_bad) (*inout_bad)++;
}

/* Gather an interface's real methods, sorted by metadata method token -- an
   order independent of slot.c's own traversal -- so the expected slot
   (6 + position) is a genuine second source. */
static uint32_t gather_iface_methods(cwinrt_raw_db const *raw, uint32_t type_token, cwinrt_raw_method *ms) {
	uint32_t n = 0;
	uint32_t mi;
	for (mi = 0; mi < raw->method_count; mi++) {
		cwinrt_raw_method const *m = &raw->methods [mi];
		if (!method_is_real_on(m, type_token)) continue;
		ms [n++] = *m;
	}
	qsort(ms, n, sizeof(*ms), slot_cmp_token);
	return n;
}

/* Validate one interface's contiguous slot numbering. Returns violation count
   and accumulates ok/bad counters; -1 on allocation failure. */
static int check_iface(
  cwinrt_raw_db const *raw, cwinrt_raw_type const *t, uint32_t *inout_ok, uint32_t *inout_bad
) {
	cwinrt_raw_method *ms;
	uint32_t           n;
	uint32_t           expected = CWINRT_VTBL_SLOT_FIRST;
	uint32_t           bad      = 0;
	uint32_t           k;

	ms = ( cwinrt_raw_method * ) calloc(raw->method_count ? raw->method_count : 1, sizeof(*ms));
	if (!ms) return -1;
	n = gather_iface_methods(raw, t->token, ms);

	for (k = 0; k < n; k++) {
		bool ok = ms [k].vtable_slot == expected && ms [k].dispatch_token == t->token;
		if (ok) {
			if (inout_ok) (*inout_ok)++;
		}
		else {
			slot_report_bad(&bad, inout_bad);
			fprintf(
			  stderr, "  SLOT %s.%s: got slot=%u disp=0x%06x, expected slot=%u disp=0x%06x\n",
			  t->short_name ? t->short_name : "?", ms [k].name ? ms [k].name : "?", ms [k].vtable_slot,
			  ms [k].dispatch_token, expected, t->token
			);
		}
		expected++;
	}
	free(ms);
	return ( int ) bad;
}

/* Sanity: every runtimeclass instance method gets a real slot (>= 6) and a
   dispatch target (its default interface, or the class token as fallback). */
static uint32_t check_class(
  cwinrt_raw_db const *raw, cwinrt_raw_type const *t, uint32_t *inout_ok, uint32_t *inout_bad
) {
	uint32_t bad = 0;
	uint32_t mi;
	for (mi = 0; mi < raw->method_count; mi++) {
		cwinrt_raw_method const *m = &raw->methods [mi];
		if (!method_is_real_on(m, t->token) || m->is_static) continue;
		if (m->vtable_slot >= CWINRT_VTBL_SLOT_FIRST && m->dispatch_token) {
			if (inout_ok) (*inout_ok)++;
			continue;
		}
		slot_report_bad(&bad, inout_bad);
		fprintf(
		  stderr, "  SLOT class %s.%s: slot=%u disp=0x%06x (expected slot>=6, nonzero dispatch)\n",
		  t->short_name ? t->short_name : "?", m->name ? m->name : "?", m->vtable_slot, m->dispatch_token
		);
	}
	return bad;
}

int cwinrt_slot_check(cwinrt_raw_db const *raw, uint32_t *inout_ok, uint32_t *inout_bad) {
	uint32_t ti;
	uint32_t bad = 0;
	if (!raw) return 0;

	for (ti = 0; ti < raw->type_count; ti++) {
		cwinrt_raw_type const *t = &raw->types [ti];
		int                    r;
		if (t->kind != CWINRT_RAW_IFACE) continue;
		r = check_iface(raw, t, inout_ok, inout_bad);
		if (r < 0) return ( int ) (bad + 1);
		bad += ( uint32_t ) r;
	}

	for (ti = 0; ti < raw->type_count; ti++) {
		cwinrt_raw_type const *t = &raw->types [ti];
		if (t->kind != CWINRT_RAW_CLASS) continue;
		bad += check_class(raw, t, inout_ok, inout_bad);
	}
	return ( int ) bad;
}
