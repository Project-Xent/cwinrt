#include "emit.h"

#include "arena.h"
#include "bio.h"
#include "dispatch_util.h"
#include "err.h"
#include "fmt.h"
#include "io.h"
#include "name.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int emit_write_str(int bd, char const *s) {
	vlong n;
	if (!s) return 0;
	n = bwrite(bd, ( void * ) s, strlen(s));
	return n < 0 ? -1 : 0;
}

static int emit_guard_open(int bd, cwinrt_mapped_unit const *u) {
	return bprint(bd, "#ifndef %s\n#define %s\n\n", u->include_guard, u->include_guard) < 0 ? -1 : 0;
}

static int emit_includes(int bd, cwinrt_mapped_unit const *u) {
	uint32_t i;
	for (i = 0; i < u->include_count; i++)
		if (bprint(bd, "#include %s\n", u->includes [i]) < 0) return -1;
	/* Parameterized IIDs for generic instantiations (QI with &CWINRT_IID_<name>). */
	if (bprint(bd, "#include <cwinrt/cwinrt_piids.h>\n") < 0) return -1;
	return emit_write_str(bd, "\n");
}

/* Membership test against a NULL-terminated string table. */
static bool emit_name_in_table(char const *name, char const *const *table) {
	int i;
	for (i = 0; table [i]; i++)
		if (strcmp(name, table [i]) == 0) return true;
	return false;
}

static bool emit_skip_extern_forward(char const *name) {
	static char const *skip [] = {
	  "WF_Size", "WF_Point", "WF_Rect", "WFN_Vector2", "WFN_Vector3", "WFN_Vector4", "WUI_Color", "WUI_WindowId",
	  "WG_SizeInt32", "WG_PointInt32", "WG_RectInt32", "WG_DisplayId", "WG_DisplayAdapterId", NULL};
	if (!name) return true;
	/* Provided by Windows.Foundation.h / Windows.Foundation.Numerics.h includes. */
	if (strncmp(name, "WF_", 3) == 0 || strncmp(name, "WFN_", 4) == 0) return true;
	if (strncmp(name, "WFOCO_", 6) == 0) return true;
	return emit_name_in_table(name, skip);
}

static int emit_guarded_forward(int bd, char const *cname);

/* Emit a full enum definition inline (guarded), identical to the one in its
   defining header so the CWINRT_FWD_ guard de-dups whichever appears first.
   Used for cross-namespace enums (cannot be opaque-forward-declared in C). */
static int emit_inline_enum_def(int bd, cwinrt_enum_def const *e) {
	uint32_t mi;
	if (!e || !e->cname) return 0;
	if (bprint(bd, "#ifndef CWINRT_FWD_%s\n#define CWINRT_FWD_%s\n", e->cname, e->cname) < 0) return -1;
	if (bprint(bd, "typedef enum %s {\n", e->cname) < 0) return -1;
	for (mi = 0; mi < e->member_count; mi++)
		if (bprint(
			  bd, "    %s_%s = %lld%s\n", e->cname, e->members [mi].name, ( long long ) e->members [mi].value,
			  mi + 1 < e->member_count ? "," : ""
			)
			< 0)
			return -1;
	return bprint(bd, "} %s;\n#endif\n", e->cname) < 0 ? -1 : 0;
}

/* Forward-declare a referenced cross-namespace type: full enum definition when it
   is a known enum (registry), otherwise an opaque struct forward. */
static int emit_referenced_type_decl(int bd, cwinrt_mapped_unit const *u, char const *cname) {
	cwinrt_enum_def const *e = cwinrt_value_type_find_enum(u->value_types, cname);
	if (e) return emit_inline_enum_def(bd, e);
	return emit_guarded_forward(bd, cname);
}

static bool emit_has_extern_forward(cwinrt_mapped_unit const *u) {
	uint32_t i;
	for (i = 0; i < u->extern_forward_count; i++)
		if (!emit_skip_extern_forward(u->extern_forwards [i])) return true;
	return false;
}

static int emit_extern_forwards(int bd, cwinrt_mapped_unit const *u) {
	uint32_t i;
	if (!u->extern_forward_count || !emit_has_extern_forward(u)) return 0;
	if (emit_write_str(bd, "/* External types (forward declarations) */\n") != 0) return -1;
	for (i = 0; i < u->extern_forward_count; i++) {
		if (emit_skip_extern_forward(u->extern_forwards [i])) continue;
		if (emit_referenced_type_decl(bd, u, u->extern_forwards [i]) != 0) return -1;
	}
	return emit_write_str(bd, "\n");
}

static bool emit_is_wellknown_abi_name(char const *name) {
	static char const *names [] = {
	  "WF_Size", "WF_Point", "WF_Rect", "WFN_Vector2", "WFN_Vector3", "WFN_Vector4", "WG_SizeInt32", "WG_PointInt32",
	  "WG_RectInt32", NULL};
	if (!name) return false;
	return emit_name_in_table(name, names);
}

static bool emit_is_foundation_ns(cwinrt_mapped_unit const *u) {
	return u && u->filter_ns && strcmp(u->filter_ns, "Windows.Foundation") == 0;
}

static int emit_foundation_supplemental_types(int bd, cwinrt_mapped_unit const *u) {
	if (!emit_is_foundation_ns(u)) return 0;
	/* Guard so the matching opaque forward decl in consumer headers is skipped. */
	if (emit_write_str(bd, "#ifndef CWINRT_FWD_WF_AsyncStatus\n#define CWINRT_FWD_WF_AsyncStatus\n") != 0) return -1;
	if (emit_write_str(bd, "typedef enum WF_AsyncStatus {\n") != 0) return -1;
	if (emit_write_str(bd, "    WF_AsyncStatus_Started = 0,\n") != 0) return -1;
	if (emit_write_str(bd, "    WF_AsyncStatus_Completed = 1,\n") != 0) return -1;
	if (emit_write_str(bd, "    WF_AsyncStatus_Canceled = 2,\n") != 0) return -1;
	if (emit_write_str(bd, "    WF_AsyncStatus_Error = 3\n") != 0) return -1;
	/* No WF_PropertyType placeholder: emit_enums emits it as a real enum before any
	   method uses it; a `typedef int` alias would be a clang/gcc typedef redefinition. */
	return emit_write_str(bd, "} WF_AsyncStatus;\n#endif\n\n");
}

static int emit_wellknown_value_types(int bd) {
	if (emit_write_str(bd, "/* Well-known value types (ABI layout) */\n") != 0) return -1;
	if (emit_write_str(bd, "typedef struct WF_Size { float Width; float Height; } WF_Size;\n") != 0) return -1;
	if (emit_write_str(bd, "typedef struct WF_Point { float X; float Y; } WF_Point;\n") != 0) return -1;
	if (emit_write_str(bd, "typedef struct WF_Rect { float X; float Y; float Width; float Height; } WF_Rect;\n") != 0)
		return -1;
	if (emit_write_str(bd, "typedef struct WFN_Vector2 { float X; float Y; } WFN_Vector2;\n") != 0) return -1;
	if (emit_write_str(bd, "typedef struct WFN_Vector3 { float X; float Y; float Z; } WFN_Vector3;\n") != 0) return -1;
	if (emit_write_str(bd, "typedef struct WFN_Vector4 { float X; float Y; float Z; float W; } WFN_Vector4;\n") != 0)
		return -1;
	return emit_write_str(bd, "\n");
}

static bool emit_params_need_wg(char const *p) {
	static char const *need [] = {"WG_SizeInt32", "WG_PointInt32", "WG_RectInt32", NULL};
	int                ni;
	if (!p) return false;
	for (ni = 0; need [ni]; ni++)
		if (strstr(p, need [ni])) return true;
	return false;
}

static bool emit_type_needs_wg(cwinrt_mapped_type const *t) {
	uint32_t mi;
	for (mi = 0; mi < t->method_count; mi++)
		if (emit_params_need_wg(t->methods [mi].params_c)) return true;
	return false;
}

static bool emit_unit_needs_wg_value_types(cwinrt_mapped_unit const *u) {
	uint32_t ti;
	if (!u) return false;
	for (ti = 0; ti < u->type_count; ti++)
		if (emit_type_needs_wg(&u->types [ti])) return true;
	return false;
}

static int emit_wellknown_wg_value_types(int bd) {
	if (emit_write_str(bd, "/* Windows.Graphics integer value types (ABI layout) */\n") != 0) return -1;
	if (emit_write_str(
		  bd,
		  "#ifndef CWINRT_ABI_WG_SIZEINT32\n" "#define CWINRT_ABI_WG_SIZEINT32\n" "typedef struct WG_SizeInt32 { "
	                                                                              "int32_t Width; int32_t Height; } "
	                                                                              "WG_SizeInt32;\n" "#endif\n"
		)
		!= 0)
		return -1;
	if (emit_write_str(
		  bd,
		  "#ifndef CWINRT_ABI_WG_POINTINT32\n" "#define CWINRT_ABI_WG_POINTINT32\n" "typedef struct WG_PointInt32 { "
	                                                                                "int32_t X; int32_t Y; } "
	                                                                                "WG_PointInt32;\n" "#endif\n"
		)
		!= 0)
		return -1;
	if (emit_write_str(
		  bd,
		  "#ifndef CWINRT_ABI_WG_RECTINT32\n" "#define CWINRT_ABI_WG_RECTINT32\n" "typedef struct WG_RectInt32 { "
	                                                                              "int32_t X; int32_t Y; int32_t "
	                                                                              "Width; int32_t Height; } "
	                                                                              "WG_RectInt32;\n" "#endif\n\n"
		)
		!= 0)
		return -1;
	return 0;
}

static bool emit_skip_forward_decl(cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t) {
	if (t->kind == CWINRT_MAP_STRUCT) return true;
	if (t->kind == CWINRT_MAP_ENUM && t->enum_member_count) return true;
	if (emit_is_wellknown_abi_name(t->c_typedef)) return true;
	if (emit_is_foundation_ns(u) && strcmp(t->c_typedef, "WF_AsyncStatus") == 0) return true;
	if (emit_is_foundation_ns(u) && strcmp(t->c_typedef, "WF_PropertyType") == 0) return true;
	return false;
}

static int emit_forward_decls(int bd, cwinrt_mapped_unit const *u) {
	uint32_t ti;
	if (emit_write_str(bd, "/* Types (forward declarations) */\n") != 0) return -1;
	for (ti = 0; ti < u->type_count; ti++) {
		cwinrt_mapped_type const *t = &u->types [ti];
		if (emit_skip_forward_decl(u, t)) continue;
		if (bprint(bd, "typedef struct %s %s;\n", t->c_typedef, t->c_typedef) < 0) return -1;
	}
	return emit_write_str(bd, "\n");
}

static bool emit_type_known(cwinrt_mapped_unit const *u, char const *name) {
	uint32_t ti;
	if (!name || !name [0]) return true;
	for (ti = 0; ti < u->type_count; ti++)
		if (u->types [ti].c_typedef && strcmp(u->types [ti].c_typedef, name) == 0) return true;
	return false;
}

static bool emit_type_is_enum(cwinrt_mapped_unit const *u, char const *name) {
	uint32_t ti;
	if (!name || !name [0]) return false;
	for (ti = 0; ti < u->type_count; ti++) {
		cwinrt_mapped_type const *t = &u->types [ti];
		if (t->kind == CWINRT_MAP_ENUM && t->c_typedef && strcmp(t->c_typedef, name) == 0) return true;
	}
	return false;
}

static bool emit_extra_name_seen(char names [][128], uint32_t n, char const *tok) {
	uint32_t i;
	for (i = 0; i < n; i++)
		if (strcmp(names [i], tok) == 0) return true;
	return false;
}

static bool emit_unit_includes_header(cwinrt_mapped_unit const *u, char const *hdr) {
	uint32_t i;
	if (!u || !hdr) return false;
	for (i = 0; i < u->include_count; i++)
		if (u->includes [i] && strstr(u->includes [i], hdr)) return true;
	return false;
}

static bool emit_scan_type_token(char const *p) {
	size_t i;
	if (!p || p [0] != 'W') return false;
	if (strncmp(p, "IInspectable", 12) == 0) return true;
	for (i = 1; i < 12 && p [i]; i++)
		if (p [i] == '_') return i >= 2;
	return false;
}

static void emit_extra_name_add(char names [][128], uint32_t *n, cwinrt_mapped_unit const *u, char const *tok) {
	( void ) u;
	if (!tok || !tok [0] || *n >= 512) return;
	if (emit_extra_name_seen(names, *n, tok)) return;
	snprintf(names [*n], 128, "%s", tok);
	(*n)++;
}

static int emit_guarded_forward(int bd, char const *cname) {
	if (!cname || !cname [0]) return 0;
	return bprint(
			 bd, "#ifndef CWINRT_FWD_%s\n#define CWINRT_FWD_%s\ntypedef struct %s %s;\n#endif\n", cname, cname, cname,
			 cname
		   ) < 0
	       ? -1
	       : 0;
}

static bool emit_token_char(char c) {
	return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/* Length of the type-token starting at p (already known to begin one), or 0. */
static size_t emit_token_len(char const *p) {
	size_t n = 0;
	while (p [n] && emit_token_char(p [n])) n++;
	return n;
}

/* Record the type token of length `len` at p (if a usable, unknown, unseen name). */
static void emit_record_sig_token(
  cwinrt_mapped_unit const *u, char const *p, size_t len, char names [][128], uint32_t *n
) {
	char tok [128];
	if (len == 0 || len >= sizeof(tok)) return;
	memcpy(tok, p, len);
	tok [len] = '\0';
	if (!emit_type_known(u, tok) && !emit_extra_name_seen(names, *n, tok))
		emit_extra_name_add(names, n, u, tok);
}

/* Scan one params_c string, collecting unknown signature-referenced type names. */
static void emit_collect_sig_tokens(
  cwinrt_mapped_unit const *u, char const *p, char names [][128], uint32_t *n
) {
	if (!p) return;
	while (*p) {
		size_t len;
		if (!emit_scan_type_token(p)) {
			p++;
			continue;
		}
		len = emit_token_len(p);
		emit_record_sig_token(u, p, len, names, n);
		p += len ? len : 1;
	}
}

/* Emit the gathered signature-referenced type names (skipping enums and
   well-known ABI names, which are defined elsewhere). */
static int emit_sig_forward_block(int bd, cwinrt_mapped_unit const *u, char names [][128], uint32_t count) {
	uint32_t i;
	if (!count) return 0;
	if (emit_write_str(bd, "/* Signature-referenced types */\n") != 0) return -1;
	for (i = 0; i < count; i++) {
		if (emit_type_is_enum(u, names [i]) || emit_is_wellknown_abi_name(names [i])) continue;
		if (emit_referenced_type_decl(bd, u, names [i]) != 0) return -1;
	}
	return emit_write_str(bd, "\n");
}

static int emit_sig_type_forwards(int bd, cwinrt_mapped_unit const *u) {
	uint32_t ti;
	uint32_t mi;
	int      rc;
	char     (*extra) [128] = NULL;
	uint32_t extra_n        = 0;

	extra                   = ( char (*) [128] ) calloc(512, 128);
	if (!extra) return -1;

	for (ti = 0; ti < u->type_count; ti++) {
		cwinrt_mapped_type const *t = &u->types [ti];
		for (mi = 0; mi < t->method_count; mi++)
			emit_collect_sig_tokens(u, t->methods [mi].params_c, extra, &extra_n);
	}
	rc = emit_sig_forward_block(bd, u, extra, extra_n);
	free(extra);
	return rc;
}

static bool emit_skip_enum(cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t) {
	if (t->kind != CWINRT_MAP_ENUM || !t->enum_member_count) return true;
	if (emit_is_foundation_ns(u) && strcmp(t->c_typedef, "WF_AsyncStatus") == 0) return true;
	if (emit_is_wellknown_abi_name(t->c_typedef)) return true;
	if (emit_unit_includes_header(u, "Windows.UI.Composition.h") && t->c_typedef
		&& strncmp(t->c_typedef, "WUC_", 4) == 0)
		return true;
	return false;
}

static int emit_one_enum(int bd, cwinrt_mapped_type const *t) {
	uint32_t mi;
	/* Guard with CWINRT_FWD_<name> so this real definition and any opaque
	   forward decl of the same type are mutually exclusive: the real enum wins
	   over a (struct) forward of the same name -- no redefinition. */
	if (bprint(bd, "#ifndef CWINRT_FWD_%s\n#define CWINRT_FWD_%s\n", t->c_typedef, t->c_typedef) < 0) return -1;
	if (bprint(bd, "typedef enum %s {\n", t->c_typedef) < 0) return -1;
	for (mi = 0; mi < t->enum_member_count; mi++)
		if (bprint(
			  bd, "    %s_%s = %lld%s\n", t->c_typedef, t->enum_members [mi].name,
			  ( long long ) t->enum_members [mi].value, mi + 1 < t->enum_member_count ? "," : ""
			)
			< 0)
			return -1;
	return bprint(bd, "} %s;\n#endif\n\n", t->c_typedef) < 0 ? -1 : 0;
}

static int emit_enums(int bd, cwinrt_mapped_unit const *u) {
	uint32_t ti;
	for (ti = 0; ti < u->type_count; ti++) {
		cwinrt_mapped_type const *t = &u->types [ti];
		if (emit_skip_enum(u, t)) continue;
		if (emit_one_enum(bd, t) != 0) return -1;
	}
	return 0;
}

static int emit_one_struct(int bd, cwinrt_mapped_type const *t) {
	uint32_t fi;
	/* Emit the typedef forward and the struct body under SEPARATE guards. A bare
	   `typedef struct X X;` opaque forward (CWINRT_FWD_X) and the body `struct X {..}`
	   (CWINRT_DEF_X) are both legal and may coexist, so an opaque forward emitted
	   earlier (e.g. via an include cycle) can never suppress the real definition. */
	if (bprint(bd, "#ifndef CWINRT_FWD_%s\n#define CWINRT_FWD_%s\ntypedef struct %s %s;\n#endif\n", t->c_typedef,
			   t->c_typedef, t->c_typedef, t->c_typedef)
		< 0)
		return -1;
	if (bprint(bd, "#ifndef CWINRT_DEF_%s\n#define CWINRT_DEF_%s\nstruct %s {\n", t->c_typedef, t->c_typedef,
			   t->c_typedef)
		< 0)
		return -1;
	for (fi = 0; fi < t->field_count; fi++)
		if (bprint(bd, "    %s %s;\n", t->fields [fi].c_type, t->fields [fi].name) < 0) return -1;
	if (bprint(bd, "};\n#endif\n\n") < 0) return -1;
	return 0;
}

/* Find the struct type in this unit whose c_typedef equals `cname`, or -1. */
static int emit_struct_index_by_name(cwinrt_mapped_unit const *u, char const *cname) {
	uint32_t ti;
	if (!cname) return -1;
	for (ti = 0; ti < u->type_count; ti++)
		if (u->types [ti].kind == CWINRT_MAP_STRUCT && u->types [ti].c_typedef
			&& strcmp(u->types [ti].c_typedef, cname) == 0)
			return ( int ) ti;
	return -1;
}

/* Emit struct `ti` after any same-unit struct it contains by value (DFS post-order).
   state: 0=unvisited, 1=on stack, 2=emitted. By-value containment is acyclic. */
static int emit_struct_dfs(int bd, cwinrt_mapped_unit const *u, uint32_t ti, uint8_t *state);

/* Emit field `fi`'s same-unit by-value dependency first, if not yet visited. */
static int emit_struct_visit_dep(int bd, cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t,
								 uint32_t fi, uint8_t *state) {
	int dep = emit_struct_index_by_name(u, t->fields [fi].c_type);
	if (dep < 0 || state [dep] != 0) return 0;
	return emit_struct_dfs(bd, u, ( uint32_t ) dep, state);
}

static int emit_struct_dfs(int bd, cwinrt_mapped_unit const *u, uint32_t ti, uint8_t *state) {
	uint32_t fi;
	cwinrt_mapped_type const *t = &u->types [ti];
	if (state [ti]) return 0; /* emitted or on stack (cycle guard) */
	state [ti] = 1;
	for (fi = 0; fi < t->field_count; fi++)
		if (emit_struct_visit_dep(bd, u, t, fi, state) != 0) return -1;
	if (state [ti] == 2) return 0;
	if (emit_one_struct(bd, t) != 0) return -1;
	state [ti] = 2;
	return 0;
}

/* Pre-mark non-struct, empty, and well-known-ABI types as emitted (state 2) so
   the DFS skips them. WF_Size/Point/Rect, WFN_Vector*, WG_* have hand-written
   ABI definitions (emit_wellknown_value_types) and must not be re-derived. */
static void emit_structs_init_state(cwinrt_mapped_unit const *u, uint8_t *state) {
	uint32_t ti;
	for (ti = 0; ti < u->type_count; ti++) {
		cwinrt_mapped_type const *t = &u->types [ti];
		if (t->kind != CWINRT_MAP_STRUCT || !t->field_count || emit_is_wellknown_abi_name(t->c_typedef))
			state [ti] = 2;
	}
}

static int emit_structs(int bd, cwinrt_mapped_unit const *u) {
	uint32_t ti;
	uint8_t *state;
	if (!u->type_count) return 0;
	state = ( uint8_t * ) calloc(u->type_count, 1);
	if (!state) return -1;
	emit_structs_init_state(u, state);
	for (ti = 0; ti < u->type_count; ti++) {
		if (u->types [ti].kind != CWINRT_MAP_STRUCT || state [ti] == 2) continue;
		if (emit_struct_dfs(bd, u, ti, state) != 0) {
			free(state);
			return -1;
		}
	}
	free(state);
	return 0;
}

static int emit_activate_new(int bd, cwinrt_mapped_type const *t) {
	if (!t->is_activatable || !t->activate_c_name || !t->c_typedef) return 0;
	if (t->kind == CWINRT_MAP_CLASS && !t->method_count) return 0;
	return bprint(bd, "/* activate %s */\nHRESULT %s(%s **out);\n\n", t->winrt_name, t->activate_c_name, t->c_typedef)
	         < 0
	       ? -1
	       : 0;
}

static bool emit_event_core_name(char const *winrt_method, char *core, size_t cap) {
	if (!winrt_method || !core || cap < 2) return false;
	if (strncmp(winrt_method, "add_", 4) == 0) {
		snprintf(core, cap, "%s", winrt_method + 4);
		return core [0] != '\0';
	}
	if (strncmp(winrt_method, "remove_", 7) == 0) {
		snprintf(core, cap, "%s", winrt_method + 7);
		return core [0] != '\0';
	}
	return false;
}

static void emit_event_snake(char const *core, char *dst, size_t cap) {
	size_t di = 0;
	size_t i  = 0;
	if (!core || !dst || cap < 2) return;
	for (i = 0; core [i] && di + 2 < cap; i++) {
		char c = core [i];
		if (c < 'A' || c > 'Z') {
			dst [di++] = c;
			continue;
		}
		if (di > 0) dst [di++] = '_';
		dst [di++] = ( char ) (c - 'A' + 'a');
	}
	dst [di] = '\0';
}

/* Locate the remove_ method whose core name matches `core`, or NULL. */
static cwinrt_mapped_method const *emit_find_remove(cwinrt_mapped_type const *t, char const *core) {
	uint32_t mj;
	for (mj = 0; mj < t->method_count; mj++) {
		char                        rem_core [128];
		cwinrt_mapped_method const *m2 = &t->methods [mj];
		if (!m2->winrt_name || strncmp(m2->winrt_name, "remove_", 7) != 0) continue;
		snprintf(rem_core, sizeof(rem_core), "%s", m2->winrt_name + 7);
		if (strcmp(core, rem_core) == 0) return m2;
	}
	return NULL;
}

static int emit_one_event_wrapper(int bd, cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t,
								   char const *abbrev, char const *core) {
	char ev_snake [128];
	emit_event_snake(core, ev_snake, sizeof(ev_snake));
	return bprint(
			 bd,
			 "cwinrt_token %s_%s_on_%s(%s *self, cwinrt_event_fn fn, void *ctx);\n" "void %s_%s_off_%s(%s *self, "
		                                                                            "cwinrt_token token);\n\n",
			 u->ns_prefix, abbrev, ev_snake, t->c_typedef, u->ns_prefix, abbrev, ev_snake, t->c_typedef
		   ) < 0
		   ? -1
		   : 0;
}

static int emit_event_section_hdr(int bd) {
	if (emit_write_str(bd, "/* Event subscriptions (on_/off_) */\n") != 0) return -1;
	return emit_write_str(bd, "#include <cwinrt/event.h>\n\n");
}

/* True if method `mi` of `t` is an add_X with a matching remove_X and both slots. */
static bool emit_is_event_pair(cwinrt_mapped_type const *t, uint32_t mi, char *core, size_t cap) {
	cwinrt_mapped_method const *add_m = &t->methods [mi];
	cwinrt_mapped_method const *rem_m;
	if (!add_m->winrt_name || strncmp(add_m->winrt_name, "add_", 4) != 0) return false;
	if (!emit_event_core_name(add_m->winrt_name, core, cap)) return false;
	rem_m = emit_find_remove(t, core);
	return rem_m && add_m->vtable_slot && rem_m->vtable_slot;
}

/* Short type name abbreviation used in the on_/off_ wrapper symbol prefix. */
static char const *emit_type_abbrev(cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t) {
	char const *dot    = t->winrt_name ? strrchr(t->winrt_name, '.') : NULL;
	char const *shortn = dot ? dot + 1 : "";
	return cwinrt_name_type_abbrev(u->filter_ns, shortn);
}

/* Emit the on_/off_ wrappers for event pair `mi`, writing the section header
   once on the first pair (*wrote_hdr tracks this across calls). */
static int emit_event_pair(int bd, cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t,
						   uint32_t mi, bool *wrote_hdr) {
	char core [128];
	if (!emit_is_event_pair(t, mi, core, sizeof(core))) return 0;
	if (!*wrote_hdr) {
		if (emit_event_section_hdr(bd) != 0) return -1;
		*wrote_hdr = true;
	}
	return emit_one_event_wrapper(bd, u, t, emit_type_abbrev(u, t), core);
}

static int emit_event_wrappers(int bd, cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t) {
	uint32_t mi;
	bool     wrote_hdr = false;

	if (t->kind == CWINRT_MAP_IFACE) return 0;
	for (mi = 0; mi < t->method_count; mi++)
		if (emit_event_pair(bd, u, t, mi, &wrote_hdr) != 0) return -1;
	return 0;
}

/* Emit a per-interface IID constant for every interface in the unit. Consumers
   QI with &CWINRT_IID_<name> instead of the runtime's blind GetIids()+QI scan. */
static int emit_iid_block(int bd, cwinrt_mapped_unit const *u) {
	uint32_t ti;
	bool     any = false;
	for (ti = 0; ti < u->type_count; ti++) {
		cwinrt_mapped_type const *t = &u->types [ti];
		char                      iid_sym [160];
		uint8_t const            *g;
		if (!t->has_uuid || t->kind != CWINRT_MAP_IFACE) continue;
		cwinrt_name_iid_symbol(t->c_typedef, iid_sym, sizeof(iid_sym));
		g = t->uuid;
		/* These IIDs are header-only static const; most are unused in any given TU,
		   so tag them maybe-unused to stay clean under clang/gcc -Wall -Wextra. */
		if (!any
		    && emit_write_str(
		         bd, "/* Interface IIDs */\n"
		             "#ifndef CWINRT_MAYBE_UNUSED\n"
		             "#  if defined(__GNUC__) || defined(__clang__)\n"
		             "#    define CWINRT_MAYBE_UNUSED __attribute__((unused))\n"
		             "#  else\n"
		             "#    define CWINRT_MAYBE_UNUSED\n"
		             "#  endif\n"
		             "#endif\n"
		       ) != 0)
			return -1;
		if (bprint(
			  bd,
			  "#ifndef CWINRT_IIDDEF_%s\n#define CWINRT_IIDDEF_%s\n"
			  "static const IID %s CWINRT_MAYBE_UNUSED = { 0x%02X%02X%02X%02X, "
			  "0x%02X%02X, 0x%02X%02X, { 0x%02X, 0x%02X, 0x%02X, "
			  "0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X } };\n"
			  "#endif\n",
			  t->c_typedef, t->c_typedef, iid_sym, g [3], g [2], g [1], g [0], g [5], g [4], g [7], g [6], g [8], g [9],
			  g [10], g [11], g [12], g [13], g [14], g [15]
			)
			< 0)
			return -1;
		any = true;
	}
	if (any && emit_write_str(bd, "\n") != 0) return -1;
	return 0;
}

static bool emit_skip_method(cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t,
							 cwinrt_mapped_method const *m) {
	if (m->winrt_name && (strncmp(m->winrt_name, "add_", 4) == 0 || strncmp(m->winrt_name, "remove_", 7) == 0))
		return true;
	return cwinrt_mapped_skip_iface_method_dup(u, t, m);
}

static int emit_one_method(int bd, cwinrt_mapped_method const *m) {
	if (bprint(bd, "/* %s */\n", m->comment ? m->comment : "") < 0) return -1;
	if (emit_write_str(bd, m->c_sig) != 0) return -1;
	return emit_write_str(bd, ";\n\n");
}

static int emit_type_methods(int bd, cwinrt_mapped_unit const *u, cwinrt_mapped_type const *t) {
	uint32_t mi;
	if (emit_activate_new(bd, t) != 0) return -1;
	if (!t->method_count) return 0;
	if (bprint(bd, "/* %s */\n", t->winrt_name) < 0) return -1;
	for (mi = 0; mi < t->method_count; mi++) {
		cwinrt_mapped_method const *m = &t->methods [mi];
		if (emit_skip_method(u, t, m)) continue;
		if (emit_one_method(bd, m) != 0) return -1;
	}
	return emit_event_wrappers(bd, u, t);
}

static int emit_methods(int bd, cwinrt_mapped_unit const *u) {
	uint32_t ti;
	if (emit_write_str(bd, "/* Methods */\n\n") != 0) return -1;
	for (ti = 0; ti < u->type_count; ti++)
		if (emit_type_methods(bd, u, &u->types [ti]) != 0) return -1;
	return 0;
}

static int emit_guard_close(int bd, cwinrt_mapped_unit const *u) {
	return bprint(bd, "#endif /* %s */\n", u->include_guard) < 0 ? -1 : 0;
}

/* Uniform (bd,unit) wrappers for the two conditional value-type sections so the
   whole header body can run through one ordered dispatch table. */
static int emit_wellknown_value_types_section(int bd, cwinrt_mapped_unit const *u) {
	return emit_is_foundation_ns(u) ? emit_wellknown_value_types(bd) : 0;
}

static int emit_wellknown_wg_value_types_section(int bd, cwinrt_mapped_unit const *u) {
	return emit_unit_needs_wg_value_types(u) ? emit_wellknown_wg_value_types(bd) : 0;
}

/* Emit the full header body in order; returns 0 on success, -1 on any write error. */
static int emit_header_body(int bd, cwinrt_mapped_unit const *unit) {
	static int (*const sections []) (int, cwinrt_mapped_unit const *) = {
	  emit_guard_open,
	  emit_includes,
	  emit_extern_forwards,
	  emit_wellknown_value_types_section,
	  emit_wellknown_wg_value_types_section,
	  emit_foundation_supplemental_types,
	  emit_forward_decls,
	  emit_sig_type_forwards,
	  emit_enums,
	  emit_structs,
	  emit_iid_block,
	  emit_methods,
	  emit_guard_close,
	};
	size_t i;
	emit_write_str(bd, "/* Generated by cwinrt-gen; do not edit. */\n\n");
	for (i = 0; i < sizeof(sections) / sizeof(sections [0]); i++)
		if (sections [i] (bd, unit) != 0) return -1;
	return 0;
}

int cwinrt_emit_header(cwinrt_mapped_unit const *unit, cwinrt_emit_opts const *opts) {
	char  path [1024];
	int   arena;
	int   bd;
	int   rc;
	omode mod = {.w = true, .t = true};
	if (!unit || !opts || !opts->out_dir || !opts->header_basename) return -1;
	snprintf(path, sizeof(path), "%s/%s", opts->out_dir, opts->header_basename);
	arena = mkarena();
	if (arena < 0) {
		errmsg("emit mkarena failed");
		return -1;
	}
	bd = bopen(arena, path, mod);
	if (bd < 0) {
		errmsg("emit bopen failed");
		rmarena(arena);
		return -1;
	}
	rc = emit_header_body(bd, unit);
	rmbio(bd);
	rmarena(arena);
	return rc;
}
