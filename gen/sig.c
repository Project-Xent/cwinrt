#include "sig.h"

#include "name.h"
#include "sig_read.h"
#include "winmd.h"
#include "winmd_int.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Generic argument environment threaded through the recursive reader: decoded
   type slots (VAR/MVAR indices) and their count. */
typedef struct sig_genv {
	char const *const *types;
	uint32_t           count;
} sig_genv;

/* Growable C parameter-list accumulator (NUL-terminated). */
typedef struct sig_outbuf {
	char  *buf;
	size_t cap;
	size_t len;
} sig_outbuf;

static char *sig_strdup(char const *s) {
	size_t n;
	char  *p;
	if (!s) return NULL;
	n = strlen(s) + 1;
	p = ( char * ) malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static int sig_cat(sig_outbuf *o, char const *piece) {
	size_t need;
	size_t plen;
	char  *n;
	if (!piece) return -1;
	plen = strlen(piece);
	need = o->len + plen + 1;
	if (need > o->cap) {
		size_t nc = o->cap ? o->cap * 2 : 128;
		while (nc < need) nc *= 2;
		n = ( char * ) realloc(o->buf, nc);
		if (!n) return -1;
		o->buf = n;
		o->cap = nc;
	}
	memcpy(o->buf + o->len, piece, plen + 1);
	o->len += plen;
	return 0;
}

static int map_primitive(uint8_t elt, char *buf, size_t bufsz) {
	static struct {
		uint8_t     elt;
		char const *c;
	} const prim [] = {
	  {ELT_VOID, "void"},     {ELT_BOOLEAN, "BOOL"}, {ELT_CHAR, "WCHAR"},     {ELT_I1, "int8_t"},
	  {ELT_U1, "uint8_t"},    {ELT_I2, "int16_t"},   {ELT_U2, "uint16_t"},    {ELT_I4, "int32_t"},
	  {ELT_U4, "uint32_t"},   {ELT_I8, "int64_t"},   {ELT_U8, "uint64_t"},    {ELT_R4, "float"},
	  {ELT_R8, "double"},     {ELT_STRING, "cwinrt_hstring"}, {ELT_OBJECT, "IInspectable*"},
	};
	size_t k;
	for (k = 0; k < sizeof(prim) / sizeof(prim [0]); k++)
		if (prim [k].elt == elt) {
			snprintf(buf, bufsz, "%s", prim [k].c);
			return 0;
		}
	snprintf(buf, bufsz, "void*");
	return 0;
}

static void sig_coerce_unresolved_type(char *buf, size_t bufsz) {
	if (!buf || bufsz < 6) return;
	if (strcmp(buf, "IInspectable*") == 0) return;
	/* A bare `void` param type is an unresolved/degraded type — pass as void*.
	   (Do NOT coerce uint8_t: ELEMENT_TYPE_U1 is a real Byte parameter, e.g.
	   PropertyValue.CreateUInt8, DataWriter.WriteByte, audio-effect bytes.) */
	if (strcmp(buf, "void") == 0) snprintf(buf, bufsz, "void*");
}

/* WinRT well-known types that project to a fixed ABI C type rather than the
   generated struct/interface name. */
static char const *named_type_alias(char const *full) {
	static struct {
		char const *full;
		char const *c;
	} const alias [] = {
	  {"System.Guid", "GUID"},
	  {"Windows.Foundation.HResult", "HRESULT"},
	  {"Windows.Foundation.EventRegistrationToken", "uint64_t"},
	  {"System.String", "cwinrt_hstring"},
	  {"System.Object", "IInspectable*"},
	  {"Windows.Foundation.IAsyncAction", "WF_IAsyncAction"},
	  {"Windows.Foundation.IAsyncInfo", "WF_IAsyncInfo"},
	  {"Windows.Graphics.SizeInt32", "WG_SizeInt32"},
	  {"Windows.Graphics.PointInt32", "WG_PointInt32"},
	  {"Windows.Graphics.RectInt32", "WG_RectInt32"},
	  {"Windows.Foundation.Point", "WF_Point"},
	  {"Windows.Foundation.Rect", "WF_Rect"},
	};
	size_t k;
	for (k = 0; k < sizeof(alias) / sizeof(alias [0]); k++)
		if (strcmp(full, alias [k].full) == 0) return alias [k].c;
	return NULL;
}

static int map_named_type(winmd_meta const *m, uint32_t coded, char *buf, size_t bufsz) {
	char        full [256];
	char const *c;
	if (winmd_coded_type_full_name(m, coded, full, sizeof(full)) != 0) {
		snprintf(buf, bufsz, "void*");
		return 0;
	}
	c = named_type_alias(full);
	if (c) {
		snprintf(buf, bufsz, "%s", c);
		return 0;
	}
	if (winmd_type_full_name(m, coded, buf, bufsz) != 0) snprintf(buf, bufsz, "void*");
	return 0;
}

/* WinRT open generic projection: WF_IAsyncOperation_1, WFOCO_IIterator_1, etc. */
static bool type_is_open_winrt_generic(char const *name) {
	size_t n;
	size_t i;
	if (!name || !name [0]) return false;
	n = strlen(name);
	while (n > 0 && name [n - 1] == '*') n--;
	if (n < 3) return false;
	i = n;
	while (i > 0 && name [i - 1] >= '0' && name [i - 1] <= '9') i--;
	return i > 0 && i < n && name [i - 1] == '_';
}

static void strip_trailing_pointer(char *name) {
	size_t n;
	if (!name) return;
	n = strlen(name);
	if (n > 0 && name [n - 1] == '*') name [n - 1] = '\0';
}

/* Identifier-safe mangle of a C type for a generic instantiation name. void and
   float keywords, and ABI typedefs, get readable aliases. */
static void sig_mangle_generic_arg(char const *type, char *out, size_t cap) {
	static struct {
		char const *c;
		char const *id;
	} const alias [] = {
	  {"void", "Void"},      {"BOOL", "Bool"},       {"WCHAR", "Char16"}, {"float", "F32"}, {"double", "F64"},
	  {"cwinrt_hstring", "HSTRING"}, {"IInspectable", "Object"}, {"GUID", "Guid"},    {"HRESULT", "HResult"},
	};
	char   buf [256];
	size_t k;

	if (!out || cap < 2) return;
	if (!type || !type [0]) {
		snprintf(out, cap, "Void");
		return;
	}
	snprintf(buf, sizeof(buf), "%s", type);
	strip_trailing_pointer(buf);
	for (k = 0; k < sizeof(alias) / sizeof(alias [0]); k++)
		if (strcmp(buf, alias [k].c) == 0) {
			snprintf(out, cap, "%s", alias [k].id);
			return;
		}
	snprintf(out, cap, "%s", buf);
}

/* WF_IAsyncOperation_1 -> WF_IAsyncOperation before appending type args. */
static void strip_generic_arity_suffix(char *name) {
	size_t n;
	size_t i;
	if (!name || !name [0]) return;
	n = strlen(name);
	i = n;
	while (i > 0 && name [i - 1] >= '0' && name [i - 1] <= '9') i--;
	if (i > 0 && i < n && name [i - 1] == '_') name [i - 1] = '\0';
}

static bool sig_type_is_array_marked(char const *t, char *elem, size_t elem_cap) {
	size_t n;
	if (!t || !elem || elem_cap < 2) return false;
	n = strlen(t);
	if (n < 3 || t [n - 1] != ']' || t [n - 2] != '[') return false;
	{
		size_t elen = n - 2;
		if (elen >= elem_cap) elen = elem_cap - 1;
		memcpy(elem, t, elen);
		elem [elen] = '\0';
	}
	return true;
}

static int sig_format_param(char *chunk, size_t chunk_cap, char const *ptype, char const *pname, bool byref) {
	char elem [128];
	char len_name [80];

	if (!chunk || chunk_cap < 8 || !ptype || !pname) return -1;
	if (sig_type_is_array_marked(ptype, elem, sizeof(elem))) {
		snprintf(len_name, sizeof(len_name), "%s_len", pname);
		snprintf(chunk, chunk_cap, "%s *%s, uint32_t %s", elem, pname, len_name);
		return 0;
	}
	if (byref) snprintf(chunk, chunk_cap, "%s* %s", ptype, pname);
	else snprintf(chunk, chunk_cap, "%s %s", ptype, pname);
	return 0;
}

/* Recursive type-decode state: blob cursor, metadata, and the generic-arg
   environment. Constant through one signature's decode. */
typedef struct sig_dec {
	sig_reader     *r;
	winmd_meta const *m;
	sig_genv const *gv;
} sig_dec;

static int read_type_ex(sig_dec *d, char *buf, size_t bufsz, bool *byref);

/* ELEMENT_TYPE_VAR / _MVAR: a generic parameter, resolved via gv->types. */
static int read_generic_param(sig_dec *d, char *buf, size_t bufsz) {
	sig_genv const *gv = d->gv;
	uint32_t        ix;
	if (sig_read_compressed(d->r, &ix) != 0) return -1;
	if (gv->types && ix < gv->count && gv->types [ix] && gv->types [ix][0]) snprintf(buf, bufsz, "%s", gv->types [ix]);
	else snprintf(buf, bufsz, "void*");
	return 0;
}

/* ELEMENT_TYPE_VALUETYPE / _CLASS: a coded TypeDefOrRef. Enums use their full
   name; classes get a trailing '*'. */
static int read_named_type(sig_dec *d, uint8_t elt, char *buf, size_t bufsz) {
	winmd_meta const *m = d->m;
	uint32_t          tok;
	if (sig_read_compressed(d->r, &tok) != 0) return -1;
	if (elt == ELT_VALUETYPE && winmd_coded_typedef_is_enum(m, tok)) {
		if (winmd_type_full_name(m, tok, buf, bufsz) != 0) snprintf(buf, bufsz, "int32_t");
	}
	else map_named_type(m, tok, buf, bufsz);
	if (elt == ELT_CLASS && bufsz > 2) {
		size_t n = strlen(buf);
		if (n + 2 < bufsz && strchr(buf, '*') == NULL) {
			buf [n]     = '*';
			buf [n + 1] = '\0';
		}
	}
	return 0;
}

static int skip_generic_args(sig_dec *d, uint32_t from, uint32_t argc) {
	uint32_t i;
	for (i = from; i < argc; i++) {
		char skip [256];
		if (read_type_ex(d, skip, sizeof(skip), NULL) != 0) return -1;
	}
	return 0;
}

/* Mangle `argc` type arguments into `args` joined by '_' (identifier-safe). */
static int mangle_generic_args(sig_dec *d, uint32_t argc, char *args, size_t cap) {
	uint32_t i;
	size_t   alen = 0;
	args [0]      = '\0';
	for (i = 0; i < argc; i++) {
		char arg [256];
		char tag [256];
		int  n;
		if (read_type_ex(d, arg, sizeof(arg), NULL) != 0) return -1;
		sig_mangle_generic_arg(arg, tag, sizeof(tag));
		if (i > 0 && alen + 1 < cap) args [alen++] = '_';
		if (alen + 1 >= cap) break;
		n = snprintf(args + alen, cap - alen, "%s", tag);
		if (n < 0) return -1;
		alen += ( size_t ) n;
		if (alen >= cap - 1) break;
	}
	return 0;
}

/* IReference<T> projects to T* (the boxed value's pointer); remaining generic
   args are skipped. */
static int read_genericinst_ireference(sig_dec *d, char *buf, size_t bufsz, uint32_t argc) {
	char ref_inner [256];
	if (read_type_ex(d, ref_inner, sizeof(ref_inner), NULL) != 0) return -1;
	strip_trailing_pointer(ref_inner);
	snprintf(buf, bufsz, "%s*", ref_inner);
	return skip_generic_args(d, 1, argc);
}

/* Project a resolved closed generic to its mangled C wrapper name
   (Inner_Arg0_Arg1...*). `inner` is consumed/modified. */
static int read_genericinst_mangled(sig_dec *d, char *buf, size_t bufsz, char *inner, uint32_t argc) {
	char   args [512];
	size_t n;
	strip_trailing_pointer(inner);
	strip_generic_arity_suffix(inner);
	if (mangle_generic_args(d, argc, args, sizeof(args)) != 0) return -1;
	if (argc > 0) snprintf(buf, bufsz, "%s_%s", inner, args); /* snprintf truncates to bufsz safely */
	else snprintf(buf, bufsz, "%s", inner);
	n = strlen(buf);
	if (n > 0 && buf [n - 1] != '*' && bufsz > n + 2) {
		buf [n]     = '*';
		buf [n + 1] = '\0';
	}
	return 0;
}

/* ELEMENT_TYPE_GENERICINST: project a closed generic to its mangled C wrapper
   name (IReference<T> -> T*; unresolved/object -> void*). */
static int read_genericinst(sig_dec *d, char *buf, size_t bufsz) {
	char     inner [256];
	uint32_t argc;

	if (read_type_ex(d, inner, sizeof(inner), NULL) != 0) return -1;
	if (sig_read_compressed(d->r, &argc) != 0) return -1;
	if (strstr(inner, "IReference") != NULL && argc >= 1) return read_genericinst_ireference(d, buf, bufsz, argc);
	if (strcmp(inner, "void*") == 0 || strcmp(inner, "IInspectable*") == 0) {
		if (skip_generic_args(d, 0, argc) != 0) return -1;
		snprintf(buf, bufsz, "void*");
		return 0;
	}
	if (type_is_open_winrt_generic(inner) && argc == 0) {
		snprintf(buf, bufsz, "%s", inner);
		return 0;
	}
	return read_genericinst_mangled(d, buf, bufsz, inner, argc);
}

/* ELEMENT_TYPE_SZARRAY: the element type followed by "[]". */
static int read_szarray(sig_dec *d, char *buf, size_t bufsz) {
	char inner [256];
	if (read_type_ex(d, inner, sizeof(inner), NULL) != 0) return -1;
	strip_trailing_pointer(inner);
	snprintf(buf, bufsz, "%s[]", inner);
	return 0;
}

static int read_type_ex(sig_dec *d, char *buf, size_t bufsz, bool *byref) {
	uint8_t elt;
	if (byref) *byref = false;
	if (sig_read_u8(d->r, &elt) != 0) return -1;
	if (elt == ELT_BYREF) {
		if (byref) *byref = true;
		return read_type_ex(d, buf, bufsz, byref);
	}
	if (elt == ELT_VAR || elt == ELT_MVAR) return read_generic_param(d, buf, bufsz);
	if (elt == ELT_VALUETYPE || elt == ELT_CLASS) return read_named_type(d, elt, buf, bufsz);
	if (elt == ELT_SZARRAY) return read_szarray(d, buf, bufsz);
	if (elt == ELT_GENERICINST) return read_genericinst(d, buf, bufsz);
	return map_primitive(elt, buf, bufsz);
}

static int read_type(sig_reader *r, winmd_meta const *m, char *buf, size_t bufsz, bool *byref) {
	sig_genv const gv = {NULL, 0};
	sig_dec        d  = {r, m, &gv};
	return read_type_ex(&d, buf, bufsz, byref);
}

int winmd_parse_type_blob(winmd_meta const *m, uint32_t blob_ix, char *buf, size_t bufsz) {
	sig_reader sr;
	if (!m || !buf || bufsz < 2) return -1;
	memset(&sr, 0, sizeof(sr));
	if (sig_reader_from_blob(m, blob_ix, &sr) != 0) return -1;
	return read_type(&sr, m, buf, bufsz, NULL);
}

/* FieldSig (ECMA-335 II.23.2.4): FIELD(0x06) CustomMod* Type. The leading 0x06
   calling-convention byte aliases ELEMENT_TYPE_I2, so it must be consumed before
   reading the field type, otherwise every field decodes as int16. */
int winmd_parse_field_blob(winmd_meta const *m, uint32_t blob_ix, char *buf, size_t bufsz) {
	sig_reader sr;
	uint8_t    cc;
	if (!m || !buf || bufsz < 2) return -1;
	memset(&sr, 0, sizeof(sr));
	if (sig_reader_from_blob(m, blob_ix, &sr) != 0) return -1;
	if (sig_read_u8(&sr, &cc) != 0) return -1; /* FIELD calling convention (0x06) */
	return read_type(&sr, m, buf, bufsz, NULL);
}

static bool        is_prop_get(char const *name) { return name && strncmp(name, "get_", 4) == 0; }

static bool        is_prop_put(char const *name) { return name && strncmp(name, "put_", 4) == 0; }

static bool        type_is_void(char const *t) { return !t || strcmp(t, "void") == 0; }

static char const *param_name_at(char const *const *names, uint32_t count, uint32_t sig_index) {
	if (!names || sig_index >= count) return NULL;
	if (!names [sig_index] || !names [sig_index][0]) return NULL;
	return names [sig_index];
}

static bool param_name_reserved(char const *name) {
	return !name || !name [0] || strcmp(name, "retval") == 0 || strcmp(name, "out") == 0 || strcmp(name, "value") == 0;
}

static void sig_sanitize_param_name(char *dst, size_t cap, char const *src, uint32_t fallback_ix) {
	if (!dst || cap < 2) return;
	if (!src || !src [0]) snprintf(dst, cap, "p%u", ( unsigned ) (fallback_ix + 1));
	else cwinrt_name_sanitize_param_ident(dst, src, cap);
}

/* Param table names with no matching blob entry (WinRT ABI quirk). */
static int resolve_extra_param_type(winmd_meta const *m, char const *name, char *buf, size_t bufsz) {
	( void ) m;
	if (!name || !buf || bufsz < 2) return -1;
	if (strcmp(name, "sdrBoost") == 0) {
		snprintf(buf, bufsz, "float");
		return 0;
	}
	if (strcmp(name, "color") == 0) {
		snprintf(buf, bufsz, "WUI_Color");
		return 0;
	}
	return -1;
}

/* Emit one Param-table-only parameter (no matching blob entry). Returns 1 when
   the name is reserved and was skipped, 0 on success, -1 on error. */
static int append_one_extra_param(sig_outbuf *o, winmd_meta const *m, char const *pn, uint32_t ni) {
	char ptype [128];
	char pname [64];
	char pchunk [256];

	if (param_name_reserved(pn)) return 1;
	ptype [0] = '\0';
	if (resolve_extra_param_type(m, pn, ptype, sizeof(ptype)) != 0) snprintf(ptype, sizeof(ptype), "void*");
	sig_sanitize_param_name(pname, sizeof(pname), pn, ni);
	if (o->len && sig_cat(o, ", ") != 0) return -1;
	snprintf(pchunk, sizeof(pchunk), "%s %s", ptype, pname);
	return sig_cat(o, pchunk);
}

static int append_extra_param_from_names(
  sig_outbuf *o, winmd_meta const *m, char const *const *param_names, uint32_t param_name_count,
  uint32_t blob_user_count
) {
	uint32_t ni;
	for (ni = blob_user_count; ni < param_name_count; ni++) {
		char const *pn = param_name_at(param_names, param_name_count, ni);
		if (append_one_extra_param(o, m, pn, ni) < 0) return -1;
	}
	return 0;
}

#define SIG_GEN_SLOTS  16u
#define SIG_GEN_STRIDE 512u
#define SIG_TYPE_BUFSZ 1024u

static char *sig_gen_slot(char *gen_buf, uint32_t ix) { return gen_buf + ( size_t ) ix * SIG_GEN_STRIDE; }

/* Shared state for emitting the C parameter list: metadata, the generic-arg
   environment, the Param-table names, the scratch type buffer (SIG_TYPE_BUFSZ),
   and the output accumulator. */
typedef struct sig_emit {
	winmd_meta const        *m;
	sig_genv const          *gv;
	winmd_param_names const *pnames;
	char                    *ptype;
	sig_outbuf              *o;
} sig_emit;

/* Read the MethodDefSig header: calling convention then, if GENERIC, the
   generic argument list. Fills gen_ptrs[] (decoded slots, padded with empty
   slots) and stores the generic arg count in *gen. */
static int sig_read_gen_slots(sig_reader *sr, winmd_meta const *m, char *gen_buf, char const **gen_ptrs, uint32_t gen) {
	sig_genv const empty = {NULL, 0};
	sig_dec        d     = {sr, m, &empty};
	uint32_t       i;
	for (i = 0; i < gen; i++) {
		char *slot   = sig_gen_slot(gen_buf, i);
		slot [0]     = '\0';
		gen_ptrs [i] = slot;
		if (read_type_ex(&d, slot, SIG_GEN_STRIDE, NULL) != 0) return -1;
	}
	return 0;
}

static int sig_read_gen_header(sig_reader *sr, winmd_meta const *m, char *gen_buf, char const **gen_ptrs, uint32_t *gen) {
	uint8_t  conv;
	uint32_t i;
	*gen = 0;
	if (sig_read_u8(sr, &conv) != 0) return -1;
	if (conv & 0x10) {
		if (sig_read_compressed(sr, gen) != 0) return -1;
		if (*gen > SIG_GEN_SLOTS) *gen = SIG_GEN_SLOTS;
		if (sig_read_gen_slots(sr, m, gen_buf, gen_ptrs, *gen) != 0) return -1;
	}
	for (i = *gen; i < SIG_GEN_SLOTS; i++) gen_ptrs [i] = sig_gen_slot(gen_buf, i);
	return 0;
}

/* Append one already-decoded parameter (`ptype`, size SIG_TYPE_BUFSZ) to the C
   parameter list. A generic items/values param degrades to the element array. */
static int sig_append_param(sig_emit *e, char const *pn, bool byref, uint32_t i) {
	char     *ptype = e->ptype;
	sig_genv const *gv = e->gv;
	char      pname [64];
	char      pchunk [512];
	if (gv->count > 0 && gv->types [0][0] && strcmp(ptype, "void*") == 0 && pn
	    && (strcmp(pn, "items") == 0 || strcmp(pn, "values") == 0))
		snprintf(ptype, SIG_TYPE_BUFSZ, "%s[]", gv->types [0]);
	sig_coerce_unresolved_type(ptype, SIG_TYPE_BUFSZ);
	if (e->o->len && sig_cat(e->o, ", ") != 0) return -1;
	sig_sanitize_param_name(pname, sizeof(pname), pn, i);
	if (sig_format_param(pchunk, sizeof(pchunk), ptype, pname, byref) != 0) return -1;
	return sig_cat(e->o, pchunk);
}

/* Append the WinRT [out] retval as a trailing pointer parameter named `outn`. */
static int sig_append_retval(sig_outbuf *o, char const *ret_type, char const *outn) {
	char   elem [512];
	char   outp [1024];
	size_t rn = strlen(ret_type);
	if (o->len && sig_cat(o, ", ") != 0) return -1;
	if (sig_type_is_array_marked(ret_type, elem, sizeof(elem)))
		snprintf(outp, sizeof(outp), "%s **%s, uint32_t *%s_len", elem, outn, outn);
	else if (rn > 0 && ret_type [rn - 1] == '*') {
		char   base [512];
		size_t blen = rn - 1;
		if (blen >= sizeof(base)) blen = sizeof(base) - 1;
		memcpy(base, ret_type, blen);
		base [blen] = '\0';
		snprintf(outp, sizeof(outp), "%s** %s", base, outn);
	}
	else snprintf(outp, sizeof(outp), "%s *%s", ret_type, outn);
	return sig_cat(o, outp);
}

/* Resolve out->ret_c_type from the decoded return type, appending the WinRT
   [out]/[value] retval parameter when the method actually returns a value.
   Property setters always project as returning void. */
static int sig_finish_retval(winmd_method_sig *out, char const *method_name, char const *ret_type, sig_outbuf *o) {
	out->ret_c_type = sig_strdup(ret_type);
	if (!out->ret_c_type) return -1;
	if (is_prop_put(method_name)) {
		free(out->ret_c_type);
		out->ret_c_type = sig_strdup("void");
	}
	else if (!type_is_void(ret_type)) {
		if (sig_append_retval(o, ret_type, is_prop_get(method_name) ? "value" : "out") != 0) return -1;
		free(out->ret_c_type);
		out->ret_c_type = sig_strdup("void");
	}
	if (!out->ret_c_type) out->ret_c_type = sig_strdup("void");
	return 0;
}

/* Emit the implicit `this` parameter. ECMA-335 ParamCount excludes this and the
   blob carries no this slot, so it is synthesized here for instance methods. */
static int sig_emit_self_param(sig_outbuf *o, char const *self_c_type, bool is_static) {
	if (is_static || !self_c_type) return 0;
	if (sig_cat(o, self_c_type) != 0) return -1;
	return sig_cat(o, " *self");
}

/* Decode and emit the `nparam` explicit parameters carried in the blob. */
static int sig_emit_blob_params(sig_emit *e, sig_reader *sr, uint32_t nparam) {
	sig_dec  d = {sr, e->m, e->gv};
	uint32_t i;
	for (i = 0; i < nparam; i++) {
		bool        byref = false;
		char const *pn;
		if (read_type_ex(&d, e->ptype, SIG_TYPE_BUFSZ, &byref) != 0) return -1;
		pn = param_name_at(e->pnames->names, e->pnames->count, i);
		if (sig_append_param(e, pn, byref, i) != 0) return -1;
	}
	return 0;
}

int          winmd_parse_sig(
  winmd_db const *db, uint32_t blob_ix, winmd_method_info const *info, winmd_param_names const *pnames,
  winmd_method_sig *out
) {
	winmd_meta const *m;
	sig_reader        sr;
	uint32_t          gen = 0;
	uint32_t          nparam;
	char              ret_type [SIG_TYPE_BUFSZ];
	char             *gen_buf = NULL;
	char const       *gen_ptrs [SIG_GEN_SLOTS];
	char             *ptype     = NULL;
	sig_outbuf        params    = {NULL, 0, 0};
	bool              is_static = (info->method_flags & 0x0010) != 0;
	sig_genv          gv;
	sig_emit          e;

	if (!db || !out || !db->meta) return -1;
	memset(out, 0, sizeof(*out));
	m = db->meta;
	memset(&sr, 0, sizeof(sr));
	gen_buf = ( char * ) calloc(SIG_GEN_SLOTS, SIG_GEN_STRIDE);
	ptype   = ( char * ) malloc(SIG_TYPE_BUFSZ);
	if (!gen_buf || !ptype) goto fail;
	if (sig_reader_from_blob(m, blob_ix, &sr) != 0) goto fail;

	if (sig_read_gen_header(&sr, m, gen_buf, gen_ptrs, &gen) != 0) goto fail;
	gv.types = gen_ptrs;
	gv.count = gen;
	if (sig_read_compressed(&sr, &nparam) != 0) goto fail;
	{
		sig_dec d = {&sr, m, &gv};
		if (read_type_ex(&d, ret_type, sizeof(ret_type), NULL) != 0) goto fail;
	}
	/* Do not coerce void return to void* — that wrongly adds void** retval. */

	e.m      = m;
	e.gv     = &gv;
	e.pnames = pnames;
	e.ptype  = ptype;
	e.o      = &params;
	if (sig_emit_self_param(&params, info->self_c_type, is_static) != 0) goto fail;
	if (sig_emit_blob_params(&e, &sr, nparam) != 0) goto fail;
	if (append_extra_param_from_names(&params, m, pnames->names, pnames->count, nparam) != 0) goto fail;

	if (sig_finish_retval(out, info->method_name, ret_type, &params) != 0) goto fail;
	out->params_c = params.buf;
	free(gen_buf);
	free(ptype);
	return 0;

fail:
	free(params.buf);
	free(gen_buf);
	free(ptype);
	free(out->ret_c_type);
	memset(out, 0, sizeof(*out));
	return -1;
}

void winmd_sig_free(winmd_method_sig *sig) {
	if (!sig) return;
	free(sig->ret_c_type);
	free(sig->params_c);
	memset(sig, 0, sizeof(*sig));
}
