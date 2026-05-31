#include "map.h"

#include "dispatch_util.h"
#include "method_names.h"
#include "name.h"
#include "arena.h"
#include "err.h"
#include <stdio.h>
#include <string.h>

static char *map_strdup(int arena, char const *s) {
	size_t n;
	char  *p;
	if (!s) return NULL;
	n = strlen(s) + 1;
	p = ( char * ) aden(arena, n);
	if (!p) return NULL;
	memcpy(p, s, n);
	return p;
}

static char *map_format_sig(int arena, char const *c_method, char const *params_c) {
	char  *sig;
	size_t len;

	len = strlen(c_method) + (params_c ? strlen(params_c) : 0) + 32;
	sig = ( char * ) aden(arena, len + 1);
	if (!sig) return NULL;
	if (params_c && params_c [0]) snprintf(sig, len + 1, "HRESULT %s(%s)", c_method, params_c);
	else snprintf(sig, len + 1, "HRESULT %s(void)", c_method);
	return sig;
}

static uint32_t map_find_type_index(cwinrt_raw_db const *raw, uint32_t type_token) {
	uint32_t i;
	for (i = 0; i < raw->type_count; i++)
		if (raw->types [i].token == type_token) return i;
	return UINT32_MAX;
}

static cwinrt_mapped_kind map_kind(cwinrt_raw_kind k) {
	switch (k) {
	case CWINRT_RAW_IFACE    : return CWINRT_MAP_IFACE;
	case CWINRT_RAW_CLASS    : return CWINRT_MAP_CLASS;
	case CWINRT_RAW_STRUCT   : return CWINRT_MAP_STRUCT;
	case CWINRT_RAW_ENUM     : return CWINRT_MAP_ENUM;
	case CWINRT_RAW_DELEGATE : return CWINRT_MAP_IFACE;
	default                  : return CWINRT_MAP_IFACE;
	}
}

static int map_add_include(int arena, cwinrt_mapped_unit *out, char const *inc) {
	uint32_t i;
	char   **nlist;
	for (i = 0; i < out->include_count; i++)
		if (strcmp(out->includes [i], inc) == 0) return 0;
	nlist = ( char ** ) aden(arena, (out->include_count + 1) * sizeof(char *));
	if (!nlist) return -1;
	if (out->include_count) memcpy(nlist, out->includes, out->include_count * sizeof(char *));
	nlist [out->include_count] = map_strdup(arena, inc);
	if (!nlist [out->include_count]) return -1;
	out->includes = nlist;
	out->include_count++;
	return 0;
}

static bool map_has_include(cwinrt_mapped_unit const *out, char const *inc) {
	uint32_t i;
	for (i = 0; i < out->include_count; i++)
		if (strcmp(out->includes [i], inc) == 0) return true;
	return false;
}

static int map_add_include_for_winrt_ns(int arena, cwinrt_mapped_unit *out, char const *winrt_ns) {
	char inc [256];
	if (!winrt_ns || !winrt_ns [0]) return 0;
	cwinrt_name_include_for_ns(winrt_ns, inc, sizeof(inc));
	return map_add_include(arena, out, inc);
}

/* C typedef names that are enums/structs defined in another namespace header (not opaque iface). */
static bool map_cname_use_defining_header(char const *cname) {
	if (!cname || !cname [0]) return false;
	if (strcmp(cname, "WUI_Color") == 0 || strcmp(cname, "WUI_WindowId") == 0) return true;
	if (strncmp(cname, "WF_", 3) == 0 || strncmp(cname, "WFN_", 4) == 0) return true;
	if (strcmp(cname, "WG_SizeInt32") == 0
		|| strcmp(cname, "WG_PointInt32") == 0
		|| strcmp(cname, "WG_RectInt32") == 0
		|| strcmp(cname, "WG_DisplayId") == 0
		|| strcmp(cname, "WG_DisplayAdapterId") == 0)
		return true;
	return false;
}

static bool map_ident_char(char c);

/* True if `cname` appears as a standalone token in `s` that is NOT immediately
 * followed (skipping spaces) by '*' -- i.e. used by value rather than by pointer. */
static bool map_token_by_value_in(char const *s, char const *cname, size_t clen) {
	char const *p = s;
	if (!s || !cname || !clen) return false;
	while ((p = strstr(p, cname)) != NULL) {
		char const *after = p + clen;
		bool        lb    = (p == s) || !map_ident_char(p [-1]);
		bool        rb    = !map_ident_char(*after);
		if (lb && rb) {
			char const *q = after;
			while (*q == ' ') q++;
			if (*q != '*') return true; /* by value */
		}
		p = after;
	}
	return false;
}

/* True if a cross-namespace value type `cname` is consumed by value anywhere in
 * the unit (method params or struct fields). Such types need their real (enum/
 * struct) definition, so the defining header must be included rather than an
 * opaque `typedef struct` forward declaration emitted. */
static bool map_type_uses_cname_by_value(cwinrt_mapped_type const *t, char const *cname, size_t clen) {
	uint32_t mi, fi;
	for (mi = 0; mi < t->method_count; mi++)
		if (map_token_by_value_in(t->methods [mi].params_c, cname, clen)) return true;
	for (fi = 0; fi < t->field_count; fi++) {
		char const *ft = t->fields [fi].c_type;
		if (ft && strcmp(ft, cname) == 0) return true; /* struct field: by value */
	}
	return false;
}

static bool map_cname_used_by_value(cwinrt_mapped_unit const *out, char const *cname) {
	size_t   clen = cname ? strlen(cname) : 0;
	uint32_t ti;
	if (!out || !clen) return false;
	for (ti = 0; ti < out->type_count; ti++)
		if (map_type_uses_cname_by_value(&out->types [ti], cname, clen)) return true;
	return false;
}

static bool map_cname_looks_opaque_iface(char const *cname) {
	char const *p;
	if (!cname) return false;
	for (p = cname; (p = strchr(p, '_')) != NULL; p++) {
		p++;
		if (p [0] == 'I' && p [1] >= 'A' && p [1] <= 'Z') return true;
	}
	return false;
}

typedef struct {
	char const *token;
	char const *winrt_ns;
} map_cross_ns;

static const map_cross_ns map_cross_ns_table [] = {
  {"WUI_Color",    "Windows.UI"},
  {"WUI_WindowId", "Windows.UI"},
  {NULL,           NULL        }
};

static void map_mark_cross_ns_tokens(char const *params, bool *need) {
	uint32_t ci;
	if (!params) return;
	for (ci = 0; map_cross_ns_table [ci].token; ci++)
		if (strstr(params, map_cross_ns_table [ci].token)) need [ci] = true;
}

static void map_scan_cross_ns_tokens(cwinrt_mapped_unit const *out, bool *need) {
	uint32_t ti;
	uint32_t mi;
	for (ti = 0; ti < out->type_count; ti++)
		for (mi = 0; mi < out->types [ti].method_count; mi++)
			map_mark_cross_ns_tokens(out->types [ti].methods [mi].params_c, need);
}

static int map_add_includes_from_signature_tokens(int arena, cwinrt_raw_db const *raw, cwinrt_mapped_unit *out) {
	uint32_t ci;
	bool     need [8];
	memset(need, 0, sizeof(need));
	map_scan_cross_ns_tokens(out, need);
	for (ci = 0; map_cross_ns_table [ci].token; ci++) {
		if (!need [ci] || !raw->filter_ns || strcmp(raw->filter_ns, map_cross_ns_table [ci].winrt_ns) == 0) continue;
		if (map_add_include_for_winrt_ns(arena, out, map_cross_ns_table [ci].winrt_ns) != 0) return -1;
	}
	return 0;
}

static bool map_ident_char(char c) {
	return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

void       cwinrt_fixup_params_c(char *params) { ( void ) params; }

static int map_add_extern_forward(int arena, cwinrt_mapped_unit *out, char const *cname) {
	uint32_t i;
	char   **nlist;
	for (i = 0; i < out->extern_forward_count; i++)
		if (strcmp(out->extern_forwards [i], cname) == 0) return 0;
	nlist = ( char ** ) aden(arena, (out->extern_forward_count + 1) * sizeof(char *));
	if (!nlist) return -1;
	if (out->extern_forward_count) memcpy(nlist, out->extern_forwards, out->extern_forward_count * sizeof(char *));
	nlist [out->extern_forward_count] = map_strdup(arena, cname);
	if (!nlist [out->extern_forward_count]) return -1;
	out->extern_forwards = nlist;
	out->extern_forward_count++;
	return 0;
}

/* Bundles the inputs threaded through every mapping phase. */
typedef struct {
	cwinrt_mapped_unit  *out;
	cwinrt_raw_db const *raw;
	char const          *ns_prefix;
} map_ctx;

/* Map one raw type into out->types [ti] (names/uuid/fields/enum-members). */
static int map_one_type(map_ctx const *c, uint32_t ti, uint32_t ri) {
	cwinrt_mapped_unit    *out       = c->out;
	char const            *ns_prefix = c->ns_prefix;
	cwinrt_raw_type const *rt        = &c->raw->types [ri];
	cwinrt_mapped_type    *mt        = &out->types [ti];
	uint32_t               fi;

	mt->winrt_name = map_strdup(out->arena, rt->full_name);
	mt->c_typedef  = cwinrt_name_type(ns_prefix, rt->ns, rt->short_name, out->arena);
	mt->c_prefix   = map_strdup(out->arena, ns_prefix);
	mt->kind       = map_kind(rt->kind);
	if (rt->has_uuid) {
		memcpy(mt->uuid, rt->uuid, 16);
		mt->has_uuid = true;
	}
	mt->topo_id        = ri;
	mt->is_activatable = rt->is_activatable && rt->kind == CWINRT_RAW_CLASS;
	if (rt->short_name && strchr(rt->short_name, '`')) mt->is_activatable = false;
	mt->activate_c_name = NULL;
	if (mt->is_activatable)
		mt->activate_c_name = cwinrt_name_class_new(ns_prefix, rt->ns, rt->short_name, out->arena);
	mt->methods      = NULL;
	mt->method_count = 0;
	if (!rt->field_count) return 0;

	mt->fields = ( cwinrt_mapped_field * ) aden(out->arena, rt->field_count * sizeof(cwinrt_mapped_field));
	if (!mt->fields) return -1;
	for (fi = 0; fi < rt->field_count; fi++) {
		mt->fields [fi].c_type = map_strdup(out->arena, rt->fields [fi].c_type);
		mt->fields [fi].name   = map_strdup(out->arena, rt->fields [fi].name);
	}
	mt->field_count = rt->field_count;
	if (rt->kind != CWINRT_RAW_ENUM) return 0;

	mt->enum_members = ( cwinrt_mapped_enum_member * ) aden(
	  out->arena, rt->field_count * sizeof(cwinrt_mapped_enum_member)
	);
	if (!mt->enum_members) return -1;
	for (fi = 0; fi < rt->field_count; fi++) {
		mt->enum_members [fi].name  = map_strdup(out->arena, rt->fields [fi].name);
		mt->enum_members [fi].value = rt->fields [fi].enum_value;
	}
	mt->enum_member_count = rt->field_count;
	return 0;
}

/* Locate the mapped slot whose topo_id == raw_idx, or UINT32_MAX. */
static uint32_t map_slot_for_topo(cwinrt_mapped_unit const *out, uint32_t topo_id) {
	uint32_t slot;
	for (slot = 0; slot < out->type_count; slot++)
		if (out->types [slot].topo_id == topo_id) return slot;
	return UINT32_MAX;
}

/* Tally per-type method counts from the flat raw method list. */
static void map_count_methods(map_ctx const *c, uint32_t *method_counts) {
	cwinrt_raw_db const *raw = c->raw;
	uint32_t             mi;
	for (mi = 0; mi < raw->method_count; mi++) {
		uint32_t idx  = map_find_type_index(raw, raw->methods [mi].type_token);
		uint32_t slot = (idx == UINT32_MAX) ? UINT32_MAX : map_slot_for_topo(c->out, idx);
		if (slot != UINT32_MAX) method_counts [slot]++;
	}
}

/* Allocate each type's method array from its tallied count. */
static int map_alloc_methods(cwinrt_mapped_unit *out, uint32_t const *method_counts) {
	uint32_t ti;
	for (ti = 0; ti < out->type_count; ti++) {
		if (!method_counts [ti]) continue;
		out->types [ti].methods
		  = ( cwinrt_mapped_method * ) aden(out->arena, method_counts [ti] * sizeof(cwinrt_mapped_method));
		if (!out->types [ti].methods) {
			errmsg("map methods OOM");
			return -1;
		}
		out->types [ti].method_count = method_counts [ti];
	}
	return 0;
}

/* True if raw method `im` matches `rm` by name and static-param signature. */
static bool map_iface_method_matches(cwinrt_raw_db const *raw, uint32_t im, uint32_t iface_token, cwinrt_raw_method const *rm) {
	if (raw->methods [im].type_token != iface_token
		|| !raw->methods [im].name
		|| !rm->name
		|| strcmp(raw->methods [im].name, rm->name) != 0)
		return false;
	return cwinrt_params_match_iface_static(rm->params_c, raw->methods [im].params_c);
}

/* Search ifaces serving `cls` for the one declaring this static method.
   Preserves the original scan order and match conditions exactly. */
static uint32_t map_find_serving_iface(cwinrt_raw_db const *raw, uint32_t raw_idx, cwinrt_raw_method const *rm) {
	cwinrt_raw_type const *cls = &raw->types [raw_idx];
	uint32_t               ti2;
	uint32_t               im;
	for (ti2 = 0; ti2 < raw->type_count; ti2++) {
		cwinrt_raw_type const *st = &raw->types [ti2];
		if (st->kind != CWINRT_RAW_IFACE) continue;
		if (!cwinrt_iface_serves_class_static(raw, st->short_name, cls->short_name)) continue;
		for (im = 0; im < raw->method_count; im++) {
			if (!map_iface_method_matches(raw, im, st->token, rm)) continue;
			return ti2;
		}
	}
	return UINT32_MAX;
}

static void map_set_delegate_c_name(map_ctx const *c, uint32_t iface_raw, cwinrt_raw_method const *rm, cwinrt_mapped_method *mm) {
	cwinrt_mapped_unit  *out = c->out;
	cwinrt_raw_db const *raw = c->raw;
	uint32_t             im;
	for (im = 0; im < raw->method_count; im++) {
		if (!map_iface_method_matches(raw, im, raw->types [iface_raw].token, rm)) continue;
		if (raw->methods [im].c_name)
			mm->delegate_c_name = map_strdup(out->arena, raw->methods [im].c_name);
		else
			mm->delegate_c_name
			  = cwinrt_name_method(c->ns_prefix, raw->types [iface_raw].short_name, rm->name, out->arena);
		return;
	}
}

/* Resolve a static class method to its serving iface, delegate name, and dispatch iface. */
static void map_resolve_static_dispatch(
  map_ctx const *c, uint32_t raw_idx, cwinrt_raw_method const *rm, cwinrt_mapped_type const *mt, cwinrt_mapped_method *mm
) {
	cwinrt_mapped_unit *out       = c->out;
	uint32_t            iface_raw = UINT32_MAX;
	uint32_t            slot_j;

	mm->static_class_winrt = map_strdup(out->arena, mt->winrt_name);
	if (rm->dispatch_token) iface_raw = map_find_type_index(c->raw, rm->dispatch_token);
	if (iface_raw == UINT32_MAX) iface_raw = map_find_serving_iface(c->raw, raw_idx, rm);
	if (iface_raw == UINT32_MAX) return;

	map_set_delegate_c_name(c, iface_raw, rm, mm);
	slot_j = map_slot_for_topo(out, iface_raw);
	if (slot_j != UINT32_MAX) {
		mm->dispatch_iface_c = map_strdup(out->arena, out->types [slot_j].c_typedef);
		mm->dispatch_has_iid = out->types [slot_j].has_uuid;
	}
}

/* For a class instance method that dispatches through a NON-default interface,
   record that interface so the emitted wrapper QIs to it before calling. Methods
   on the default interface leave dispatch_iface_c NULL (called directly on the
   class's default-interface pointer, as before). */
static void map_resolve_instance_dispatch(
  map_ctx const *c, uint32_t raw_idx, cwinrt_raw_method const *rm, cwinrt_mapped_method *mm
) {
	cwinrt_raw_type const *rt = &c->raw->types [raw_idx];
	uint32_t               iface_raw;
	uint32_t               slot_j;

	/* Slot assignment leaves dispatch_token == the class token for baseline
	 * (default-interface / unresolved) methods, and overrides it to a non-default
	 * interface token for methods that need a QI. Only the latter get a QI wrapper. */
	if (!rm->dispatch_token || rm->dispatch_token == rt->token) return;
	iface_raw = map_find_type_index(c->raw, rm->dispatch_token);
	if (iface_raw == UINT32_MAX) return;
	slot_j = map_slot_for_topo(c->out, iface_raw);
	if (slot_j == UINT32_MAX) return;
	mm->dispatch_iface_c = map_strdup(c->out->arena, c->out->types [slot_j].c_typedef);
	mm->dispatch_has_iid = c->out->types [slot_j].has_uuid;
}

/* Fill names, comment and signature of one mapped method (non-dispatch fields). */
static int map_fill_method_core(map_ctx const *c, cwinrt_raw_method const *rm, uint32_t raw_idx, cwinrt_mapped_method *mm) {
	cwinrt_mapped_unit    *out = c->out;
	cwinrt_raw_type const *rt  = &c->raw->types [raw_idx];
	char                  *cmt;

	mm->winrt_name = map_strdup(out->arena, rm->name);
	if (rm->c_name) mm->c_name = map_strdup(out->arena, rm->c_name);
	else mm->c_name = cwinrt_name_method(c->ns_prefix, rt->short_name, rm->name, out->arena);

	cmt = cwinrt_format_method_comment(
	  out->arena, rt->full_name, rm->name, rm->param_names, rm->param_name_count, rm->overload_name
	);
	mm->comment = cmt ? cmt : map_strdup(out->arena, rt->full_name);

	/* params come verbatim from winmd_parse_sig (rm->params_c) -- single source of
	   truth: map only byte-copies, never re-derives. The c_sig built here is rendered
	   identically by both emit.c and emit_impl.c; do not build a second signature
	   in either. cwinrt_fixup_params_c is a deliberate no-op (normalization belongs
	   upstream in winmd_parse_sig). */
	mm->params_c = rm->params_c ? map_strdup(out->arena, rm->params_c) : NULL;
	if (mm->params_c) cwinrt_fixup_params_c(mm->params_c);
	if (mm->params_c) mm->c_sig = map_format_sig(out->arena, mm->c_name, mm->params_c);
	else mm->c_sig = map_format_sig(out->arena, mm->c_name, NULL);
	if (!mm->c_sig) return -1;
	mm->is_static   = rm->is_static;
	mm->vtable_slot = rm->vtable_slot;
	return 0;
}

/* Fill one mapped method slot from raw method `mi`. */
static int map_fill_method(map_ctx const *c, uint32_t mi, uint32_t *method_fill) {
	cwinrt_raw_method const *rm      = &c->raw->methods [mi];
	uint32_t                 raw_idx = map_find_type_index(c->raw, rm->type_token);
	uint32_t                 slot_i;
	cwinrt_mapped_type      *mt;
	uint32_t                 slot;
	cwinrt_mapped_method    *mm;

	if (raw_idx == UINT32_MAX) return 0;
	slot_i = map_slot_for_topo(c->out, raw_idx);
	if (slot_i == UINT32_MAX) return 0;
	mt   = &c->out->types [slot_i];
	slot = method_fill [slot_i];
	if (slot >= mt->method_count) {
		errmsg("map: method slot overflow");
		return -1;
	}
	method_fill [slot_i] = slot + 1;
	mm                   = &mt->methods [slot];
	if (map_fill_method_core(c, rm, raw_idx, mm) != 0) return -1;
	if (rm->is_static && mt->kind == CWINRT_MAP_CLASS)
		map_resolve_static_dispatch(c, raw_idx, rm, mt, mm);
	else if (!rm->is_static && mt->kind == CWINRT_MAP_CLASS)
		map_resolve_instance_dispatch(c, raw_idx, rm, mm);
	return 0;
}

static int map_add_default_includes(map_ctx const *c) {
	cwinrt_mapped_unit  *out = c->out;
	cwinrt_raw_db const *raw = c->raw;
	map_add_include(out->arena, out, "<stdint.h>");
	map_add_include(out->arena, out, "<windows.h>");
	map_add_include(out->arena, out, "<cwinrt/hstring.h>");
	map_add_include(out->arena, out, "<unknwn.h>");
	map_add_include(out->arena, out, "<inspectable.h>");
	if (raw->filter_ns
		&& strncmp(raw->filter_ns, "Windows.", 8) == 0
		&& strcmp(raw->filter_ns, "Windows.Foundation") != 0)
	{
		map_add_include(out->arena, out, "<cwinrt/Windows.Foundation.h>");
		if (strcmp(raw->filter_ns, "Windows.Foundation.Collections") != 0)
			map_add_include(out->arena, out, "<cwinrt/Windows.Foundation.Collections.h>");
	}
	return 0;
}

/* Decide whether a cross-namespace parent should be included (and its forward
   skipped). Sets *do_include when the defining header must be pulled in.
   Preserves the original predicate exactly. */
static bool map_extern_should_skip_forward(
  cwinrt_mapped_unit const *out, cwinrt_raw_db const *raw, char const *parent, char const *cname, bool *do_include
) {
	*do_include = false;
	if (raw->filter_ns && strcmp(parent, raw->filter_ns) == 0) return true;
	if (!(raw->filter_ns && strncmp(parent, "Windows", 7) == 0 && (parent [7] == '\0' || parent [7] == '.')))
		return false;
	{
		size_t plen = strlen(parent);
		size_t flen = strlen(raw->filter_ns);
		bool   parent_encloses
		  = plen < flen && strncmp(raw->filter_ns, parent, plen) == 0 && raw->filter_ns [plen] == '.';
		bool   sibling = raw->filter_ns && strcmp(parent, raw->filter_ns) != 0;
		if (parent_encloses
			|| strcmp(parent, "Windows.Foundation.Numerics") == 0
			|| strcmp(parent, "Windows.Graphics") == 0
			|| strncmp(parent, "Windows.Graphics.", 17) == 0
			|| (sibling && map_cname_use_defining_header(cname))
			|| (sibling && map_cname_used_by_value(out, cname)))
		{
			*do_include = true;
			return true;
		}
	}
	return false;
}

/* Emit an include or an extern forward decl for one raw extern type. */
static int map_add_one_extern(map_ctx const *c, char const *ext) {
	cwinrt_mapped_unit *out = c->out;
	char                cname [128];
	char                parent [128];
	char const         *dot = strrchr(ext, '.');
	size_t              pn;
	bool                skip_fwd;
	bool                do_include;

	if (!dot) return 0;
	cwinrt_name_winrt_to_c(ext, cname, sizeof(cname));
	pn = ( size_t ) (dot - ext);
	if (pn >= sizeof(parent)) return map_add_extern_forward(out->arena, out, cname);

	memcpy(parent, ext, pn);
	parent [pn] = '\0';
	skip_fwd    = map_extern_should_skip_forward(out, c->raw, parent, cname, &do_include);
	if (do_include && map_add_include_for_winrt_ns(out->arena, out, parent) != 0) return -1;
	if (!skip_fwd) return map_add_extern_forward(out->arena, out, cname);
	return 0;
}

static int map_add_extern_forwards(map_ctx const *c) {
	uint32_t ei;
	for (ei = 0; ei < c->raw->extern_type_count; ei++)
		if (map_add_one_extern(c, c->raw->extern_types [ei]) != 0) return -1;
	return 0;
}

static int map_setup_header(cwinrt_mapped_unit *out, cwinrt_raw_db const *raw, char const *ns_prefix, cwinrt_value_type_set const *vts) {
	char hdr [128];
	char guard [128];

	memset(out, 0, sizeof(*out));
	out->value_types = vts;
	out->arena       = mkarena();
	if (out->arena < 0) {
		errmsg("map mkarena");
		return -1;
	}
	out->ns_prefix = map_strdup(out->arena, ns_prefix);
	if (raw->filter_ns) out->filter_ns = map_strdup(out->arena, raw->filter_ns);
	cwinrt_name_header_from_ns(raw->filter_ns, hdr, sizeof(hdr));
	cwinrt_name_guard_from_ns(raw->filter_ns, guard, sizeof(guard));
	out->header_name   = map_strdup(out->arena, hdr);
	out->include_guard = map_strdup(out->arena, guard);
	out->type_count    = raw->type_count;
	return 0;
}

/* Allocate the types array plus the three per-type scratch arrays; init scratch. */
static int map_alloc_scratch(
  cwinrt_mapped_unit *out, uint32_t **method_counts, uint32_t **method_fill, uint32_t **type_order
) {
	size_t bytes = ( size_t ) out->type_count * sizeof(uint32_t);
	out->types     = ( cwinrt_mapped_type * ) aden(out->arena, out->type_count * sizeof(cwinrt_mapped_type));
	*method_counts = ( uint32_t * ) aden(out->arena, bytes);
	*method_fill   = ( uint32_t * ) aden(out->arena, bytes);
	*type_order    = ( uint32_t * ) aden(out->arena, bytes);
	if (!out->types || !*method_counts || !*method_fill || !*type_order) {
		errmsg("map alloc");
		return -1;
	}
	memset(*method_counts, 0, bytes);
	memset(*method_fill, 0, bytes);
	return 0;
}

/* type_order is the topo order when present and consistent, else identity. */
static void map_init_type_order(cwinrt_raw_db const *raw, cwinrt_topo_graph const *topo, uint32_t *type_order) {
	uint32_t ti;
	if (topo && topo->order && topo->order_count == raw->type_count)
		for (ti = 0; ti < topo->order_count; ti++) type_order [ti] = topo->order [ti];
	else
		for (ti = 0; ti < raw->type_count; ti++) type_order [ti] = ti;
}

static int map_map_all_types(map_ctx const *c, uint32_t const *type_order) {
	uint32_t ti;
	for (ti = 0; ti < c->raw->type_count; ti++)
		if (map_one_type(c, ti, type_order [ti]) != 0) return -1;
	return 0;
}

static int map_fill_all_methods(map_ctx const *c, uint32_t *method_counts, uint32_t *method_fill) {
	uint32_t mi;
	map_count_methods(c, method_counts);
	if (map_alloc_methods(c->out, method_counts) != 0) return -1;
	for (mi = 0; mi < c->raw->method_count; mi++)
		if (map_fill_method(c, mi, method_fill) != 0) return -1;
	return 0;
}

int cwinrt_map_unit(
  cwinrt_raw_db const *raw, cwinrt_topo_graph const *topo, char const *ns_prefix,
  cwinrt_value_type_set const *vts, cwinrt_mapped_unit *out
) {
	uint32_t *method_counts = NULL;
	uint32_t *method_fill   = NULL;
	uint32_t *type_order    = NULL;
	map_ctx   c;

	if (!raw || !out || !ns_prefix) return -1;
	if (map_setup_header(out, raw, ns_prefix, vts) != 0) return -1;
	if (!out->type_count) {
		out->types = NULL;
		return 0;
	}
	if (map_alloc_scratch(out, &method_counts, &method_fill, &type_order) != 0) return -1;
	map_init_type_order(raw, topo, type_order);

	c.out       = out;
	c.raw       = raw;
	c.ns_prefix = ns_prefix;
	if (map_map_all_types(&c, type_order) != 0) return -1;
	if (map_fill_all_methods(&c, method_counts, method_fill) != 0) return -1;
	if (map_add_default_includes(&c) != 0) return -1;
	if (map_add_extern_forwards(&c) != 0) return -1;
	if (map_add_includes_from_signature_tokens(out->arena, raw, out) != 0) return -1;
	return 0;
}

void cwinrt_mapped_free(cwinrt_mapped_unit *u) {
	if (!u) return;
	if (u->arena > 0) rmarena(u->arena);
	memset(u, 0, sizeof(*u));
}
