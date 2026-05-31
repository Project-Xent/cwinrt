#include "method_names.h"

#include "arena.h"
#include "name.h"
#include <stdio.h>
#include <string.h>

static void sanitize_ident(char *s) {
	size_t i;
	if (!s) return;
	for (i = 0; s [i]; i++) {
		char c = s [i];
		if (c == '`' || c == '<' || c == '>' || c == ',') s [i] = '_';
	}
}

/* Append src to dst (capacity cap) converting CamelCase to snake_case, starting
   the snake run at offset di. Returns the new length. */
static size_t snake_into(char *dst, size_t cap, size_t di, char const *src) {
	size_t i;
	for (i = 0; src [i] && di + 2 < cap; i++) {
		char c = src [i];
		if (c < 'A' || c > 'Z') {
			dst [di++] = c;
			continue;
		}
		if (di > 0) dst [di++] = '_';
		dst [di++] = ( char ) (c - 'A' + 'a');
	}
	dst [di] = '\0';
	return di;
}

static void snake_append(char *dst, size_t cap, char const *src) {
	if (!src || !cap) return;
	snake_into(dst, cap, strlen(dst), src);
}

/* Truncate buf just after the comma preceding the [out] retval/value, leaving the
   trailing comma in place. This deliberately differs from strip_self_and_retval
   (which also drops the comma) -- the suffix mangle relies on this exact output. */
static void suffix_strip_retval(char *buf) {
	char *retp = strstr(buf, "** retval");
	if (!retp) retp = strstr(buf, "** value");
	if (!retp) return;
	while (retp > buf && retp [-1] != ',') retp--;
	if (retp > buf && retp [-1] == ',') *retp = '\0';
}

/* Drop a leading "*self" parameter. Returns false (with buf emptied) when self is
   the only parameter, signalling the caller to abort. */
static bool suffix_strip_self(char *buf) {
	char *selfp = strstr(buf, "*self");
	char *comma;
	if (!selfp) return true;
	comma = strchr(selfp, ',');
	if (!comma) {
		buf [0] = '\0';
		return false;
	}
	memmove(buf, comma + 2, strlen(comma + 2) + 1);
	return true;
}

/* Return the trailing parameter's name within buf (the token after the last
   space of the last segment), or NULL when no usable name is present. */
static char const *suffix_param_name(char *buf) {
	char *last_comma;
	char const *seg;
	char const *last_space;
	char const *name;

	while (buf [0] == ' ') memmove(buf, buf + 1, strlen(buf));
	if (!buf [0]) return NULL;
	last_comma = strrchr(buf, ',');
	seg        = last_comma ? last_comma + 1 : buf;
	while (*seg == ' ') seg++;
	last_space = strrchr(seg, ' ');
	if (!last_space) return NULL;
	name = last_space + 1;
	while (*name == ' ') name++;
	if (!name [0] || strchr(name, '*') || strchr(name, ',')) return NULL;
	if (name [0] >= 'A' && name [0] <= 'Z') return NULL;
	if (strcmp(name, "retval") == 0 || strcmp(name, "value") == 0) return NULL;
	return name;
}

static void suffix_from_params(char const *params_c, char *dst, size_t cap) {
	char        buf [768];
	char const *name;
	char        snake [128];

	if (!dst || cap < 2) return;
	dst [0] = '\0';
	if (!params_c || !params_c [0]) return;
	snprintf(buf, sizeof(buf), "%s", params_c);
	suffix_strip_retval(buf);
	if (!suffix_strip_self(buf)) return;
	name = suffix_param_name(buf);
	if (!name) return;
	snake [0] = '\0';
	snake_append(snake, sizeof(snake), name);
	if (snake [0]) snprintf(dst, cap, "%s", snake);
}

/* Map one C parameter type to a short, stable mangling token. */
static void params_type_token(char const *type, char *dst, size_t cap) {
	static struct {
		char const *type;
		char const *tok;
	} const map [] = {
	  {"int32_t", "i32"}, {"uint32_t", "u32"}, {"int64_t", "i64"},  {"uint64_t", "u64"}, {"int16_t", "i16"},
	  {"uint16_t", "u16"}, {"uint8_t", "u8"},  {"float", "f32"},    {"double", "f64"},   {"boolean", "b"},
	  {"BOOL", "b"},      {"bool", "b"},        {"cwinrt_hstring", "str"}, {"HSTRING", "str"},
	};
	size_t      k;
	char const *p;
	if (strchr(type, '*')) {
		snprintf(dst, cap, "p");
		return;
	}
	for (k = 0; k < sizeof(map) / sizeof(map [0]); k++)
		if (strcmp(type, map [k].type) == 0) {
			snprintf(dst, cap, "%s", map [k].tok);
			return;
		}
	/* Unknown: strip a leading namespace prefix (WUC_Foo -> foo) and lowercase. */
	p       = strrchr(type, '_');
	dst [0] = '\0';
	snake_append(dst, cap, p ? p + 1 : type);
}

/* Drop the WinRT [out] retval (** retval/value) and a leading "*self" from a
   copy of a C parameter list, leaving only the real call parameters. */
static void strip_self_and_retval(char *buf) {
	char *p = strstr(buf, "** retval");
	if (!p) p = strstr(buf, "** value");
	if (p) {
		while (p > buf && p [-1] != ',') p--;
		if (p > buf && p [-1] == ',') p--;
		*p = '\0';
	}
	p = strstr(buf, "*self");
	if (p) {
		char *comma = strchr(p, ',');
		if (comma) memmove(buf, comma + 2, strlen(comma + 2) + 1);
		else buf [0] = '\0';
	}
}

/* Append one comma-delimited parameter segment's type token to acc. The type is
   everything up to the last space (which precedes the param name). Returns 1 when
   a token was emitted (a non-empty type), else 0, so the caller can sum arity. */
static uint32_t mangle_one_seg(char *acc, size_t acccap, char const *p, size_t n) {
	char   seg [128];
	char   tok [64];
	char  *sp;
	size_t di;

	if (n >= sizeof(seg)) n = sizeof(seg) - 1;
	memcpy(seg, p, n);
	seg [n] = '\0';
	while (seg [0] == ' ') memmove(seg, seg + 1, strlen(seg));
	sp = strrchr(seg, ' ');
	if (sp) *sp = '\0';
	if (!seg [0]) return 0;
	params_type_token(seg, tok, sizeof(tok));
	di = strlen(acc);
	if (di && di + 1 < acccap) acc [di++] = '_';
	snprintf(acc + di, acccap - di, "%s", tok);
	acc [acccap - 1] = '\0';
	return 1;
}

/* Build the readable type mangle (e.g. "i32_p") into acc from the stripped call
   parameter list buf, returning the parameter arity. */
static uint32_t mangle_param_types(char *buf, char *acc, size_t acccap) {
	char    *p;
	uint32_t arity = 0;

	acc [0] = '\0';
	for (p = buf; *p;) {
		char  *end = strchr(p, ',');
		size_t n   = end ? ( size_t ) (end - p) : strlen(p);
		arity += mangle_one_seg(acc, acccap, p, n);
		if (!end) break;
		p = end + 1;
	}
	return arity;
}

/* Deterministic type-signature suffix for an overload, e.g. "i32_p" or "0".
   Depends only on parameter types (arity + each type), never on metadata order,
   so a new overload adds a new symbol without renaming existing ones. */
static void params_type_mangle(char const *params_c, char *dst, size_t cap) {
	char     buf [768];
	char     acc [256];
	uint32_t arity;

	dst [0] = '\0';
	if (cap < 2) return;
	if (!params_c || !params_c [0]) {
		snprintf(dst, cap, "0");
		return;
	}
	snprintf(buf, sizeof(buf), "%s", params_c);
	strip_self_and_retval(buf);
	arity = mangle_param_types(buf, acc, sizeof(acc));
	if (!acc [0]) {
		snprintf(dst, cap, "%u", arity);
		return;
	}
	/* Bound the length: build_method_c_name's suffix buffer (suf[64]) would
	   otherwise truncate two long, distinct signatures to the same string. When the
	   readable mangle is long, keep a prefix + an FNV-1a hash of the full mangle so
	   distinct signatures stay distinct. */
	if (strlen(acc) <= 44) {
		snprintf(dst, cap, "%s", acc);
	}
	else {
		uint32_t    h = 2166136261u;
		char const *q;
		char        head [37];
		for (q = acc; *q; q++) {
			h ^= ( uint8_t ) *q;
			h *= 16777619u;
		}
		snprintf(head, sizeof(head), "%s", acc); /* first 36 chars */
		snprintf(dst, cap, "%s_%08x", head, h);
	}
}

static uint32_t count_call_params(char const *params_c) {
	char     buf [768];
	char    *p;
	uint32_t n = 0;

	if (!params_c || !params_c [0]) return 0;
	snprintf(buf, sizeof(buf), "%s", params_c);
	strip_self_and_retval(buf);
	for (p = buf; *p; p++)
		if (*p == ',') n++;
	if (buf [0]) n++;
	return n;
}

static void method_name_snake(char const *method_name, char *meth_snake, size_t cap) {
	char const *p;
	meth_snake [0] = '\0';
	if (!method_name) return;
	/* WinRT OnFoo handlers -> handle_foo; add_/remove_ events stay on_/off_ in emit.c */
	p = method_name;
	if (p [0] == 'O' && p [1] == 'n' && p [2] >= 'A' && p [2] <= 'Z') {
		snprintf(meth_snake, cap, "handle");
		snake_into(meth_snake, cap, strlen(meth_snake), p + 2);
		sanitize_ident(meth_snake);
		return;
	}
	snake_into(meth_snake, cap, 0, method_name);
	sanitize_ident(meth_snake);
}

/* Like snake_into but first folds the WinRT type punctuation (`<>,) to '_'. */
static void type_short_snake(char *dst, char const *src) {
	size_t di = 0;
	size_t i;
	for (i = 0; src [i] && di + 2 < 256; i++) {
		char c = src [i];
		if (c == '`' || c == '<' || c == '>' || c == ',') c = '_';
		if (c < 'A' || c > 'Z') {
			dst [di++] = c;
			continue;
		}
		if (di > 0) dst [di++] = '_';
		dst [di++] = ( char ) (c - 'A' + 'a');
	}
	dst [di] = '\0';
}

static void
type_method_snake(char const *type_short, char const *method_name, char *type_snake, char *meth_snake, size_t cap) {
	( void ) cap;
	type_snake [0] = '\0';
	meth_snake [0] = '\0';
	if (!type_short || !method_name) return;
	type_short_snake(type_snake, type_short);
	sanitize_ident(type_snake);
	snake_into(meth_snake, 256, 0, method_name);
	sanitize_ident(meth_snake);
}

static char *build_method_c_name(
  int arena, char const *ns_prefix, char const *winrt_ns, char const *type_short, char const *method_name,
  char const *suffix
) {
	char   type_snake [256];
	char   meth_snake [256];
	char   suf [64];
	char   tmp [512];
	size_t len;
	char  *buf;

	snprintf(type_snake, sizeof(type_snake), "%s", cwinrt_name_type_abbrev(winrt_ns, type_short));
	method_name_snake(method_name, meth_snake, sizeof(meth_snake));
	suf [0] = '\0';
	if (suffix && suffix [0]) snprintf(suf, sizeof(suf), "_%s", suffix);
	snprintf(tmp, sizeof(tmp), "%s_%s_%s%s", ns_prefix, type_snake, meth_snake, suf);
	len = strlen(tmp) + 1;
	buf = ( char * ) aden(arena, len);
	if (!buf) return NULL;
	memcpy(buf, tmp, len);
	return buf;
}

static bool param_name_reserved(char const *name) {
	return !name
	    || !name [0]
	    || strcmp(name, "retval") == 0
	    || strcmp(name, "out") == 0
	    || strcmp(name, "value") == 0
	    || cwinrt_name_is_c_keyword(name);
}

/* Pick the base overload (keeps the clean, unsuffixed name): the [default]
   overload if any, else the one with the fewest declared params (ties broken by
   fewest call params). Order-independent so a new SDK overload never renames it. */
static uint32_t group_pick_base(cwinrt_raw_db *db, uint32_t const *group, uint32_t gn) {
	uint32_t base_ix = 0;
	uint32_t gi;

	for (gi = 0; gi < gn; gi++) {
		if (db->methods [group [gi]].is_default_overload) return gi;
	}
	for (gi = 1; gi < gn; gi++) {
		cwinrt_raw_method *best = &db->methods [group [base_ix]];
		cwinrt_raw_method *cur  = &db->methods [group [gi]];
		if (cur->param_name_count < best->param_name_count) base_ix = gi;
		else if (cur->param_name_count == best->param_name_count
				 && count_call_params(cur->params_c) < count_call_params(best->params_c))
			base_ix = gi;
	}
	return base_ix;
}

/* Assign each overload's c_name: base keeps the clean name; others prefer the
   WinRT [Overload] attribute name, else a deterministic param-type mangle.
   No ordinal _1/_2, so insertions never renumber existing symbols. */
static void
group_assign_names(cwinrt_raw_db *db, char const *ns_prefix, cwinrt_raw_type const *rt, uint32_t const *group,
				   uint32_t gn, uint32_t base_ix) {
	uint32_t gi;
	for (gi = 0; gi < gn; gi++) {
		cwinrt_raw_method *rm = &db->methods [group [gi]];
		char               mangle [128];

		if (gi == base_ix) {
			rm->c_name = build_method_c_name(db->arena, ns_prefix, rt->ns, rt->short_name, rm->name, NULL);
			continue;
		}
		if (rm->overload_name && rm->overload_name [0] && strcmp(rm->overload_name, rm->name) != 0) {
			rm->c_name = build_method_c_name(db->arena, ns_prefix, rt->ns, rt->short_name, rm->overload_name, NULL);
			continue;
		}
		params_type_mangle(rm->params_c, mangle, sizeof(mangle));
		rm->c_name = build_method_c_name(db->arena, ns_prefix, rt->ns, rt->short_name, rm->name, mangle);
	}
}

/* Safety net: if two names still collide (same [Overload] name, or identical
   mangle), append the type mangle (and the static/instance kind) to the second.
   Still order-independent -- derived from the signature, not the group position. */
static void
group_resolve_collisions(cwinrt_raw_db *db, char const *ns_prefix, cwinrt_raw_type const *rt, uint32_t const *group,
						 uint32_t gn) {
	uint32_t gi;
	uint32_t gj;
	for (gi = 0; gi < gn; gi++) {
		for (gj = gi + 1; gj < gn; gj++) {
			cwinrt_raw_method *a = &db->methods [group [gi]];
			cwinrt_raw_method *b = &db->methods [group [gj]];
			char               sfx [160];
			char               mangle [128];
			if (!a->c_name || !b->c_name || strcmp(a->c_name, b->c_name) != 0) continue;
			params_type_mangle(b->params_c, mangle, sizeof(mangle));
			if (b->is_static != a->is_static)
				snprintf(sfx, sizeof(sfx), "%s_%s", mangle, b->is_static ? "static" : "instance");
			else snprintf(sfx, sizeof(sfx), "%s_x", mangle);
			b->c_name = build_method_c_name(db->arena, ns_prefix, rt->ns, rt->short_name, b->name, sfx);
		}
	}
}

static void
assign_method_group(cwinrt_raw_db *db, char const *ns_prefix, cwinrt_raw_type const *rt, uint32_t *group, uint32_t gn) {
	uint32_t base_ix;

	if (gn <= 1) {
		cwinrt_raw_method *rm = &db->methods [group [0]];
		rm->c_name            = build_method_c_name(db->arena, ns_prefix, rt->ns, rt->short_name, rm->name, NULL);
		return;
	}

	base_ix = group_pick_base(db, group, gn);
	group_assign_names(db, ns_prefix, rt, group, gn, base_ix);
	group_resolve_collisions(db, ns_prefix, rt, group, gn);
}

/* Gather the still-unnamed methods of type rt that share rm's name, starting at
   index mi, into group (cap 64). Returns the group size. */
static uint32_t
collect_same_name_group(cwinrt_raw_db *db, cwinrt_raw_type const *rt, uint32_t mi, uint32_t *group) {
	cwinrt_raw_method *rm = &db->methods [mi];
	uint32_t           gn = 0;
	uint32_t           mj;

	group [gn++] = mi;
	for (mj = mi + 1; mj < db->method_count; mj++) {
		cwinrt_raw_method *rm2 = &db->methods [mj];
		if (rm2->type_token != rt->token || rm2->c_name) continue;
		if (!rm->name || !rm2->name || strcmp(rm->name, rm2->name) != 0) continue;
		if (gn < 64) group [gn++] = mj;
	}
	return gn;
}

/* Name every method of rt by grouping same-named overloads together. */
static void assign_type_groups(cwinrt_raw_db *db, cwinrt_raw_type const *rt) {
	char const *ns_prefix = cwinrt_name_ns_prefix(rt->ns);
	uint32_t    mi;

	for (mi = 0; mi < db->method_count; mi++) {
		cwinrt_raw_method *rm = &db->methods [mi];
		uint32_t           group [64];
		uint32_t           gn;

		if (rm->type_token != rt->token || rm->c_name) continue;
		gn = collect_same_name_group(db, rt, mi, group);
		assign_method_group(db, ns_prefix, rt, group, gn);
	}
}

static cwinrt_raw_type const *find_type_by_token(cwinrt_raw_db *db, uint32_t token) {
	uint32_t tix;
	for (tix = 0; tix < db->type_count; tix++)
		if (db->types [tix].token == token) return &db->types [tix];
	return NULL;
}

/* Fallback for any method left unnamed (its type produced no group). */
static void assign_orphan_method(cwinrt_raw_db *db, cwinrt_raw_method *rm) {
	cwinrt_raw_type const *rt;
	if (rm->c_name) return;
	rt = find_type_by_token(db, rm->type_token);
	if (!rt) return;
	rm->c_name = build_method_c_name(db->arena, cwinrt_name_ns_prefix(rt->ns), rt->ns, rt->short_name, rm->name, NULL);
}

void cwinrt_assign_method_names(cwinrt_raw_db *db) {
	uint32_t ti;
	uint32_t mi;

	if (!db) return;

	for (ti = 0; ti < db->type_count; ti++) assign_type_groups(db, &db->types [ti]);
	for (mi = 0; mi < db->method_count; mi++) assign_orphan_method(db, &db->methods [mi]);
}

char *cwinrt_format_method_comment(
  int arena, char const *type_full, char const *method_name, char **param_names, uint32_t param_name_count,
  char const *overload_name
) {
	char    *buf;
	char     tmp [512];
	size_t   len;
	size_t   pos;
	uint32_t i;
	bool     first;

	if (!type_full || !method_name) return NULL;
	pos   = ( size_t ) snprintf(tmp, sizeof(tmp), "%s.%s(", type_full, method_name);
	first = true;
	for (i = 0; i < param_name_count && pos + 2 < sizeof(tmp); i++) {
		char const *pn = param_names [i];
		if (param_name_reserved(pn)) continue;
		if (!first) pos += ( size_t ) snprintf(tmp + pos, sizeof(tmp) - pos, ", ");
		pos   += ( size_t ) snprintf(tmp + pos, sizeof(tmp) - pos, "%s", pn);
		first  = false;
	}
	if (overload_name && overload_name [0] && strcmp(overload_name, method_name) != 0) {
		if (!first) pos += ( size_t ) snprintf(tmp + pos, sizeof(tmp) - pos, "; ");
		pos += ( size_t ) snprintf(tmp + pos, sizeof(tmp) - pos, "overload=%s", overload_name);
	}
	snprintf(tmp + pos, sizeof(tmp) - pos, ")");
	len = strlen(tmp) + 1;
	buf = ( char * ) aden(arena, len);
	if (!buf) return NULL;
	memcpy(buf, tmp, len);
	return buf;
}

char *cwinrt_name_class_new(char const *ns_prefix, char const *winrt_ns, char const *type_short, int arena) {
	char const *abbrev;
	size_t      len;
	char       *buf;

	if (!ns_prefix || !type_short) return NULL;
	abbrev = cwinrt_name_type_abbrev(winrt_ns, type_short);
	len    = strlen(ns_prefix) + strlen(abbrev) + 8;
	buf    = ( char * ) aden(arena, len);
	if (!buf) return NULL;
	snprintf(buf, len, "%s_%s_new", ns_prefix, abbrev);
	return buf;
}
