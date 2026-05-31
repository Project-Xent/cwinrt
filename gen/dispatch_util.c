#include "dispatch_util.h"

#include <stdio.h>
#include <string.h>

static char const *iface_params_after_self(char const *iface_params) {
	char const *p;
	if (!iface_params) return NULL;
	p = strstr(iface_params, " *self");
	if (!p) return iface_params;
	p = strchr(p, ',');
	if (!p) return "";
	p++;
	while (*p == ' ' || *p == '\t') p++;
	return p;
}

/* Drop the trailing parameter identifier; keep generic/type tokens only. */
static size_t param_piece_type_len(char const *piece, size_t len) {
	size_t end     = len;
	size_t last_sp = ( size_t ) -1;
	size_t i;
	for (i = 0; i < len; i++)
		if (piece [i] == ' ' || piece [i] == '\t') last_sp = i;
	if (last_sp == ( size_t ) -1) return len;
	while (end > 0 && (piece [end - 1] == ' ' || piece [end - 1] == '\t')) end--;
	return last_sp;
}

/* Locate the next top-level (generic-depth 0) comma at/after p, else NULL. */
static char const *param_next_comma(char const *p) {
	int depth = 0;
	for (; *p; p++) {
		if (*p == ',' && depth == 0) return p;
		if (*p == '<') depth++;
		if (*p == '>' && depth > 0) depth--;
	}
	return NULL;
}

typedef struct {
	char  *buf;
	size_t cap;
	size_t len;
	bool   first;
} param_writer;

static int append_one_param_type(param_writer *w, char const *p, char const *end) {
	size_t o = w->len;
	size_t tlen;

	while (p < end && (*p == ' ' || *p == '\t')) p++;
	tlen = param_piece_type_len(p, ( size_t ) (end - p));
	if (tlen == 0) return 0;
	if (!w->first) {
		if (o + 2 >= w->cap) return -1;
		w->buf [o++] = ',';
		w->buf [o++] = ' ';
	}
	if (o + tlen >= w->cap) return -1;
	memcpy(w->buf + o, p, tlen);
	w->len   = o + tlen;
	w->first = false;
	return 0;
}

static int append_param_types(char const *params, char *out, size_t outsz) {
	param_writer w = { out, outsz, 0, true };
	char const  *p = params ? params : "";

	while (*p) {
		char const *comma = param_next_comma(p);
		char const *end   = comma ? comma : p + strlen(p);
		if (append_one_param_type(&w, p, end) != 0) return -1;
		p = comma ? comma + 1 : end;
		if (!*p) break;
	}
	if (w.len >= outsz) return -1;
	out [w.len] = '\0';
	return 0;
}

bool cwinrt_params_match_iface_static(char const *class_params, char const *iface_params) {
	char        cls_norm [384];
	char        if_norm [384];
	char const *iface_args;

	if (!iface_params) return false;
	iface_args = iface_params_after_self(iface_params);
	if (!iface_args) return false;
	if (append_param_types(class_params, cls_norm, sizeof(cls_norm)) != 0) return false;
	if (append_param_types(iface_args, if_norm, sizeof(if_norm)) != 0) return false;
	return strcmp(cls_norm, if_norm) == 0;
}

bool cwinrt_params_match_instance(char const *class_params, char const *iface_params) {
	char        cls_norm [384];
	char        if_norm [384];
	char const *cls_args;
	char const *iface_args;

	if (!class_params || !iface_params) return false;
	/* Both are instance signatures with a leading `*self`; compare the arg types
	   after self so overloads (same name, different params) are distinguished. */
	cls_args   = iface_params_after_self(class_params);
	iface_args = iface_params_after_self(iface_params);
	if (!cls_args || !iface_args) return false;
	if (append_param_types(cls_args, cls_norm, sizeof(cls_norm)) != 0) return false;
	if (append_param_types(iface_args, if_norm, sizeof(if_norm)) != 0) return false;
	return strcmp(cls_norm, if_norm) == 0;
}

bool cwinrt_params_has_self(char const *params_c) { return params_c && strstr(params_c, " *self"); }

/* Find the bare argument identifier within [p,end): last whitespace-token, minus leading '*'. */
static char const *param_arg_name_start(char const *p, char const *end) {
	char const *q = end;
	while (q > p && (*q == ' ' || *q == '\t')) q--;
	while (q > p && *q != ' ' && *q != '\t') q--;
	q++;
	while (q < end && (*q == ' ' || *q == '\t')) q++;
	while (q < end && *q == '*') q++;
	return q;
}

static int emit_call_arg(char const *p, char const *end, char *out, size_t outsz, size_t *po) {
	char const *name_start = param_arg_name_start(p, end);
	size_t      o          = *po;

	if (name_start >= end) return -1;
	if (o) {
		if (o + 2 >= outsz) return -1;
		out [o++] = ',';
		out [o++] = ' ';
	}
	while (name_start < end && *name_start != ' ' && *name_start != '\t' && o + 1 < outsz)
		out [o++] = *name_start++;
	out [o] = '\0';
	*po     = o;
	return 0;
}

int  cwinrt_params_to_call_args(char const *params_c, char *out, size_t outsz) {
	char const *p;
	size_t      o = 0;

	if (!out || outsz < 2) return -1;
	if (!params_c || !params_c [0]) {
		out [0] = '\0';
		return 0;
	}
	out [0] = '\0';
	p       = params_c;
	while (*p) {
		char const *comma = param_next_comma(p);
		char const *end   = comma ? comma : p + strlen(p);
		if (emit_call_arg(p, end, out, outsz, &o) != 0) return -1;
		p = comma ? comma + 1 : end;
		if (!*p) break;
	}
	return 0;
}

bool cwinrt_iface_relates_class(char const *iface_short, char const *class_short) {
	size_t      clen;
	char const *rest;

	if (!iface_short || !class_short || !class_short [0]) return false;
	if (iface_short [0] != 'I') return false;
	clen = strlen(class_short);
	if (strncmp(iface_short + 1, class_short, clen) != 0) return false;
	rest = iface_short + 1 + clen;
	if (!rest [0]) return true;
	if (strspn(rest, "0123456789") == strlen(rest)) return true;
	return strstr(rest, "Statics") != NULL || strstr(rest, "Static") != NULL || strstr(rest, "Factory") != NULL;
}

bool cwinrt_iface_is_core_immersive_pattern(char const *iface_short, char const *class_short) {
	char        alt [128];
	size_t      n;
	size_t      alen;
	char const *rest;

	if (!iface_short || !class_short || iface_short [0] != 'I') return false;
	n = strlen(class_short);
	if (n <= 11 || strcmp(class_short + n - 11, "Application") != 0) return false;
	if (snprintf(alt, sizeof(alt), "I%.*sImmersiveApplication", ( int ) (n - 11), class_short) < 0) return false;
	alen = strlen(alt);
	if (strncmp(iface_short, alt, alen) != 0) return false;
	rest = iface_short + alen;
	return !rest [0] || strspn(rest, "0123456789") == strlen(rest);
}

static char const *iface_statics_suffix(char const *iface_short) {
	char const *p;

	if (!iface_short) return NULL;
	p = strstr(iface_short, "Statics");
	if (p) return p;
	return strstr(iface_short, "Static");
}

static size_t common_prefix_len(char const *a, char const *b) {
	size_t i = 0;

	if (!a || !b) return 0;
	while (a [i] && b [i] && a [i] == b [i]) i++;
	return i;
}

bool cwinrt_iface_is_activation_statics_pattern(char const *iface_short, char const *class_short) {
	char        alt [128];
	size_t      n;
	size_t      alen;
	char const *rest;

	if (!iface_short || !class_short || iface_short [0] != 'I') return false;
	n = strlen(class_short);
	if (n <= 11 || strcmp(class_short + n - 11, "Application") != 0) return false;
	if (snprintf(alt, sizeof(alt), "I%.*sActivationStatics", ( int ) (n - 11), class_short) < 0) return false;
	alen = strlen(alt);
	if (strncmp(iface_short, alt, alen) != 0) return false;
	rest = iface_short + alen;
	return !rest [0] || strspn(rest, "0123456789") == strlen(rest);
}

static bool iface_statics_stem_matches_class(char const *iface_short, char const *class_short) {
	char const *st;
	char const *body;
	size_t      stem_len;
	size_t      clen;

	if (!iface_short || !class_short || iface_short [0] != 'I') return false;
	st = iface_statics_suffix(iface_short);
	if (!st) return false;
	body     = iface_short + 1;
	stem_len = ( size_t ) (st - body);
	clen     = strlen(class_short);
	if (strncmp(class_short, body, stem_len) != 0) return false;
	if (class_short [stem_len] == '\0') return true;
	if (class_short [stem_len] == 's' && class_short [stem_len + 1] == '\0') return true;
	if (clen > stem_len + 9 && strcmp(class_short + clen - 9, "Constants") == 0) return true;
	return false;
}

static bool iface_class_name_embedded(char const *iface_short, char const *class_short) {
	if (!iface_short || !class_short || iface_short [0] != 'I') return false;
	if (!iface_statics_suffix(iface_short)) return false;
	return strstr(iface_short + 1, class_short) != NULL;
}

static bool class_is_manager(char const *class_short, size_t clen) {
	return clen > 7 && strcmp(class_short + clen - 7, "Manager") == 0;
}

static bool iface_statics_shared_anchor(char const *iface_short, char const *class_short) {
	size_t clen;
	size_t anchor;

	if (!iface_short || !class_short || iface_short [0] != 'I') return false;
	if (!iface_statics_suffix(iface_short)) return false;
	clen = strlen(class_short);
	if (strcmp(class_short, "MdmPolicy") == 0)
		return strncmp(iface_short + 1, "Mdm", 3) == 0 && strstr(iface_short, "Policy") != NULL;
	if (strcmp(class_short, "SpeechSynthesizer") == 0) return strstr(iface_short, "InstalledVoices") != NULL;
	if (class_is_manager(class_short, clen) && strncmp(iface_short + 1, class_short, 8) == 0 &&
	    strstr(iface_short, "Manager") != NULL)
		return true;
	if (clen < 8) return false;
	anchor = clen < 14 ? clen : 14;
	if (strncmp(iface_short + 1, class_short, anchor) != 0) return false;
	if (class_is_manager(class_short, clen)) return strstr(iface_short, "Manager") != NULL;
	return false;
}

static bool iface_statics_long_prefix(char const *iface_short, char const *class_short) {
	char const *st;
	char const *body;
	size_t      body_len;
	size_t      cp;
	size_t      clen;
	size_t      min_len;

	if (!iface_short || !class_short || iface_short [0] != 'I') return false;
	st = iface_statics_suffix(iface_short);
	if (!st) return false;
	body     = iface_short + 1;
	body_len = ( size_t ) (st - body);
	clen     = strlen(class_short);
	cp       = common_prefix_len(class_short, body);
	if (cp < 12) return false;
	min_len = clen < body_len ? clen : body_len;
	return cp + 4 >= min_len || cp >= 24;
}

typedef bool (*iface_static_pattern_fn)(char const *, char const *);

static iface_static_pattern_fn const k_iface_static_patterns [] = {
	cwinrt_iface_is_core_immersive_pattern,
	cwinrt_iface_is_activation_statics_pattern,
	iface_statics_stem_matches_class,
	iface_class_name_embedded,
	iface_statics_shared_anchor,
	iface_statics_long_prefix,
};

/* "Class" + "rest" names an existing raw class type -> the iface is its instance face, not a static one. */
static bool raw_has_class_named(cwinrt_raw_db const *raw, char const *class_short, char const *rest) {
	uint32_t ti;
	char     combined [256];

	if (snprintf(combined, sizeof(combined), "%s%s", class_short, rest) < 0) return false;
	for (ti = 0; ti < raw->type_count; ti++) {
		cwinrt_raw_type const *t = &raw->types [ti];
		if (t->kind != CWINRT_RAW_CLASS || !t->short_name) continue;
		if (strcmp(t->short_name, combined) == 0) return true;
	}
	return false;
}

bool cwinrt_iface_serves_class_static(cwinrt_raw_db const *raw, char const *iface_short, char const *class_short) {
	size_t      clen;
	char const *rest;
	size_t      i;

	if (!iface_short || !class_short || !class_short [0] || iface_short [0] != 'I') return false;
	for (i = 0; i < sizeof(k_iface_static_patterns) / sizeof(k_iface_static_patterns [0]); i++)
		if (k_iface_static_patterns [i](iface_short, class_short)) return true;
	clen = strlen(class_short);
	if (strncmp(iface_short + 1, class_short, clen) != 0) return false;
	rest = iface_short + 1 + clen;
	if (!rest [0] || strspn(rest, "0123456789") == strlen(rest)) return true;
	if (strstr(rest, "Statics") || strstr(rest, "Static") || strstr(rest, "Factory")) return true;
	if (!raw) return true;
	return !raw_has_class_named(raw, class_short, rest);
}

int cwinrt_static_iface_rank(char const *iface_short, char const *class_short) {
	size_t clen;
	if (!iface_short || !class_short) return 100;
	if (strstr(iface_short, "Statics") || strstr(iface_short, "Static") || strstr(iface_short, "Factory")) return 0;
	if (cwinrt_iface_is_activation_statics_pattern(iface_short, class_short)) return 0;
	if (cwinrt_iface_is_core_immersive_pattern(iface_short, class_short)) return 1;
	clen = strlen(class_short);
	if (strncmp(iface_short + 1, class_short, clen) == 0 && !iface_short [1 + clen]) return 3;
	return 2;
}

bool cwinrt_dispatch_iface_is_statics_facade(char const *iface_c_typedef) {
	if (!iface_c_typedef) return false;
	return strstr(iface_c_typedef, "Statics") != NULL
	    || strstr(iface_c_typedef, "Static") != NULL
	    || strstr(iface_c_typedef, "Factory") != NULL;
}

static bool mapped_type_has_method(cwinrt_mapped_type const *t, char const *c_name) {
	uint32_t mi;
	for (mi = 0; mi < t->method_count; mi++)
		if (t->methods [mi].c_name && strcmp(t->methods [mi].c_name, c_name) == 0) return true;
	return false;
}

bool cwinrt_mapped_skip_iface_method_dup(
  cwinrt_mapped_unit const *unit, cwinrt_mapped_type const *t, cwinrt_mapped_method const *m
) {
	uint32_t ti;

	if (!unit || !t || !m || !m->c_name) return false;
	if (t->kind != CWINRT_MAP_IFACE) return false;
	for (ti = 0; ti < unit->type_count; ti++) {
		cwinrt_mapped_type const *ot = &unit->types [ti];
		if (ot->kind != CWINRT_MAP_CLASS) continue;
		if (mapped_type_has_method(ot, m->c_name)) return true;
	}
	return false;
}
