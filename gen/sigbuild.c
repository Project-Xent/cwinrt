#include "sigbuild.h"

#include "piid.h"
#include "winmd_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	ELT_BOOLEAN     = 0x02,
	ELT_R8          = 0x0d,
	ELT_STRING      = 0x0e,
	ELT_PTR         = 0x0f,
	ELT_BYREF       = 0x10,
	ELT_VALUETYPE   = 0x11,
	ELT_CLASS       = 0x12,
	ELT_VAR         = 0x13,
	ELT_GENERICINST = 0x15,
	ELT_OBJECT      = 0x1c,
	ELT_SZARRAY     = 0x1d,
	ELT_FIELD       = 0x06,
};

#define SB_MAX_DEPTH 24

static int sb_read_u8(uint8_t const **p, uint8_t const *end, uint8_t *v) {
	if (*p >= end) return -1;
	*v = *(*p)++;
	return 0;
}

static int sb_read_compressed(uint8_t const **p, uint8_t const *end, uint32_t *v) {
	uint8_t b0;
	if (sb_read_u8(p, end, &b0) != 0) return -1;
	if ((b0 & 0x80u) == 0) {
		*v = b0;
		return 0;
	}
	if ((b0 & 0xc0u) == 0x80u) {
		uint8_t b1;
		if (sb_read_u8(p, end, &b1) != 0) return -1;
		*v = (( uint32_t ) (b0 & 0x3fu) << 8) | b1;
		return 0;
	}
	{
		uint8_t b1, b2, b3;
		if (sb_read_u8(p, end, &b1) != 0 || sb_read_u8(p, end, &b2) != 0 || sb_read_u8(p, end, &b3) != 0) return -1;
		*v = (( uint32_t ) (b0 & 0x1fu) << 24) | (( uint32_t ) b1 << 16) | (( uint32_t ) b2 << 8) | b3;
	}
	return 0;
}

static int sb_typedef_full_name(winmd_db const *db, uint32_t row1, char *out, size_t cap) {
	char const *ns;
	char const *name;
	if (!row1 || row1 > db->typedef_count) return -1;
	ns   = db->typedefs [row1 - 1].namespace_name;
	name = db->typedefs [row1 - 1].name;
	if (!name) return -1;
	if (ns && ns [0]) snprintf(out, cap, "%s.%s", ns, name);
	else snprintf(out, cap, "%s", name);
	return 0;
}

/* WinRT sig for the "value__" backing field's element type, else "i4". */
static char const *sb_enum_backing_sig(winmd_db const *db, winmd_field_info const *f) {
	uint32_t       blen = 0;
	uint8_t const *bp   = winmd_blob_at(db->meta, f->sig_blob, &blen);
	char const    *s;
	if (!bp || blen < 2 || bp [0] != ELT_FIELD) return "i4";
	s = cwinrt_piid_basic_sig(bp [1]);
	return s ? s : "i4";
}

static int sb_enum_underlying(winmd_db const *db, uint32_t row1, char const **sig_out) {
	winmd_field_info *fields = NULL;
	uint32_t          n      = 0;
	uint32_t          i;
	char const       *res = "i4"; /* default Int32 */

	if (winmd_typedef_fields(db->meta, row1, &fields, &n) != 0) return -1;
	/* the backing field is the non-literal instance field "value__" */
	for (i = 0; i < n; i++) {
		if (!fields [i].name || strcmp(fields [i].name, "value__") != 0) continue;
		res = sb_enum_backing_sig(db, &fields [i]);
		break;
	}
	winmd_field_info_free(fields, n);
	*sig_out = res;
	return 0;
}

static int sb_type_depth(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, int depth);
static int sb_typedef_depth(winmd_db const *db, uint32_t row1, char *out, size_t cap, int depth);

/* Pointer to a TypeSpec row's signature blob (past the length prefix). */
static int sb_typespec_blob(winmd_db const *db, uint32_t typespec_row1, uint8_t const **bp, uint32_t *len) {
	*bp = winmd_blob_at(db->meta, winmd_typespec_sig_blob(db->meta, typespec_row1), len);
	return (*bp && *len) ? 0 : -1;
}

/* one struct field: FieldSig = 0x06 <Type> */
static int sb_field_sig(winmd_db const *db, uint32_t sig_blob, char *out, size_t cap, int depth) {
	uint32_t       blen = 0;
	uint8_t const *bp   = winmd_blob_at(db->meta, sig_blob, &blen);
	uint8_t const *p;
	uint8_t const *e;
	if (!bp || blen < 2) return -1;
	p = bp;
	e = bp + blen;
	if (*p == ELT_FIELD) p++;
	return sb_type_depth(db, &p, e, out, cap, depth);
}

/* enum(NS.Name;<underlying>) */
static int sb_enum_sig(winmd_db const *db, uint32_t row1, char const *full, char *out, size_t cap) {
	char const *u = "i4";
	sb_enum_underlying(db, row1, &u);
	return ( size_t ) snprintf(out, cap, "enum(%s;%s)", full, u) >= cap ? -1 : 0;
}

/* Append ";<field-sig>" at *pos; returns -1 on overflow/decode failure. */
static int sb_struct_append_field(
  winmd_db const *db, winmd_field_info const *f, char *out, size_t cap, size_t *pos, int depth
) {
	char fsig [1024];
	if (sb_field_sig(db, f->sig_blob, fsig, sizeof(fsig), depth + 1) != 0) return -1;
	if (*pos + 1 < cap) out [(*pos)++] = ';';
	*pos += ( size_t ) snprintf(out + *pos, cap - *pos, "%s", fsig);
	return *pos >= cap ? -1 : 0;
}

/* struct(NS.Name;f1;f2;...) */
static int sb_struct_sig(winmd_db const *db, uint32_t row1, char const *full, char *out, size_t cap, int depth) {
	winmd_field_info *fields = NULL;
	uint32_t          n      = 0;
	uint32_t          i;
	size_t            pos;
	int               rc = 0;

	pos = ( size_t ) snprintf(out, cap, "struct(%s", full);
	if (pos >= cap) return -1;
	if (winmd_typedef_fields(db->meta, row1, &fields, &n) != 0) return -1;
	for (i = 0; i < n && rc == 0; i++) {
		if (fields [i].name && strcmp(fields [i].name, "value__") == 0) continue; /* defensive: enum guard */
		rc = sb_struct_append_field(db, &fields [i], out, cap, &pos, depth);
	}
	winmd_field_info_free(fields, n);
	if (rc != 0 || pos + 2 >= cap) return -1;
	out [pos++] = ')';
	out [pos]   = '\0';
	return 0;
}

/* {guid} for an interface, delegate({guid}) for a delegate (wrap chooses). */
static int sb_guid_sig(winmd_db const *db, uint32_t row1, char const *fmt, char *out, size_t cap) {
	uint8_t g [16];
	char    gstr [48];
	if (winmd_typedef_uuid(db->meta, row1, g) != 0) return -1;
	cwinrt_piid_guid_str(g, gstr, sizeof(gstr));
	return ( size_t ) snprintf(out, cap, fmt, gstr) >= cap ? -1 : 0;
}

/* rc(NS.Name;<default-interface-sig>) — default iface may be a closed-generic TypeSpec. */
static int sb_rc_sig(winmd_db const *db, uint32_t row1, char const *full, char *out, size_t cap, int depth) {
	uint32_t di = winmd_typedef_default_iface(db->meta, row1);
	uint32_t di_row = 0;
	char     disig [2048];

	if (!di) return -1;
	if (winmd_coded_to_typedef_row(db, db->meta, di, &di_row) == 0) {
		if (sb_typedef_depth(db, di_row, disig, sizeof(disig), depth + 1) != 0) return -1;
	}
	else { /* TypeSpec (closed generic) default interface */
		uint8_t const *bp;
		uint32_t       blen = 0;
		if ((di & 3u) != 2u || sb_typespec_blob(db, di >> 2, &bp, &blen) != 0) return -1;
		if (sb_type_depth(db, &bp, bp + blen, disig, sizeof(disig), depth + 1) != 0) return -1;
	}
	return ( size_t ) snprintf(out, cap, "rc(%s;%s)", full, disig) >= cap ? -1 : 0;
}

static int sb_typedef_depth(winmd_db const *db, uint32_t row1, char *out, size_t cap, int depth) {
	char full [256];

	if (depth > SB_MAX_DEPTH) return -1;
	if (sb_typedef_full_name(db, row1, full, sizeof(full)) != 0) return -1;
	switch (winmd_typedef_classify(db->meta, row1)) {
	case WINMD_KIND_ENUM     : return sb_enum_sig(db, row1, full, out, cap);
	case WINMD_KIND_STRUCT   : return sb_struct_sig(db, row1, full, out, cap, depth);
	case WINMD_KIND_INTERFACE: return sb_guid_sig(db, row1, "%s", out, cap);
	case WINMD_KIND_DELEGATE : return sb_guid_sig(db, row1, "delegate(%s)", out, cap);
	case WINMD_KIND_CLASS    : return sb_rc_sig(db, row1, full, out, cap, depth);
	default                  : return -1;
	}
}

/* ELEMENT_TYPE_VALUETYPE / ELEMENT_TYPE_CLASS: a coded TypeDefOrRef token. */
static int sb_named_type(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, int depth) {
	uint32_t coded;
	uint32_t row = 0;
	char     full [256];
	if (sb_read_compressed(p, end, &coded) != 0) return -1;
	/* System.* references aren't TypeDefs in Windows.winmd; map by name. */
	if (winmd_coded_type_full_name(db->meta, coded, full, sizeof(full)) == 0) {
		if (strcmp(full, "System.Guid") == 0) return ( size_t ) snprintf(out, cap, "g16") >= cap ? -1 : 0;
		if (strcmp(full, "System.Object") == 0)
			return ( size_t ) snprintf(out, cap, "cinterface(IInspectable)") >= cap ? -1 : 0;
	}
	if (winmd_coded_to_typedef_row(db, db->meta, coded, &row) != 0) return -1;
	return sb_typedef_depth(db, row, out, cap, depth + 1);
}

/* Parse a GenericInst header: <CLASS|VALUETYPE> <open-token> <argc>.
   Yields the open type's GUID and arg count (1..8). */
static int sb_generic_header(
  winmd_db const *db, uint8_t const **p, uint8_t const *end, uint8_t og [16], uint32_t *argc
) {
	uint8_t  inner;
	uint32_t coded, open_row = 0;
	if (sb_read_u8(p, end, &inner) != 0 || (inner != ELT_CLASS && inner != ELT_VALUETYPE)) return -1;
	if (sb_read_compressed(p, end, &coded) != 0) return -1;
	if (winmd_coded_to_typedef_row(db, db->meta, coded, &open_row) != 0) return -1;
	if (winmd_typedef_uuid(db->meta, open_row, og) != 0) return -1;
	if (sb_read_compressed(p, end, argc) != 0) return -1;
	return (*argc == 0 || *argc > 8) ? -1 : 0; /* WinRT generics have at most 2 args */
}

/* Decode argc closed-generic args into 4096-byte slots of argbuf, filling argsigs. */
static int sb_generic_args(
  winmd_db const *db, uint8_t const **p, uint8_t const *end, char *argbuf, char const **argsigs,
  uint32_t argc, int depth
) {
	uint32_t i;
	for (i = 0; i < argc; i++) {
		char *slot = argbuf + ( size_t ) i * 4096u;
		if (sb_type_depth(db, p, end, slot, 4096u, depth + 1) != 0) return -1;
		argsigs [i] = slot;
	}
	return 0;
}

/* ELEMENT_TYPE_GENERICINST: <CLASS|VALUETYPE> <open-token> <argc> <arg>...  */
static int sb_genericinst(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, int depth) {
	uint32_t    argc;
	uint8_t     og [16];
	char const *argsigs [8];
	char       *argbuf;
	int         rc;

	if (sb_generic_header(db, p, end, og, &argc) != 0) return -1;
	argbuf = ( char * ) malloc(( size_t ) argc * 4096u);
	if (!argbuf) return -1;
	rc = sb_generic_args(db, p, end, argbuf, argsigs, argc, depth);
	if (rc == 0) rc = cwinrt_sig_pinterface(og, argsigs, argc, out, cap) < 0 ? -1 : 0;
	free(argbuf);
	return rc;
}

/* Leaf element types that need no further token reads: string, object, basic.
   Returns 1 if handled (rc in *rc), 0 if elt is not a scalar leaf. */
static int sb_scalar_sig(uint8_t elt, char *out, size_t cap, int *rc) {
	char const *s;
	if (elt == ELT_STRING) {
		*rc = ( size_t ) snprintf(out, cap, "string") >= cap ? -1 : 0;
		return 1;
	}
	if (elt == ELT_OBJECT) {
		*rc = ( size_t ) snprintf(out, cap, "cinterface(IInspectable)") >= cap ? -1 : 0;
		return 1;
	}
	if (elt >= ELT_BOOLEAN && elt <= ELT_R8) {
		s   = cwinrt_piid_basic_sig(elt);
		*rc = (!s || ( size_t ) snprintf(out, cap, "%s", s) >= cap) ? -1 : 0;
		return 1;
	}
	return 0;
}

static int sb_type_depth(
  winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, int depth
) {
	uint8_t elt;
	int     rc;
	if (depth > SB_MAX_DEPTH) return -1;
	if (sb_read_u8(p, end, &elt) != 0) return -1;
	if (sb_scalar_sig(elt, out, cap, &rc)) return rc;
	if (elt == ELT_VALUETYPE || elt == ELT_CLASS) return sb_named_type(db, p, end, out, cap, depth);
	if (elt == ELT_GENERICINST) return sb_genericinst(db, p, end, out, cap, depth);
	return -1; /* VAR/MVAR/PTR/BYREF/SZARRAY not valid in a closed instantiation */
}

int cwinrt_sigbuild_typedef(winmd_db const *db, uint32_t typedef_row1, char *out, size_t cap) {
	return sb_typedef_depth(db, typedef_row1, out, cap, 0);
}

int cwinrt_sigbuild_type(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap) {
	return sb_type_depth(db, p, end, out, cap, 0);
}

/* Build a human-readable display name for a GenericInst blob:
   "Namespace.OpenName`N<arg,arg>" with args by WinRT short name. Best-effort. */
static int sb_argname(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, int depth);

static int sb_typedef_name(winmd_db const *db, uint32_t row1, char *out, size_t cap) {
	char full [256];
	if (sb_typedef_full_name(db, row1, full, sizeof(full)) != 0) return -1;
	snprintf(out, cap, "%s", full);
	return 0;
}

/* Basic ELEMENT_TYPE -> WinRT short name ("Int32", ...) for display; NULL if none. */
static char const *sb_basic_name(uint8_t elt) {
	static char const *const nm [] = {0,       0,        "Boolean", "Char16", "Int8",   "UInt8",  "Int16",
	                                  "UInt16", "Int32",  "UInt32",  "Int64",  "UInt64", "Single", "Double"};
	if (elt == ELT_STRING) return "String";
	if (elt == ELT_OBJECT) return "Object";
	if (elt < sizeof(nm) / sizeof(nm [0]) && nm [elt]) return nm [elt];
	return 0;
}

/* CLASS/VALUETYPE -> short WinRT name for display. */
static int sb_named_name(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap) {
	uint32_t coded, row = 0;
	char     full [256];
	if (sb_read_compressed(p, end, &coded) != 0) return -1;
	if (winmd_coded_type_full_name(db->meta, coded, full, sizeof(full)) == 0 && full [0]) {
		char const *s = strrchr(full, '.');
		snprintf(out, cap, "%s", s ? s + 1 : full);
		return 0;
	}
	if (winmd_coded_to_typedef_row(db, db->meta, coded, &row) == 0) return sb_typedef_name(db, row, out, cap);
	return -1;
}

/* Read the GenericInst open-type token and write its short name (sans `N arity)
   into base. Leaves *p positioned at <argc>. */
static int sb_generic_base_name(
  winmd_db const *db, uint8_t const **p, uint8_t const *end, char *base, size_t base_cap
) {
	uint8_t     inner;
	uint32_t    coded, open_row = 0;
	char        full [256];
	char       *bt;
	char const *s;
	if (sb_read_u8(p, end, &inner) != 0 || sb_read_compressed(p, end, &coded) != 0) return -1;
	if (winmd_coded_to_typedef_row(db, db->meta, coded, &open_row) != 0) return -1;
	sb_typedef_full_name(db, open_row, full, sizeof(full));
	s = strrchr(full, '.');
	snprintf(base, base_cap, "%s", s ? s + 1 : full);
	if ((bt = strchr(base, '`')) != NULL) *bt = '\0';
	return 0;
}

/* Append "arg[,arg]..." display names at *pos. */
static int sb_generic_arg_names(
  winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, size_t *pos,
  uint32_t argc, int depth
) {
	uint32_t i;
	for (i = 0; i < argc; i++) {
		char an [256];
		if (sb_argname(db, p, end, an, sizeof(an), depth + 1) != 0) return -1;
		if (i && *pos + 1 < cap) out [(*pos)++] = ',';
		*pos += ( size_t ) snprintf(out + *pos, cap - *pos, "%s", an);
		if (*pos >= cap) return -1;
	}
	return 0;
}

/* GENERICINST -> "OpenName<arg,arg>" for display. */
static int sb_genericinst_name(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, int depth) {
	uint32_t argc;
	char     base [128];
	size_t   pos;

	if (sb_generic_base_name(db, p, end, base, sizeof(base)) != 0) return -1;
	pos = ( size_t ) snprintf(out, cap, "%s<", base);
	if (sb_read_compressed(p, end, &argc) != 0) return -1;
	if (sb_generic_arg_names(db, p, end, out, cap, &pos, argc, depth) != 0) return -1;
	if (pos + 2 >= cap) return -1;
	out [pos++] = '>';
	out [pos]   = '\0';
	return 0;
}

static int sb_argname(winmd_db const *db, uint8_t const **p, uint8_t const *end, char *out, size_t cap, int depth) {
	uint8_t     elt;
	char const *basic;
	if (depth > SB_MAX_DEPTH) return -1;
	if (sb_read_u8(p, end, &elt) != 0) return -1;
	if ((basic = sb_basic_name(elt)) != NULL) return snprintf(out, cap, "%s", basic) >= ( int ) cap ? -1 : 0;
	if (elt == ELT_VALUETYPE || elt == ELT_CLASS) return sb_named_name(db, p, end, out, cap);
	if (elt == ELT_GENERICINST) return sb_genericinst_name(db, p, end, out, cap, depth);
	return -1;
}

int cwinrt_sigbuild_typespec(
  winmd_db const *db, uint32_t typespec_row1, char *sig, size_t sig_cap, char *name, size_t name_cap
) {
	uint32_t       blen = 0;
	uint8_t const *bp;
	uint8_t const *e;

	if (sb_typespec_blob(db, typespec_row1, &bp, &blen) != 0) return -1;
	if (bp [0] != ELT_GENERICINST) return -1; /* only parameterized instantiations */

	if (name && name_cap) {
		uint8_t const *np = bp;
		uint8_t const *ne = bp + blen;
		if (sb_argname(db, &np, ne, name, name_cap, 0) != 0) name [0] = '\0';
	}
	e = bp + blen;
	return sb_type_depth(db, &bp, e, sig, sig_cap, 0);
}
