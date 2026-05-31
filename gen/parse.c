#include "parse.h"

#include "method_names.h"
#include "map.h"
#include "name.h"
#include "sig.h"
#include "winmd.h"
#include "winmd_int.h"
#include "arena.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct parse_ns_entry {
	char const *ns;
	uint32_t    ti;
} parse_ns_entry;

static int parse_ns_entry_cmp(void const *a, void const *b) {
	parse_ns_entry const *ea = ( parse_ns_entry const * ) a;
	parse_ns_entry const *eb = ( parse_ns_entry const * ) b;
	if (!ea->ns && !eb->ns) return 0;
	if (!ea->ns) return -1;
	if (!eb->ns) return 1;
	return strcmp(ea->ns, eb->ns);
}

enum
{
	TD_INTERFACE  = 0x00000020,
	TD_SEALED     = 0x00000100,
	TD_VALUE_TYPE = 0x00000008,
};

static cwinrt_raw_kind raw_kind_from_flags(uint32_t flags) {
	/* TypeDef flags cannot distinguish enum from struct: both are sealed value
	   types. Classify value types as STRUCT here; the base-type check (extends
	   System.Enum) in parse_row_typedef upgrades true enums to CWINRT_RAW_ENUM. */
	if (flags & TD_INTERFACE) return CWINRT_RAW_IFACE;
	if (flags & TD_VALUE_TYPE) return CWINRT_RAW_STRUCT;
	return CWINRT_RAW_CLASS;
}

static bool ns_has_prefix(char const *ns, char const *prefix) {
	size_t plen;
	if (!prefix || !*prefix) return true;
	if (!ns) return false;
	plen = strlen(prefix);
	if (strncmp(ns, prefix, plen) != 0) return false;
	return ns [plen] == '\0' || ns [plen] == '.';
}

/* One header per WinRT namespace: exact match on TypeDef namespace column. */
static bool ns_matches(char const *ns, char const *filter) {
	if (!filter || !*filter) return true;
	if (!ns) return false;
	return strcmp(ns, filter) == 0;
}

static char *arena_str(int arena, char const *s) {
	size_t n;
	char  *p;
	if (!s) return NULL;
	n = strlen(s) + 1;
	p = ( char * ) aden(arena, ( uvlong ) n);
	if (!p) return NULL;
	memcpy(p, s, n);
	return p;
}

static uint32_t ns_index_collect_entries(winmd_db const *wm, char const *prefix, parse_ns_entry *entries) {
	uint32_t n = 0;
	uint32_t i;
	for (i = 0; i < wm->typedef_count; i++) {
		char const *ns = wm->typedefs [i].namespace_name;
		if (!ns_has_prefix(ns, prefix ? prefix : "Windows")) continue;
		entries [n].ns = ns;
		entries [n].ti = i;
		n++;
	}
	return n;
}

static uint32_t ns_index_count_namespaces(parse_ns_entry const *entries, uint32_t n) {
	uint32_t ns_i = 0;
	uint32_t i;
	for (i = 1; i < n; i++)
		if (strcmp(entries [i - 1].ns, entries [i].ns) != 0) ns_i++;
	return ns_i + 1;
}

static int ns_index_fill_names(int arena, parse_ns_entry const *entries, uint32_t n, cwinrt_typedef_ns_index *out) {
	uint32_t at = 0;
	uint32_t i;

	out->offsets [0] = 0;
	out->names [0]   = arena_str(arena, entries [0].ns);
	if (!out->names [0]) return -1;
	for (i = 1; i < n; i++) {
		if (strcmp(entries [i - 1].ns, entries [i].ns) == 0) continue;
		at++;
		out->names [at] = arena_str(arena, entries [i].ns);
		if (!out->names [at]) return -1;
		out->offsets [at] = i;
	}
	out->offsets [out->ns_count] = n;
	return 0;
}

int cwinrt_typedef_ns_index_build(winmd_db const *wm, char const *prefix, int arena, cwinrt_typedef_ns_index *out) {
	parse_ns_entry *entries = NULL;
	uint32_t        n       = 0;
	uint32_t        i;

	if (!wm || !out) return -1;
	memset(out, 0, sizeof(*out));
	out->arena = arena;

	entries = ( parse_ns_entry * ) calloc(wm->typedef_count, sizeof(*entries));
	if (!entries && wm->typedef_count) return -1;
	n = ns_index_collect_entries(wm, prefix, entries);
	if (!n) {
		free(entries);
		return 0;
	}
	qsort(entries, n, sizeof(*entries), parse_ns_entry_cmp);

	out->td_total   = n;
	out->td_indices = ( uint32_t * ) aden(arena, n * sizeof(uint32_t));
	if (!out->td_indices) {
		free(entries);
		return -1;
	}
	for (i = 0; i < n; i++) out->td_indices [i] = entries [i].ti;

	out->ns_count = ns_index_count_namespaces(entries, n);
	out->names    = ( char ** ) aden(arena, out->ns_count * sizeof(char *));
	out->offsets  = ( uint32_t * ) aden(arena, (out->ns_count + 1) * sizeof(uint32_t));
	if (!out->names || !out->offsets) {
		free(entries);
		return -1;
	}
	if (ns_index_fill_names(arena, entries, n, out) != 0) {
		free(entries);
		return -1;
	}
	free(entries);
	return 0;
}

int cwinrt_typedef_ns_index_range(
  cwinrt_typedef_ns_index const *td_ix, char const *filter_ns, uint32_t *begin, uint32_t *end
) {
	uint32_t lo;
	uint32_t hi;
	uint32_t mid;
	int      cmp;

	if (!begin || !end) return -1;
	*begin = 0;
	*end   = 0;
	if (!td_ix || !filter_ns || !td_ix->ns_count) return -1;

	lo = 0;
	hi = td_ix->ns_count;
	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		cmp = strcmp(td_ix->names [mid], filter_ns);
		if (cmp < 0) lo = mid + 1;
		else hi = mid;
	}
	if (lo >= td_ix->ns_count || strcmp(td_ix->names [lo], filter_ns) != 0) return -1;
	*begin = td_ix->offsets [lo];
	*end   = td_ix->offsets [lo + 1];
	return 0;
}

void cwinrt_typedef_ns_index_free(cwinrt_typedef_ns_index *td_ix) {
	if (!td_ix) return;
	memset(td_ix, 0, sizeof(*td_ix));
}

static char **arena_param_names(int arena, char **src, uint32_t n) {
	uint32_t i;
	char   **out;

	if (!n) return NULL;
	out = ( char ** ) aden(arena, n * sizeof(char *));
	if (!out) return NULL;
	for (i = 0; i < n; i++) {
		out [i] = arena_str(arena, src [i]);
		if (!out [i]) return NULL;
	}
	return out;
}

static uint32_t token_from_typedef_row(uint32_t row1) { return 0x02000000u | row1; }

static uint32_t row_from_typedef_token(uint32_t token) { return token & 0x00ffffffu; }

static int      find_type_index(cwinrt_raw_type const *types, uint32_t count, uint32_t token) {
	uint32_t i;
	for (i = 0; i < count; i++)
		if (types [i].token == token) return ( int ) i;
	return -1;
}

static int extern_add(int arena, cwinrt_raw_db *db, char const *full) {
	uint32_t i;
	char   **nlist;
	if (!full || !*full) return 0;
	for (i = 0; i < db->extern_type_count; i++)
		if (strcmp(db->extern_types [i], full) == 0) return 0;
	nlist = ( char ** ) aden(arena, (db->extern_type_count + 1) * sizeof(char *));
	if (!nlist) return -1;
	if (db->extern_type_count) memcpy(nlist, db->extern_types, db->extern_type_count * sizeof(char *));
	nlist [db->extern_type_count] = arena_str(arena, full);
	if (!nlist [db->extern_type_count]) return -1;
	db->extern_types = nlist;
	db->extern_type_count++;
	return 0;
}

/* True when the type's own namespace equals filter_ns (i.e. it is local, not an
   extern reference). The namespace is full up to the last '.', else the whole name. */
static bool type_ns_matches_filter(char const *full, char const *filter_ns) {
	char const *dot = strrchr(full, '.');
	char        type_ns [128];
	size_t      n;

	if (!dot || dot <= full) return ns_matches(full, filter_ns);
	n = ( size_t ) (dot - full);
	if (n >= sizeof(type_ns)) n = sizeof(type_ns) - 1;
	memcpy(type_ns, full, n);
	type_ns [n] = '\0';
	return ns_matches(type_ns, filter_ns);
}

/* Scalars needed to resolve a coded type token and decide whether it names an
   out-of-namespace Windows type that must be registered as an extern. */
typedef struct ref_ctx {
	int                    arena;
	cwinrt_raw_db         *db;
	winmd_meta const      *meta;
	cwinrt_raw_type const *types;
	uint32_t               type_count;
	char const            *filter_ns;
} ref_ctx;

static void note_type_ref_meta(ref_ctx const *cx, uint32_t coded) {
	char     full [256];
	uint32_t tag = coded & 3u;
	uint32_t row = coded >> 2;

	if (!coded || !row || !cx->meta) return;
	if (tag == 0 && find_type_index(cx->types, cx->type_count, token_from_typedef_row(row)) >= 0) return;
	if (winmd_coded_type_full_name(cx->meta, coded, full, sizeof(full)) != 0) return;
	if (type_ns_matches_filter(full, cx->filter_ns)) return;
	if (strncmp(full, "Windows.", 8) == 0) extern_add(cx->arena, cx->db, full);
}

static int ref_push(int arena, cwinrt_raw_type *t, uint32_t coded, cwinrt_raw_type const *types, uint32_t type_count) {
	uint32_t  tag = coded & 3u;
	uint32_t  row = coded >> 2;
	uint32_t  tok;
	uint32_t  i;
	uint32_t *nlist;

	if (!coded || tag != 0 || !row) return 0;
	tok = token_from_typedef_row(row);
	if (find_type_index(types, type_count, tok) < 0) return 0;
	for (i = 0; i < t->ref_count; i++)
		if (t->ref_tokens [i] == tok) return 0;
	nlist = ( uint32_t * ) aden(arena, (t->ref_count + 1) * sizeof(uint32_t));
	if (!nlist) return -1;
	if (t->ref_count) memcpy(nlist, t->ref_tokens, t->ref_count * sizeof(uint32_t));
	nlist [t->ref_count] = tok;
	t->ref_tokens        = nlist;
	t->ref_count++;
	return 0;
}

/* Shared context for the enrich pass: the arena, the database being populated, the
   metadata handle, the namespace filter, and the full type list (for ref lookups). */
typedef struct enrich_ctx {
	int                    arena;
	cwinrt_raw_db         *db;
	winmd_meta const      *m;
	winmd_db const        *wm; /* for resolving TypeRef interface refs to local TypeDefs */
	char const            *filter_ns;
	cwinrt_raw_type const *all;
	uint32_t               all_count;
} enrich_ctx;

static void enrich_note_ref(enrich_ctx const *cx, uint32_t coded) {
	ref_ctx rc = { cx->arena, cx->db, cx->m, cx->all, cx->all_count, cx->filter_ns };
	note_type_ref_meta(&rc, coded);
}

static bool name_ends_with_statics(char const *name) {
	size_t snlen = name ? strlen(name) : 0;
	return snlen >= 7 && strcmp(name + snlen - 7, "Statics") == 0;
}

/* TypeDef flags cannot mark a class activatable directly; infer it for classes
   that are abstract+sealed (0x4100) but not interfaces and not "*Statics". */
static void enrich_activatable(winmd_meta const *m, uint32_t row, cwinrt_raw_type *t) {
	uint32_t f = t->flags;

	t->is_activatable = winmd_typedef_is_activatable(m, row);
	if (t->kind != CWINRT_RAW_CLASS) {
		t->is_activatable = false;
		return;
	}
	if (t->is_activatable) return;
	if ((f & 0x4100u) == 0x4100u && !(f & TD_INTERFACE) && !name_ends_with_statics(t->short_name))
		t->is_activatable = true;
}

static int enrich_interface_impls(enrich_ctx const *cx, uint32_t row, cwinrt_raw_type *t) {
	uint32_t *ifaces  = NULL;
	uint32_t  iface_n = 0;
	uint32_t  fi;

	if (winmd_typedef_interface_impls(cx->m, row, &ifaces, &iface_n) != 0 || !iface_n) return 0;
	t->iface_tokens = ( uint32_t * ) aden(cx->arena, iface_n * sizeof(uint32_t));
	if (!t->iface_tokens) return -1;
	for (fi = 0; fi < iface_n; fi++) {
		uint32_t ic = ifaces [fi];
		uint32_t row1;
		/* InterfaceImpl.Interface is a TypeDefOrRef; in Union metadata a class's
		 * implemented interfaces are referenced via TypeRef (tag 1), not TypeDef.
		 * Resolve both to the local TypeDef row so the class knows its interfaces
		 * (else iface_count stays 0 and class methods fall back to a single flat
		 * vtable, mis-dispatching every non-default-interface method). */
		if (cx->wm && winmd_coded_to_typedef_row(cx->wm, cx->m, ic, &row1) == 0)
			t->iface_tokens [t->iface_count++] = token_from_typedef_row(row1);
		enrich_note_ref(cx, ic);
		ref_push(cx->arena, t, ic, cx->all, cx->all_count);
	}
	winmd_iface_tokens_free(ifaces);
	return 0;
}

static void enrich_field_type_refs(enrich_ctx const *cx, winmd_field_info const *wf, cwinrt_raw_field *rf) {
	/* Register cross-namespace value types referenced by struct fields so their
	   defining header is included (the field uses them by value). The first token
	   is the field's own type (used for topo ordering of struct-contains-struct
	   dependencies within a namespace). */
	uint32_t ftok [8];
	uint32_t fn = 0;
	uint32_t k;
	if (winmd_field_collect_type_tokens(cx->m, wf->sig_blob, ftok, &fn, 8) != 0) return;
	if (fn > 0) rf->type_token = ftok [0];
	for (k = 0; k < fn; k++) enrich_note_ref(cx, ftok [k]);
}

static int enrich_fields(enrich_ctx const *cx, uint32_t row, cwinrt_raw_type *t) {
	winmd_field_info *wfields  = NULL;
	uint32_t          wfield_n = 0;
	uint32_t          ei       = 0;
	uint32_t          fi;

	if (t->kind != CWINRT_RAW_STRUCT && t->kind != CWINRT_RAW_ENUM) return 0;
	if (winmd_typedef_fields(cx->m, row, &wfields, &wfield_n) != 0 || !wfield_n) return 0;
	t->fields = ( cwinrt_raw_field * ) aden(cx->arena, wfield_n * sizeof(cwinrt_raw_field));
	if (!t->fields) return -1;
	memset(t->fields, 0, wfield_n * sizeof(cwinrt_raw_field));
	for (fi = 0; fi < wfield_n; fi++) {
		char              ctype [128];
		cwinrt_raw_field *rf = &t->fields [t->field_count];
		/* An enum's first field is the underlying-type instance field `value__`
		 * (FieldAttributes lacks Static 0x10); it is not an enumerator. Skip it so
		 * it isn't emitted as a bogus member that shifts the auto-increment. */
		if (t->kind == CWINRT_RAW_ENUM && !(wfields [fi].flags & 0x0010u)) continue;
		if (winmd_parse_field_blob(cx->m, wfields [fi].sig_blob, ctype, sizeof(ctype)) != 0)
			snprintf(ctype, sizeof(ctype), "uint32_t");
		enrich_field_type_refs(cx, &wfields [fi], rf);
		rf->name           = arena_str(cx->arena, wfields [fi].name);
		rf->c_type         = arena_str(cx->arena, ctype);
		rf->has_enum_value = wfields [fi].has_const;
		rf->enum_value     = wfields [fi].const_value;
		if (t->kind == CWINRT_RAW_ENUM && !rf->has_enum_value) rf->enum_value = ( int64_t ) ei++;
		t->field_count++;
	}
	winmd_field_info_free(wfields, wfield_n);
	return 0;
}

static int parse_enrich_type(enrich_ctx const *cx, cwinrt_raw_type *t) {
	uint32_t row = row_from_typedef_token(t->token);
	uint32_t coded;
	uint8_t  uuid [16];

	if (winmd_typedef_uuid(cx->m, row, uuid) == 0) {
		memcpy(t->uuid, uuid, 16);
		t->has_uuid = true;
	}
	enrich_activatable(cx->m, row, t);

	coded = winmd_typedef_extends_coded(cx->m, row);
	if ((coded & 3u) == 0 && (coded >> 2)) t->extends_token = token_from_typedef_row(coded >> 2);
	enrich_note_ref(cx, coded);
	ref_push(cx->arena, t, coded, cx->all, cx->all_count);

	if (enrich_interface_impls(cx, row, t) != 0) return -1;
	if (enrich_fields(cx, row, t) != 0) return -1;
	return 0;
}

/* raw_kind_from_flags cannot tell delegate/enum from class/struct; refine via the
   base type: extends MulticastDelegate => delegate, extends System.Enum => enum. */
static void refine_kind_from_base(winmd_db const *wm, cwinrt_raw_type *row) {
	uint32_t trow;
	uint32_t ex;
	char     extn [256];

	if ((row->kind != CWINRT_RAW_CLASS && row->kind != CWINRT_RAW_STRUCT) || !wm->meta) return;
	trow = row->token & 0x00ffffffu;
	ex   = winmd_typedef_extends_coded(wm->meta, trow);
	if (winmd_coded_type_full_name(wm->meta, ex, extn, sizeof(extn)) != 0) return;
	if (strstr(extn, "MulticastDelegate") != NULL) row->kind = CWINRT_RAW_DELEGATE;
	else if (strcmp(extn, "System.Enum") == 0) row->kind = CWINRT_RAW_ENUM;
}

static int parse_copy_type_row(int arena, winmd_db const *wm, winmd_row_typedef const *td, cwinrt_raw_type *row) {
	size_t full_len;
	char  *full;

	full_len = strlen(td->namespace_name) + 1 + strlen(td->name) + 1;
	full     = ( char * ) aden(arena, ( uvlong ) full_len);
	if (!full) return -1;
	if (td->namespace_name [0]) snprintf(full, full_len, "%s.%s", td->namespace_name, td->name);
	else snprintf(full, full_len, "%s", td->name);

	row->token = td->token;
	row->flags = td->flags;
	row->kind  = raw_kind_from_flags(td->flags);
	refine_kind_from_base(wm, row);
	row->full_name  = full;
	row->ns         = arena_str(arena, td->namespace_name);
	row->short_name = arena_str(arena, td->name);
	return 0;
}

/* Shared context for the type-copy pass: the destination buffer and its capacity. */
typedef struct copy_ctx {
	int              arena;
	winmd_db const  *wm;
	cwinrt_raw_type *types;
	uint32_t         cap;
} copy_ctx;

static int copy_types_indexed(copy_ctx const *cx, cwinrt_typedef_ns_index const *td_ix, uint32_t begin, uint32_t end, uint32_t *out_n) {
	uint32_t n = 0;
	uint32_t i;
	for (i = begin; i < end; i++) {
		winmd_row_typedef const *td = &cx->wm->typedefs [td_ix->td_indices [i]];
		if (n >= cx->cap) return -1;
		if (parse_copy_type_row(cx->arena, cx->wm, td, &cx->types [n]) != 0) return -1;
		n++;
	}
	*out_n = n;
	return 0;
}

static int copy_types_scan(copy_ctx const *cx, char const *filter_ns, uint32_t *out_n) {
	uint32_t n = 0;
	uint32_t i;
	for (i = 0; i < cx->wm->typedef_count; i++) {
		winmd_row_typedef const *td = &cx->wm->typedefs [i];
		if (!ns_matches(td->namespace_name, filter_ns)) continue;
		if (n >= cx->cap) return -1;
		if (parse_copy_type_row(cx->arena, cx->wm, td, &cx->types [n]) != 0) return -1;
		n++;
	}
	*out_n = n;
	return 0;
}

static int parse_copy_types(
  int arena, winmd_db const *wm, cwinrt_typedef_ns_index const *td_ix, char const *filter_ns, cwinrt_raw_db *out
) {
	copy_ctx cx;
	uint32_t n     = 0;
	uint32_t begin = 0;
	uint32_t end   = 0;
	bool     use_index;
	int      rc;

	cx.arena = arena;
	cx.wm    = wm;
	if (td_ix && cwinrt_typedef_ns_index_range(td_ix, filter_ns, &begin, &end) == 0)
		cx.cap = (end > begin) ? (end - begin) : 64u;
	else cx.cap = wm->typedef_count ? wm->typedef_count : 64u;
	cx.types = ( cwinrt_raw_type * ) aden(arena, cx.cap * sizeof(cwinrt_raw_type));
	if (!cx.types) return -1;
	memset(cx.types, 0, cx.cap * sizeof(cwinrt_raw_type));

	use_index = td_ix && end > begin;
	if (use_index) rc = copy_types_indexed(&cx, td_ix, begin, end, &n);
	else rc = copy_types_scan(&cx, filter_ns, &n);
	if (rc != 0) return -1;

	out->types      = cx.types;
	out->type_count = n;
	return 0;
}

/* Shared context for the method-copy pass. */
typedef struct method_ctx {
	int              arena;
	winmd_db        *wm;
	cwinrt_raw_db   *db;
	cwinrt_raw_type *types;
	uint32_t         type_count;
} method_ctx;

static void method_collect_type_refs(method_ctx const *cx, winmd_row_method const *md, cwinrt_raw_type *rt) {
	winmd_meta const *m = cx->wm->meta;
	ref_ctx           rc = { cx->arena, cx->db, m, cx->types, cx->type_count, cx->db->filter_ns };
	uint32_t          coded_buf [64];
	uint32_t          coded_n = 0;
	uint32_t          ci;

	if (!md->sig_blob) return;
	winmd_sig_collect_type_tokens(m, md->sig_blob, coded_buf, &coded_n, 64);
	for (ci = 0; ci < coded_n; ci++) {
		note_type_ref_meta(&rc, coded_buf [ci]);
		ref_push(cx->arena, rt, coded_buf [ci], cx->types, cx->type_count);
	}
}

static void method_apply_sig(method_ctx const *cx, winmd_method_sig const *sig, cwinrt_raw_method *out) {
	if (sig->params_c) {
		char *pc = arena_str(cx->arena, sig->params_c);
		if (pc) {
			cwinrt_fixup_params_c(pc);
			out->params_c = pc;
		}
	}
	if (sig->ret_c_type) out->ret_c_type = arena_str(cx->arena, sig->ret_c_type);
}

static void method_fill_signature(method_ctx const *cx, winmd_row_method const *md, cwinrt_raw_type const *owner, char const *mname, cwinrt_raw_method *out) {
	winmd_meta const *m          = cx->wm->meta;
	char              self_c [96];
	winmd_method_sig  sig;
	char            **pnames     = NULL;
	uint32_t          pname_n     = 0;
	uint32_t          method_row  = md->token & 0x00ffffffu;
	char              ovl [256];

	if (winmd_method_overload_name(m, method_row, ovl, sizeof(ovl)) == 0)
		out->overload_name = arena_str(cx->arena, ovl);
	out->is_default_overload = winmd_method_is_default_overload(m, method_row);
	cwinrt_name_winrt_to_c(owner->full_name, self_c, sizeof(self_c));
	if (winmd_method_param_names(m, method_row, &pnames, &pname_n) != 0) pname_n = 0;
	if (pname_n && pnames) out->param_names = arena_param_names(cx->arena, pnames, pname_n);
	out->param_name_count = out->param_names ? pname_n : 0;

	memset(&sig, 0, sizeof(sig));
	{
		winmd_method_info info  = {md->flags, self_c, mname};
		winmd_param_names pn_arg = {( char const *const * ) pnames, pname_n};
		if (md->sig_blob && winmd_parse_sig(cx->wm, md->sig_blob, &info, &pn_arg, &sig) == 0) {
			method_apply_sig(cx, &sig, out);
			winmd_sig_free(&sig);
		}
	}
	winmd_param_names_free(pnames, pname_n);
}

static void method_fill(method_ctx const *cx, winmd_row_method const *md, uint32_t ti, char const *mname, cwinrt_raw_method *out) {
	out->type_token          = md->typedef_token;
	out->method_token        = md->token;
	out->name                = arena_str(cx->arena, mname);
	out->overload_name       = NULL;
	out->param_names         = NULL;
	out->param_name_count    = 0;
	out->is_default_overload = false;
	out->c_name              = NULL;
	out->is_static           = (md->flags & 0x0010) != 0;
	out->params_c            = NULL;
	out->ret_c_type          = NULL;
	method_fill_signature(cx, md, &cx->types [ti], mname, out);
	method_collect_type_refs(cx, md, &cx->types [ti]);
}

static int method_find_owner(cwinrt_raw_type const *types, uint32_t type_count, uint32_t token, uint32_t *out_ti) {
	uint32_t ti;
	for (ti = 0; ti < type_count; ti++)
		if (types [ti].token == token) {
			*out_ti = ti;
			return 0;
		}
	return -1;
}

static int parse_copy_methods(method_ctx const *cx, cwinrt_raw_method **out_methods, uint32_t *out_count) {
	winmd_db          *wm = cx->wm;
	uint32_t           n = 0;
	uint32_t           i;
	uint32_t           cap = wm->method_count ? wm->method_count : 64u;
	cwinrt_raw_method *methods;

	methods = ( cwinrt_raw_method * ) aden(cx->arena, cap * sizeof(cwinrt_raw_method));
	if (!methods) return -1;

	for (i = 0; i < wm->method_count; i++) {
		winmd_row_method const *md = &wm->methods [i];
		char const             *mname = md->name;
		uint32_t                ti;

		if (!mname || mname [0] == '.') continue;
		if (method_find_owner(cx->types, cx->type_count, md->typedef_token, &ti) != 0) continue;
		if (n >= cap) return -1;
		method_fill(cx, md, ti, mname, &methods [n]);
		n++;
	}
	*out_methods = methods;
	*out_count   = n;
	return 0;
}

static int parse_enrich_all(winmd_db const *wm, char const *filter_ns, cwinrt_raw_db *out) {
	enrich_ctx cx = { out->arena, out, wm->meta, wm, filter_ns, out->types, out->type_count };
	uint32_t   i;
	for (i = 0; i < out->type_count; i++)
		if (parse_enrich_type(&cx, &out->types [i]) != 0) return -1;
	return 0;
}

int cwinrt_parse_winmd_db(
  winmd_db const *wm, cwinrt_typedef_ns_index const *td_ix, char const *filter_ns, cwinrt_raw_db *out
) {
	method_ctx mcx;

	if (!wm || !out) return -1;
	memset(out, 0, sizeof(*out));

	out->arena = mkarena();
	if (out->arena < 0) {
		errmsg("parse mkarena");
		return -1;
	}
	if (filter_ns) out->filter_ns = arena_str(out->arena, filter_ns);

	if (parse_copy_types(out->arena, wm, td_ix, filter_ns, out) != 0) {
		cwinrt_raw_db_free(out);
		return -1;
	}
	if (parse_enrich_all(wm, filter_ns, out) != 0) {
		cwinrt_raw_db_free(out);
		return -1;
	}
	mcx.arena      = out->arena;
	mcx.wm         = ( winmd_db * ) wm;
	mcx.db         = out;
	mcx.types      = out->types;
	mcx.type_count = out->type_count;
	if (parse_copy_methods(&mcx, &out->methods, &out->method_count) != 0) {
		cwinrt_raw_db_free(out);
		return -1;
	}
	cwinrt_assign_method_names(out);
	return 0;
}

int cwinrt_parse_winmd(char const *path, char const *filter_ns, cwinrt_raw_db *out) {
	winmd_db wm;
	int      rc;

	if (!path || !out) return -1;
	memset(&wm, 0, sizeof(wm));
	if (winmd_open(path, &wm) != 0) {
		efail();
		return -1;
	}
	rc = cwinrt_parse_winmd_db(&wm, NULL, filter_ns, out);
	winmd_close(&wm);
	return rc;
}

void cwinrt_raw_db_free(cwinrt_raw_db *db) {
	if (!db) return;
	if (db->arena > 0) rmarena(db->arena);
	memset(db, 0, sizeof(*db));
}
