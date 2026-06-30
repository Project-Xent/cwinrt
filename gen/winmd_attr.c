#include "winmd_int.h"

#include <stdio.h>
#include <string.h>

enum
{
	WINMD_TBL_MEMBERREF = 10,
};

typedef struct ca_layout {
	uint8_t parent_ix;
	uint8_t type_ix;
	uint8_t blob_ix;
} ca_layout;

/* CustomAttribute row columns: Parent (HasCustomAttribute), Type
   (CustomAttributeType), Value (blob). The two coded-index widths come from the
   shared helper so they cannot drift from row_size_table's sizing. */
static void ca_layout_init(winmd_tables const *t, ca_layout *out) {
	out->parent_ix = winmd_ca_parent_ix(t);
	out->type_ix   = winmd_ca_type_ix(t);
	out->blob_ix   = t->blob_ix;
}

static void map_attr_suffix(char const *shortn, char *suffix, size_t suffix_sz) {
	static struct {
		char const *attr;
		char const *suffix;
	} const tbl []  = {
		{"OverloadAttribute", "Overload"},
		{"DefaultOverloadAttribute", "DefaultOverload"},
		{"ActivatableAttribute", "Activatable"},
	};
	size_t i;

	for (i = 0; i < sizeof(tbl) / sizeof(tbl [0]); i++)
		if (strcmp(shortn, tbl [i].attr) == 0) {
			snprintf(suffix, suffix_sz, "%s", tbl [i].suffix);
			return;
		}
	snprintf(suffix, suffix_sz, "%s", shortn);
}

static uint32_t method_declaring_typedef(winmd_meta const *m, uint32_t method_row1) {
	winmd_tables const *t    = &m->tabs;
	uint32_t            td_n = t->rows [WINMD_TBL_TYPEDEF];
	uint32_t            i;

	for (i = 1; i <= td_n; i++) {
		uint32_t start = winmd_typedef_method_list(t, i);
		uint32_t end   = (i < td_n) ? winmd_typedef_method_list(t, i + 1) : t->rows [WINMD_TBL_METHODDEF] + 1;
		if (method_row1 >= start && method_row1 < end) return i;
	}
	return 0;
}

static int typedef_attr_suffix(winmd_meta const *m, uint32_t typedef_row1, char *suffix, size_t suffix_sz) {
	uint8_t const *p;
	char const    *name;
	char const    *ns;
	char const    *shortn;

	p = winmd_row_ptr(&m->tabs, WINMD_TBL_TYPEDEF, typedef_row1);
	if (!p) return -1;
	name   = winmd_str_at(&m->strings, winmd_read_idx(p + 4, m->tabs.string_ix));
	ns     = winmd_str_at(&m->strings, winmd_read_idx(p + 4 + m->tabs.string_ix, m->tabs.string_ix));
	shortn = name;
	if (ns && ns [0]) {
		char full [256];
		snprintf(full, sizeof(full), "%s.%s", ns, name ? name : "");
		shortn = strrchr(full, '.');
		shortn = shortn ? shortn + 1 : full;
	}
	if (!shortn || !shortn [0]) return -1;
	map_attr_suffix(shortn, suffix, suffix_sz);
	return 0;
}

/* Translate a MemberRefParent coded index to a TypeDefOrRef coded index for
   name resolution. Returns -1 for ModuleRef/MethodDef (not an attribute type). */
static int memberref_parent_to_tdor(uint32_t class_coded, uint32_t *tdor) {
	uint32_t ptag = class_coded & 7u;
	uint32_t prid = class_coded >> 3;

	if (ptag == 0u) *tdor = (prid << 2) | 0u;      /* TypeDef  */
	else if (ptag == 1u) *tdor = (prid << 2) | 1u; /* TypeRef  */
	else if (ptag == 4u) *tdor = (prid << 2) | 2u; /* TypeSpec */
	else return -1;
	return 0;
}

static int memberref_attr_suffix(winmd_meta const *m, uint32_t memberref_row1, char *suffix, size_t suffix_sz) {
	winmd_tables const *t = &m->tabs;
	uint8_t const      *p;
	/* MemberRef.Class is a MemberRefParent coded index: tables
	   {TypeDef,TypeRef,ModuleRef,MethodDef,TypeSpec}, 3 tag bits. */
	static int const    mrp [] = {2, 1, 26, 6, 27};
	uint32_t            cd_sz;
	uint32_t            class_coded;
	uint32_t            tdor;
	char                full [256];
	char const         *shortn;

	if (!suffix || suffix_sz < 2) return -1;
	suffix [0] = '\0';
	p          = winmd_row_ptr(t, WINMD_TBL_MEMBERREF, memberref_row1);
	if (!p) return -1;
	cd_sz       = winmd_cd_rows(t, mrp, 5, 3);
	class_coded = winmd_read_idx(p, ( uint8_t ) cd_sz);
	if (memberref_parent_to_tdor(class_coded, &tdor) != 0) return -1;
	if (winmd_coded_type_full_name(m, tdor, full, sizeof(full)) != 0) return -1;
	shortn = strrchr(full, '.');
	shortn = shortn ? shortn + 1 : full;
	if (!shortn [0]) return -1;
	map_attr_suffix(shortn, suffix, suffix_sz);
	return 0;
}

static int
ca_constructor_suffix(winmd_meta const *m, uint8_t const *row, ca_layout const *lay, char *suffix, size_t suffix_sz) {
	uint32_t type_coded;
	uint32_t tag;
	uint32_t rid;

	type_coded = winmd_read_idx(row + lay->parent_ix, lay->type_ix);
	tag        = type_coded & 7u;
	rid        = type_coded >> 3;
	if (!rid) return -1;
	/* CustomAttributeType coded index: ECMA uses tag 2=MethodDef, 3=MemberRef;
	   accept the 0/1 variants too for robustness (those tags are otherwise unused). */
	if (tag == 1u || tag == 3u) return memberref_attr_suffix(m, rid, suffix, suffix_sz);
	if (tag == 0u || tag == 2u) {
		uint32_t td = method_declaring_typedef(m, rid);
		if (td) return typedef_attr_suffix(m, td, suffix, suffix_sz);
	}
	return -1;
}

static int blob_read_compressed(uint8_t const *blob, uint32_t len, uint32_t *pos, uint32_t *value) {
	uint32_t n;
	if (!blob || !pos || !value || *pos > len) return -1;
	n = winmd_decompress(blob + *pos, len - *pos, value);
	if (!n) return -1;
	*pos += n;
	return 0;
}

static int utf16le_to_utf8(uint16_t const *ws, uint32_t wlen, char *out, size_t outsz) {
	uint32_t i;
	size_t   o = 0;
	if (!ws || !out || outsz < 2) return -1;
	for (i = 0; i < wlen && o + 2 < outsz; i++) {
		uint16_t w = ws [i];
		if (w == 0) break;
		if (w < 0x80) out [o++] = ( char ) w;
		else out [o++] = '_';
	}
	out [o] = '\0';
	return o ? 0 : -1;
}

/* Attempt 1: 16-bit UTF-16 length prefix immediately after the 0x0001 prolog. */
static int blob_str_len_prefixed(uint8_t const *blob, uint32_t len, char *out, size_t outsz) {
	uint32_t wlen;
	if (4u > len) return -1;
	wlen = ( uint32_t ) ( uint16_t ) (blob [2] | (( uint16_t ) blob [3] << 8));
	if (wlen == 0 || wlen >= 512u || 4u + wlen * 2 > len) return -1;
	return utf16le_to_utf8(( uint16_t const * ) (blob + 4), wlen, out, outsz);
}

/* Attempt 2: ECMA SerString (compressed byte length, UTF-8 or UTF-16 payload). */
static int blob_str_compressed(uint8_t const *blob, uint32_t len, char *out, size_t outsz) {
	uint32_t pos = 2;
	uint32_t slen;
	size_t   n;

	if (blob_read_compressed(blob, len, &pos, &slen) != 0) return -1;
	if (slen == 0 || slen >= 512u || pos + slen > len) return -1;
	if (slen >= 2 && blob [pos + 1] == 0
	    && utf16le_to_utf8(( uint16_t const * ) (blob + pos), slen / 2u, out, outsz) == 0)
		return 0;
	n = slen < outsz - 1 ? slen : outsz - 1;
	memcpy(out, blob + pos, n);
	out [n] = '\0';
	return out [0] ? 0 : -1;
}

/* Fallback: scan for the first run of >=2 printable ASCII chars (UTF-16LE). */
static int blob_str_scan(uint8_t const *blob, uint32_t len, char *out, size_t outsz) {
	uint32_t i;

	for (i = 2; i + 4 < len; i += 2) {
		uint32_t j = 0;
		char     tmp [256];
		while (i + j * 2 + 1 < len && j + 1 < sizeof(tmp)) {
			uint16_t w = ( uint16_t ) blob [i + j * 2] | (( uint16_t ) blob [i + j * 2 + 1] << 8);
			if (w == 0) break;
			if (w < 0x20 || w > 0x7e) {
				j = 0;
				break;
			}
			tmp [j++] = ( char ) w;
		}
		tmp [j] = '\0';
		if (j >= 2) {
			snprintf(out, outsz, "%s", tmp);
			return 0;
		}
	}
	return -1;
}

static int blob_first_string_arg(uint8_t const *blob, uint32_t len, char *out, size_t outsz) {
	if (!blob || !out || outsz < 2) return -1;
	out [0] = '\0';
	if (len < 4 || blob [0] != 0x01 || blob [1] != 0x00) return -1;
	if (blob_str_len_prefixed(blob, len, out, outsz) == 0) return 0;
	if (blob_str_compressed(blob, len, out, outsz) == 0) return 0;
	return blob_str_scan(blob, len, out, outsz);
}

typedef struct ca_target {
	uint32_t    tag;
	uint32_t    row1;
	char const *want;
} ca_target;

/* True if CustomAttribute `row` targets tgt->(tag,row1) and its constructor
   resolves to suffix tgt->want. */
static int ca_row_matches(winmd_meta const *m, ca_layout const *lay, uint8_t const *row, ca_target const *tgt) {
	uint32_t parent;
	char     attr [96];

	if (!row) return 0;
	parent = winmd_read_idx(row, lay->parent_ix);
	if ((parent & 31u) != tgt->tag) return 0;
	if ((parent >> 5) != tgt->row1) return 0;
	if (ca_constructor_suffix(m, row, lay, attr, sizeof(attr)) != 0) return 0;
	return strcmp(attr, tgt->want) == 0;
}

/* True if parent (tag,row1) carries a CustomAttribute with suffix `want`. */
static bool ca_scan_has(winmd_meta const *m, uint32_t parent_tag, uint32_t parent_row1, char const *want) {
	winmd_tables const *t;
	ca_layout           lay;
	uint32_t            rows;
	uint32_t            i;

	if (!m) return false;
	t    = &m->tabs;
	rows = t->rows [WINMD_TBL_CUSTOMATTRIBUTE];
	if (!rows) return false;
	ca_layout_init(t, &lay);
	{
		ca_target const tgt = {parent_tag, parent_row1, want};
		for (i = 1; i <= rows; i++) {
			uint8_t const *row = winmd_row_ptr(t, WINMD_TBL_CUSTOMATTRIBUTE, i);
			if (ca_row_matches(m, &lay, row, &tgt)) return true;
		}
	}
	return false;
}

int winmd_method_overload_name(winmd_meta const *m, uint32_t method_row1, char *buf, size_t bufsz) {
	winmd_tables const *t;
	ca_layout           lay;
	ca_target           ovl;
	uint32_t            rows;
	uint32_t            i;

	if (!m || !buf || bufsz < 2 || !method_row1) return -1;
	buf [0] = '\0';
	t       = &m->tabs;
	rows    = t->rows [WINMD_TBL_CUSTOMATTRIBUTE];
	if (!rows) return -1;
	ca_layout_init(t, &lay);
	ovl.tag  = 0u;
	ovl.row1 = method_row1;
	ovl.want = "Overload";
	for (i = 1; i <= rows; i++) {
		uint8_t const *row = winmd_row_ptr(t, WINMD_TBL_CUSTOMATTRIBUTE, i);
		uint32_t       blob_off;
		uint8_t const *blob;
		uint32_t       blob_len;

		if (!ca_row_matches(m, &lay, row, &ovl)) continue;
		blob_off = winmd_read_idx(row + lay.parent_ix + lay.type_ix, lay.blob_ix);
		if (!blob_off || blob_off >= m->blobs.size) continue;
		blob     = m->blobs.data + blob_off;
		blob_len = ( uint32_t ) m->blobs.size - blob_off;
		if (blob_first_string_arg(blob, blob_len, buf, bufsz) == 0) return 0;
	}
	return -1;
}

bool winmd_method_is_default_overload(winmd_meta const *m, uint32_t method_row1) {
	if (!method_row1) return false;
	return ca_scan_has(m, 0u, method_row1, "DefaultOverload");
}

bool winmd_ca_has_attr_on(winmd_meta const *m, uint32_t parent_tag, uint32_t parent_row1, char const *attr_short) {
	if (!attr_short) return false;
	return ca_scan_has(m, parent_tag, parent_row1, attr_short);
}

bool winmd_typedef_is_activatable(winmd_meta const *m, uint32_t typedef_row1) {
	if (!typedef_row1) return false;
	return ca_scan_has(m, 3u, typedef_row1, "Activatable");
}
