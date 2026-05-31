#include "winmd.h"

#include "err.h"
#include "name.h"
#include "sig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "winmd_int.h"

#define TBL_MODULE    WINMD_TBL_MODULE
#define TBL_TYPEREF   WINMD_TBL_TYPEREF
#define TBL_TYPEDEF   WINMD_TBL_TYPEDEF
#define TBL_FIELD     WINMD_TBL_FIELD
#define TBL_METHODDEF WINMD_TBL_METHODDEF
#define TBL_PARAM     WINMD_TBL_PARAM
#define TBL_TYPESPEC  WINMD_TBL_TYPESPEC
#define WINMD_NTABLES 64

static uint16_t rd16(uint8_t const *p) { return ( uint16_t ) (p [0] | (( uint16_t ) p [1] << 8)); }

static uint32_t rd32(uint8_t const *p) {
	return ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8) | (( uint32_t ) p [2] << 16) | (( uint32_t ) p [3] << 24);
}

static uint64_t rd64(uint8_t const *p) { return ( uint64_t ) rd32(p) | (( uint64_t ) rd32(p + 4) << 32); }

static uint32_t idx_bytes(uint32_t rows) { return rows > 0xffff ? 4u : 2u; }

static uint32_t read_idx(uint8_t const *p, uint8_t sz) { return sz == 4 ? rd32(p) : rd16(p); }

static int      pe_rva_to_offset(uint8_t const *data, size_t size, uint32_t rva, uint32_t *out_off) {
	uint32_t       pe;
	uint32_t       opt_sz;
	uint16_t       nsec;
	uint32_t       i;
	uint8_t const *opt;
	uint8_t const *sec;

	if (!out_off || size < 0x40) return -1;
	pe = rd32(data + 0x3c);
	if (pe + 0x18 > size || rd32(data + pe) != 0x4550) return -1;
	opt_sz = rd16(data + pe + 20);
	nsec   = rd16(data + pe + 6);
	opt    = data + pe + 24;
	if (opt + opt_sz + ( size_t ) nsec * 40 > data + size) return -1;
	sec = opt + opt_sz;
	for (i = 0; i < nsec; i++) {
		uint32_t va    = rd32(sec + i * 40 + 12);
		uint32_t vsize = rd32(sec + i * 40 + 8);
		uint32_t raw   = rd32(sec + i * 40 + 20);
		uint32_t span  = vsize;
		if (rd32(sec + i * 40 + 16) > span) span = rd32(sec + i * 40 + 16);
		if (rva < va || rva >= va + span) continue;
		*out_off = raw + (rva - va);
		return 0;
	}
	return -1;
}

/* Reads the CLI header RVA from the PE optional header (PE32 at +208, PE32+ at
   +224). Returns 0 and stores the RVA in *cli_rva, or -1 on a malformed header. */
static int pe_cli_header_rva(uint8_t const *data, size_t size, uint32_t pe, uint32_t *cli_rva) {
	uint32_t       opt_sz = rd16(data + pe + 20);
	uint8_t const *opt    = data + pe + 24;
	uint16_t       opt_magic;

	if (opt + opt_sz > data + size) return -1;
	opt_magic = rd16(opt);
	if (opt_magic == 0x10b) *cli_rva = rd32(opt + 208);
	else if (opt_magic == 0x20b) *cli_rva = rd32(opt + 224);
	else return -1;
	return *cli_rva ? 0 : -1;
}

static int pe_cli_meta(uint8_t const *data, size_t size, uint8_t const **meta, uint32_t *meta_size) {
	uint32_t pe;
	uint32_t cli_rva;
	uint32_t cli_off;
	uint32_t meta_rva;
	uint32_t meta_off;

	if (size < 0x40) return -1;
	pe = rd32(data + 0x3c);
	if (pe + 0x18 > size || rd32(data + pe) != 0x4550) return -1;
	if (pe_cli_header_rva(data, size, pe, &cli_rva) != 0) return -1;
	if (pe_rva_to_offset(data, size, cli_rva, &cli_off) != 0) return -1;
	if (cli_off + 16 > size) return -1;
	meta_rva   = rd32(data + cli_off + 8);
	*meta_size = rd32(data + cli_off + 12);
	if (!meta_rva || !*meta_size) return -1;
	if (pe_rva_to_offset(data, size, meta_rva, &meta_off) != 0) return -1;
	if (meta_off + *meta_size > size) return -1;
	*meta = data + meta_off;
	return 0;
}

static int meta_stream(uint8_t const *meta, uint32_t meta_sz, char const *name, winmd_heap *out) {
	uint32_t ver;
	uint32_t pos;
	uint16_t n;
	uint16_t i;

	if (meta_sz < 16 || rd32(meta) != 0x424a5342) return -1;
	ver = rd32(meta + 12);
	pos = 16 + ver;
	pos = (pos + 3) & ~3u;
	if (pos + 4 > meta_sz) return -1;
	n    = rd16(meta + pos + 2);
	pos += 4;
	for (i = 0; i < n; i++) {
		uint32_t    off  = rd32(meta + pos);
		uint32_t    sz   = rd32(meta + pos + 4);
		char const *sn   = ( char const * ) (meta + pos + 8);
		pos             += 8;
		while (pos < meta_sz && meta [pos]) pos++;
		pos++;
		while ((pos & 3) != 0) pos++;
		if (strcmp(sn, name) != 0) continue;
		if (off + sz > meta_sz) return -1;
		out->data = ( uint8_t * ) (meta + off);
		out->size = sz;
		return 0;
	}
	return -1;
}

static uint32_t rows_max(winmd_tables *t, int const *ids, int n) {
	uint32_t m = 0;
	int      i;
	for (i = 0; i < n; i++)
		if (t->rows [ids [i]] > m) m = t->rows [ids [i]];
	return m;
}

static uint32_t cd_rows(winmd_tables *t, int const *ids, int n, int tag) {
	uint32_t m = rows_max(t, ids, n);
	return (m << tag) > 0xffff ? 4u : 2u;
}

/* ECMA-335: table heap may end with up to 3 zero padding bytes. */
static int tables_heap_ok(uint8_t const *base, uint32_t cursor, uint32_t data_sz) {
	uint32_t rem;
	uint32_t k;

	if (cursor == data_sz) return 0;
	if (cursor > data_sz || data_sz - cursor > 4u) return -1;
	rem = data_sz - cursor;
	for (k = 0; k < rem; k++)
		if (base [cursor + k] != 0) return -1;
	return 0;
}

/* A coded-index column: width = cd_rows over the tables in `ids` with `tag`
   tag bits, contributing `mult` copies of that width to the row size. */
typedef struct {
	int const *ids;
	uint8_t    n;
	uint8_t    tag;
	uint8_t    mult;
} cd_term;

/* Describes one metadata table's row layout as a sum of fixed-size pieces.
   `konst` fixed bytes; `ns`/`nb`/`ng` copies of the string/blob/guid heap
   index; the simple table indices in `ix`; and the coded-index columns `cd`.
   row_size_eval below replays this to the exact byte sum the original
   per-table expressions produced. */
typedef struct {
	uint8_t konst;
	uint8_t ns, nb, ng;
	int8_t  ix [4];
	cd_term cd [2];
} row_desc;

static int const cd_resolution_scope []  = {0, 26, 35, 1};
static int const cd_has_constant []       = {4, 8, 23};
static int const cd_has_field_marshal []  = {4, 8};
static int const cd_has_decl_security []  = {2, 6, 32};
static int const cd_has_semantic []       = {20, 23};
static int const cd_method_def_or_ref []  = {6, 10};
static int const cd_member_forwarded []   = {4, 6};
static int const cd_type_or_method_def [] = {2, 6};
static int const cd_type_def_or_ref []    = {2, 1, 27};
static int const cd_implementation []     = {38, 35, 39};
static int const cd_custom_attr_type []   = {6, 10};
static int const cd_has_custom_attr [] = {6, 4, 1, 2, 8, 9, 10, 0, 14, 23, 20, 17, 26, 27, 32, 35, 38, 39, 40, 42, 44, 43};
static int const cd_member_ref_parent [] = {2, 1, 26, 6, TBL_TYPESPEC};
static int const cd_has_custom_debug []
  = {6, 4, 1, 2, 8, 9, 10, 0, 14, 23, 20, 17, 26, 27, 32, 35, 38, 39, 40, 42, 44, 43, 48, 50, 51, 52, 53};

/* 0 terminates an `ix` list: table 0 (Module) is never a simple index column,
   so a 0 entry is unambiguous and lets zero-initialized empty descriptor slots
   (45..47, 56..63) yield row size 0 like the original switch default. */
#define IX_END 0
#define NOIX        \
	{               \
		0, 0, 0, 0  \
	}
#define NOCD            \
	{                   \
		{NULL, 0, 0, 0} \
	}
#define CD1(set, tag) \
	{ {set, ( uint8_t ) (sizeof(set) / sizeof((set) [0])), tag, 1}, {NULL, 0, 0, 0} }
#define CDM(set, tag, mult) \
	{ {set, ( uint8_t ) (sizeof(set) / sizeof((set) [0])), tag, mult}, {NULL, 0, 0, 0} }
#define CD2(s0, t0, s1, t1)                                                                                          \
	{ {s0, ( uint8_t ) (sizeof(s0) / sizeof((s0) [0])), t0, 1}, {s1, ( uint8_t ) (sizeof(s1) / sizeof((s1) [0])), t1, 1} }

/* Row descriptors for tables 0..55. Empty (zero) entries cover the unused
   slots 45..47 and 56..63, matching the original switch default of 0. */
static row_desc const g_row_desc [WINMD_NTABLES] = {
	[0]  = {2, 1, 0, 3, NOIX, NOCD},
	[1]  = {0, 2, 0, 0, NOIX, CD1(cd_resolution_scope, 2)},
	[2]  = {4, 2, 0, 0, {TBL_FIELD, TBL_METHODDEF, IX_END, 0}, CD1(cd_type_def_or_ref, 2)},
	[3]  = {0, 0, 0, 0, {TBL_FIELD, IX_END, 0, 0}, NOCD},
	[4]  = {2, 1, 1, 0, NOIX, NOCD},
	[5]  = {0, 0, 0, 0, {TBL_METHODDEF, IX_END, 0, 0}, NOCD},
	[6]  = {8, 1, 1, 0, {TBL_PARAM, IX_END, 0, 0}, NOCD},
	[7]  = {0, 0, 0, 0, {TBL_PARAM, IX_END, 0, 0}, NOCD},
	[8]  = {4, 1, 0, 0, NOIX, NOCD},
	[9]  = {0, 0, 0, 0, {TBL_TYPEDEF, IX_END, 0, 0}, CD1(cd_type_def_or_ref, 2)},
	[10] = {0, 1, 1, 0, NOIX, CD1(cd_member_ref_parent, 3)},
	[11] = {2, 0, 1, 0, NOIX, CD1(cd_has_constant, 2)},
	[12] = {0, 0, 1, 0, NOIX, CD2(cd_has_custom_attr, 5, cd_custom_attr_type, 3)},
	[13] = {0, 0, 1, 0, NOIX, CD1(cd_has_field_marshal, 1)},
	[14] = {2, 0, 1, 0, NOIX, CD1(cd_has_decl_security, 2)},
	[15] = {6, 0, 0, 0, {TBL_TYPEDEF, IX_END, 0, 0}, NOCD},
	[16] = {4, 0, 0, 0, {TBL_FIELD, IX_END, 0, 0}, NOCD},
	[17] = {0, 0, 1, 0, NOIX, NOCD},
	[18] = {0, 0, 0, 0, {TBL_TYPEDEF, 20, IX_END, 0}, NOCD},
	[19] = {0, 0, 0, 0, {20, IX_END, 0, 0}, NOCD},
	[20] = {2, 1, 0, 0, NOIX, CD1(cd_type_def_or_ref, 2)},
	[21] = {0, 0, 0, 0, {TBL_TYPEDEF, 23, IX_END, 0}, NOCD},
	[22] = {0, 0, 0, 0, {23, IX_END, 0, 0}, NOCD},
	[23] = {2, 1, 1, 0, NOIX, NOCD},
	[24] = {2, 0, 0, 0, {TBL_METHODDEF, IX_END, 0, 0}, CD1(cd_has_semantic, 1)},
	[25] = {0, 0, 0, 0, {TBL_TYPEDEF, IX_END, 0, 0}, CDM(cd_method_def_or_ref, 1, 2)},
	[26] = {0, 1, 0, 0, NOIX, NOCD},
	[27] = {0, 0, 1, 0, NOIX, NOCD},
	[28] = {2, 1, 0, 0, {26, IX_END, 0, 0}, CD1(cd_member_forwarded, 1)},
	[29] = {4, 0, 0, 0, {TBL_FIELD, IX_END, 0, 0}, NOCD},
	[30] = {8, 0, 0, 0, NOIX, NOCD},
	[31] = {4, 0, 0, 0, NOIX, NOCD},
	[32] = {16, 2, 1, 0, NOIX, NOCD},
	[33] = {4, 0, 0, 0, NOIX, NOCD},
	[34] = {12, 0, 0, 0, NOIX, NOCD},
	[35] = {12, 2, 2, 0, NOIX, NOCD},
	[36] = {4, 0, 0, 0, {35, IX_END, 0, 0}, NOCD},
	[37] = {12, 0, 0, 0, {35, IX_END, 0, 0}, NOCD},
	[38] = {4, 1, 1, 0, NOIX, NOCD},
	[39] = {8, 2, 0, 0, NOIX, CD1(cd_implementation, 2)},
	[40] = {8, 1, 0, 0, NOIX, CD1(cd_implementation, 2)},
	[41] = {0, 0, 0, 0, {2, 2, IX_END, 0}, NOCD},
	[42] = {4, 1, 0, 0, NOIX, CD1(cd_type_or_method_def, 1)},
	[43] = {0, 0, 1, 0, NOIX, CD1(cd_method_def_or_ref, 1)},
	[44] = {0, 0, 0, 0, {42, IX_END, 0, 0}, CD1(cd_type_def_or_ref, 2)},
	[48] = {0, 0, 2, 2, NOIX, NOCD},
	[49] = {0, 0, 1, 0, {48, IX_END, 0, 0}, NOCD},
	[50] = {8, 0, 0, 0, {6, 53, 51, 52}, NOCD},
	[51] = {4, 1, 0, 0, NOIX, NOCD},
	[52] = {0, 1, 1, 0, NOIX, NOCD},
	[53] = {0, 0, 1, 0, {53, IX_END, 0, 0}, NOCD},
	[54] = {0, 0, 0, 0, {6, 6, IX_END, 0}, NOCD},
	[55] = {0, 0, 1, 1, NOIX, CD1(cd_has_custom_debug, 5)},
};

static uint32_t row_size_table(winmd_tables *t, int tbl) {
	row_desc const *d;
	uint32_t        size;
	int             i;

	if (tbl < 0 || tbl >= WINMD_NTABLES) return 0;
	d    = &g_row_desc [tbl];
	size = ( uint32_t ) d->konst + ( uint32_t ) d->ns * t->string_ix + ( uint32_t ) d->nb * t->blob_ix
	     + ( uint32_t ) d->ng * t->guid_ix;
	for (i = 0; i < 4 && d->ix [i] != IX_END; i++) size += t->table_ix [d->ix [i]];
	for (i = 0; i < 2; i++)
		if (d->cd [i].ids) size += cd_rows(t, d->cd [i].ids, d->cd [i].n, d->cd [i].tag) * d->cd [i].mult;
	return size;
}

/* Parses the per-table row counts that follow the tables-stream header. Reads
   forward from *pos (advancing it) and fills t->rows/offset/table_ix. */
static void parse_row_counts(winmd_tables *t, winmd_heap const *heap, uint32_t max_present, uint32_t *pos) {
	uint32_t i;
	for (i = 0; i < WINMD_NTABLES; i++) {
		uint32_t n;
		t->rows [i]     = 0;
		t->offset [i]   = 0;
		t->table_ix [i] = 2;
		if (!((t->valid >> i) & 1)) continue;
		n     = rd32(heap->data + *pos) & 0x00ffffffu;
		*pos += 4;
		if (i < max_present) t->rows [i] = n;
		t->table_ix [i] = ( uint8_t ) idx_bytes(n ? n : 1u);
	}
}

/* Computes each present table's byte offset within the row data, returning the
   total row-data size in *cursor. Returns -1 on an unknown non-empty table. */
static int compute_offsets(winmd_tables *t, uint32_t *cursor) {
	uint32_t i;
	*cursor = 0;
	for (i = 0; i < WINMD_NTABLES; i++) {
		uint32_t rsz;
		if (!((t->valid >> i) & 1)) continue;
		t->offset [i] = *cursor;
		rsz           = row_size_table(t, ( int ) i);
		if (!rsz && t->rows [i]) {
			errmsg("winmd: unknown table row size");
			return -1;
		}
		if (!rsz) continue;
		*cursor += rsz * t->rows [i];
	}
	return 0;
}

/* Parses the tables-stream header (heap index widths, valid mask) into t and
   returns the largest table id that may carry rows for this metadata version. */
static uint32_t tables_parse_header(winmd_tables *t, winmd_heap const *heap) {
	uint8_t major = heap->data [4];
	uint8_t minor = heap->data [5];
	t->stream     = heap->data;
	t->string_ix  = (heap->data [6] & 1) ? 4 : 2;
	t->guid_ix    = (heap->data [6] & 2) ? 4 : 2;
	t->blob_ix    = (heap->data [6] & 4) ? 4 : 2;
	t->valid      = rd64(heap->data + 8);
	return (major == 1 && minor == 0) ? 42u : 56u;
}

/* Confirms the computed row-data size (cursor) matches the bytes left in the
   tables stream after rows_base, allowing the up-to-4 zero pad bytes. */
static int tables_validate_size(winmd_tables const *t, winmd_heap const *heap, uint32_t cursor) {
	uint32_t hdr     = ( uint32_t ) (t->rows_base - heap->data);
	uint32_t data_sz = ( uint32_t ) heap->size - hdr;
	if (tables_heap_ok(t->rows_base, cursor, data_sz) != 0) {
		errmsg("winmd: table heap size mismatch");
		return -1;
	}
	return 0;
}

static int tables_layout(winmd_tables *t, winmd_heap const *heap) {
	uint32_t pos;
	uint32_t cursor;
	uint32_t max_present;

	if (heap->size < 24) return -1;
	max_present = tables_parse_header(t, heap);

	pos         = 24;
	parse_row_counts(t, heap, max_present, &pos);
	if (heap->data [6] & 0x40) {
		if (pos + 4 > heap->size) return -1;
		pos += 4;
	}
	if (pos > heap->size) return -1;
	t->rows_base = heap->data + pos;

	if (compute_offsets(t, &cursor) != 0) return -1;
	return tables_validate_size(t, heap, cursor);
}

uint8_t const *winmd_row_ptr(winmd_tables const *t, int tbl, uint32_t row1) {
	uint32_t rsz;
	if (row1 == 0 || row1 > t->rows [tbl]) return NULL;
	rsz = row_size_table(( winmd_tables * ) t, tbl);
	return t->rows_base + t->offset [tbl] + (row1 - 1) * rsz;
}

static char const *str_from_heap(winmd_heap const *strings, uint32_t ix) {
	if (!strings || !strings->data || !ix || ix >= strings->size) return "";
	return ( char const * ) (strings->data + ix);
}

/* Resolves a TypeSpec (coded tag 2) into a C type name; falls back to void*. */
static int type_full_name_spec(winmd_meta const *m, uint32_t rid, char *buf, size_t bufsz) {
	uint8_t const *sp;
	uint32_t       sig_ix;
	sp = winmd_row_ptr(&m->tabs, TBL_TYPESPEC, rid);
	if (!sp) return -1;
	sig_ix = read_idx(sp, m->tabs.blob_ix);
	if (!sig_ix || winmd_parse_type_blob(m, sig_ix, buf, bufsz) != 0) snprintf(buf, bufsz, "void*");
	return 0;
}

/* Reads the (name, namespace) string pair from a TypeDef/TypeRef row whose
   name column begins at byte offset pos. */
static void type_row_name_ns(winmd_meta const *m, uint8_t const *p, uint32_t pos, char const **name, char const **ns) {
	*name = str_from_heap(&m->strings, read_idx(p + pos, m->tabs.string_ix));
	pos  += m->tabs.string_ix;
	*ns   = str_from_heap(&m->strings, read_idx(p + pos, m->tabs.string_ix));
}

/* Resolves a TypeDef (tag 0) or TypeRef (tag 1) coded index into its name and
   namespace. Returns 0 on success, -1 on a bad row or unsupported tag. */
static int type_full_name_def_ref(winmd_meta const *m, uint32_t tag, uint32_t rid, char const **name,
                                   char const **ns) {
	static int const cd_tdr [] = {2, 1, 27};
	uint8_t const   *p;
	if (tag == 0) {
		p = winmd_row_ptr(&m->tabs, TBL_TYPEDEF, rid);
		if (!p) return -1;
		type_row_name_ns(m, p, 4, name, ns);
		return 0;
	}
	if (tag == 1) {
		p = winmd_row_ptr(&m->tabs, TBL_TYPEREF, rid);
		if (!p) return -1;
		type_row_name_ns(m, p, cd_rows(( winmd_tables * ) &m->tabs, cd_tdr, 3, 2), name, ns);
		return 0;
	}
	return -1;
}

int winmd_type_full_name(winmd_meta const *m, uint32_t coded, char *buf, size_t bufsz) {
	uint32_t    tag;
	uint32_t    rid;
	char const *name;
	char const *ns;
	char        full [256];

	if (!m || !buf || bufsz < 2) return -1;
	tag = coded & 3;
	rid = coded >> 2;
	if (!rid) {
		snprintf(buf, bufsz, "void*");
		return 0;
	}
	if (tag == 2) return type_full_name_spec(m, rid, buf, bufsz);
	if (type_full_name_def_ref(m, tag, rid, &name, &ns) != 0) return -1;
	if (ns && ns [0]) snprintf(full, sizeof(full), "%s.%s", ns, name);
	else snprintf(full, sizeof(full), "%s", name ? name : "");
	cwinrt_name_winrt_to_c(full, buf, bufsz);
	return 0;
}

static char *str_dup(winmd_heap const *strings, uint32_t ix) {
	char const *s;
	char       *p;
	size_t      n;
	if (!strings || !strings->data) return NULL;
	if (!ix) {
		p = ( char * ) malloc(1);
		if (p) p [0] = '\0';
		return p;
	}
	if (ix >= strings->size) return NULL;
	s = ( char const * ) (strings->data + ix);
	n = strnlen(s, strings->size - ix);
	p = ( char * ) malloc(n + 1);
	if (!p) return NULL;
	memcpy(p, s, n + 1);
	return p;
}

static uint32_t typedef_method_col(winmd_tables const *t) {
	static int const cd_tdr []  = {2, 1, 27};
	uint32_t         pos        = 4 + t->string_ix * 2;
	pos                        += cd_rows(( winmd_tables * ) t, cd_tdr, 3, 2);
	pos                                    += t->table_ix [TBL_FIELD];
	return pos;
}

static uint32_t typedef_method_list(winmd_tables const *t, uint32_t row1) {
	uint8_t const *p;
	if (!row1) return 0;
	p = winmd_row_ptr(t, TBL_TYPEDEF, row1);
	if (!p) return 0;
	return read_idx(p + typedef_method_col(t), t->table_ix [TBL_METHODDEF]);
}

static int load_typedef(winmd_tables const *t, winmd_heap const *strings, uint32_t row1, winmd_row_typedef *out) {
	uint8_t const *p;
	uint32_t       pos;
	uint32_t       name_ix;
	uint32_t       ns_ix;

	p = winmd_row_ptr(t, TBL_TYPEDEF, row1);
	if (!p) return -1;
	out->flags           = rd32(p);
	pos                  = 4;
	name_ix              = read_idx(p + pos, t->string_ix);
	pos                 += t->string_ix;
	ns_ix                = read_idx(p + pos, t->string_ix);
	out->token           = 0x02000000u | row1;
	out->name            = str_dup(strings, name_ix);
	out->namespace_name  = str_dup(strings, ns_ix);
	if (!out->name || !out->namespace_name) {
		fprintf(stderr, "winmd: bad typedef row %u ix %u/%u\n", row1, name_ix, ns_ix);
		return -1;
	}
	return 0;
}

static int load_method(winmd_tables const *t, winmd_heap const *strings, uint32_t row1, winmd_row_method *out) {
	uint8_t const *p;
	uint32_t       name_ix;
	uint32_t       pos;

	p = winmd_row_ptr(t, TBL_METHODDEF, row1);
	if (!p) return -1;
	out->flags       = rd16(p + 6);
	pos              = 8;
	name_ix          = read_idx(p + pos, t->string_ix);
	pos             += t->string_ix;
	out->sig_blob    = read_idx(p + pos, t->blob_ix);
	out->token       = 0x06000000u | row1;
	out->name        = str_dup(strings, name_ix);
	out->ret_c_type  = NULL;
	out->params_c    = NULL;
	return out->name ? 0 : -1;
}

/* Reads the entire file at path into a freshly malloc'd buffer. On success
   stores the buffer in *buf and its byte size in *fsz; caller owns the buffer. */
static int read_file(char const *path, uint8_t **buf, long *fsz) {
	FILE *f;
	long  n;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "winmd: fopen failed\n");
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	n = ftell(f);
	if (n <= 0) {
		fclose(f);
		return -1;
	}
	*buf = ( uint8_t * ) malloc(( size_t ) n);
	if (!*buf) {
		fclose(f);
		return -1;
	}
	rewind(f);
	if (fread(*buf, 1, ( size_t ) n, f) != ( size_t ) n) {
		free(*buf);
		fclose(f);
		return -1;
	}
	fclose(f);
	*fsz = n;
	return 0;
}

/* Locates the #~/#-, #Strings and #Blob streams within the PE's CLI metadata. */
static int open_streams(uint8_t *buf, size_t fsz, winmd_heap *tilde, winmd_heap *strings, winmd_heap *blobs) {
	uint8_t const *meta;
	uint32_t       meta_sz;

	if (pe_cli_meta(buf, fsz, &meta, &meta_sz) != 0) {
		fprintf(stderr, "winmd: pe_cli_meta failed\n");
		return -1;
	}
	if (meta_stream(meta, meta_sz, "#~", tilde) != 0 && meta_stream(meta, meta_sz, "#-", tilde) != 0) {
		errmsg("winmd: no #~ stream");
		return -1;
	}
	if (meta_stream(meta, meta_sz, "#Strings", strings) != 0) {
		errmsg("winmd: no #Strings");
		return -1;
	}
	if (meta_stream(meta, meta_sz, "#Blob", blobs) != 0) {
		errmsg("winmd: no #Blob");
		return -1;
	}
	return 0;
}

static int load_typedefs(winmd_db *out, winmd_tables const *tabs, winmd_heap const *strings, uint32_t td_n) {
	uint32_t i;
	for (i = 0; i < td_n; i++) {
		if (load_typedef(tabs, strings, i + 1, &out->typedefs [i]) != 0) {
			fprintf(stderr, "winmd: load_typedef row %u failed\n", i + 1);
			return -1;
		}
	}
	return 0;
}

static int load_methods(winmd_db *out, winmd_tables const *tabs, winmd_heap const *strings, uint32_t md_n) {
	uint32_t i;
	for (i = 0; i < md_n; i++) {
		if (load_method(tabs, strings, i + 1, &out->methods [i]) != 0) return -1;
		out->methods [i].typedef_token = 0;
	}
	return 0;
}

/* Assigns each method its owning typedef token via the TypeDef method-list runs. */
static void link_method_lists(winmd_db *out, winmd_tables const *tabs, uint32_t td_n, uint32_t md_n) {
	uint32_t i;
	for (i = 1; i <= td_n; i++) {
		uint32_t start = typedef_method_list(tabs, i);
		uint32_t end   = (i < td_n) ? typedef_method_list(tabs, i + 1) : md_n + 1;
		uint32_t row;
		uint32_t tok = 0x02000000u | i;
		if (start < 1 || start > md_n + 1 || end < start || end > md_n + 1) continue;
		for (row = start; row < end; row++) out->methods [row - 1].typedef_token = tok;
	}
}

int winmd_open(char const *path, winmd_db *out) {
	long         fsz;
	uint8_t     *buf;
	winmd_heap   tilde;
	winmd_heap   strings;
	winmd_heap   blobs;
	winmd_tables tabs;
	uint32_t     td_n;
	uint32_t     md_n;

	if (!path || !out) return -1;
	memset(out, 0, sizeof(*out));
	memset(&tilde, 0, sizeof(tilde));
	memset(&strings, 0, sizeof(strings));
	memset(&tabs, 0, sizeof(tabs));

	if (read_file(path, &buf, &fsz) != 0) return -1;

	if (open_streams(buf, ( size_t ) fsz, &tilde, &strings, &blobs) != 0) {
		free(buf);
		return -1;
	}
	if (tables_layout(&tabs, &tilde) != 0) {
		fprintf(stderr, "winmd: tables_layout failed (stage %d)\n", 2);
		free(buf);
		return -1;
	}

	td_n               = tabs.rows [TBL_TYPEDEF];
	md_n               = tabs.rows [TBL_METHODDEF];
	out->file_data     = buf;
	out->file_size     = ( size_t ) fsz;
	out->typedef_count = td_n;
	out->method_count  = md_n;
	out->typedefs      = ( winmd_row_typedef * ) calloc(td_n, sizeof(*out->typedefs));
	out->methods       = ( winmd_row_method * ) calloc(md_n, sizeof(*out->methods));
	if ((!out->typedefs && td_n) || (!out->methods && md_n)) {
		winmd_close(out);
		return -1;
	}

	if (load_typedefs(out, &tabs, &strings, td_n) != 0) {
		winmd_close(out);
		return -1;
	}
	if (load_methods(out, &tabs, &strings, md_n) != 0) {
		winmd_close(out);
		return -1;
	}
	link_method_lists(out, &tabs, td_n, md_n);

	out->meta = ( winmd_meta * ) calloc(1, sizeof(winmd_meta));
	if (!out->meta) {
		winmd_close(out);
		return -1;
	}
	out->meta->tabs    = tabs;
	out->meta->strings = strings;
	out->meta->blobs   = blobs;
	return 0;
}

static int guid_bytes_valid(uint8_t const *g) {
	int i;
	int ascii = 0;
	for (i = 0; i < 16; i++)
		if (g [i] >= 0x20 && g [i] <= 0x7e) ascii++;
	return ascii < 6;
}

static int parse_guid_text(char const *s, uint8_t out [16]) {
	unsigned int d1;
	unsigned int d2;
	unsigned int d3;
	unsigned int b0, b1, b2, b3, b4, b5, b6, b7;
	int          n;

	if (!s || !out) return -1;
	while (*s == '{' || *s == ' ' || *s == '\t') s++;
	n = sscanf(s, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b0, &b1, &b2, &b3, &b4, &b5, &b6, &b7);
	if (n != 11) return -1;
	out [0]  = ( uint8_t ) (d1 & 0xff);
	out [1]  = ( uint8_t ) ((d1 >> 8) & 0xff);
	out [2]  = ( uint8_t ) ((d1 >> 16) & 0xff);
	out [3]  = ( uint8_t ) ((d1 >> 24) & 0xff);
	out [4]  = ( uint8_t ) (d2 & 0xff);
	out [5]  = ( uint8_t ) ((d2 >> 8) & 0xff);
	out [6]  = ( uint8_t ) (d3 & 0xff);
	out [7]  = ( uint8_t ) ((d3 >> 8) & 0xff);
	out [8]  = ( uint8_t ) b0;
	out [9]  = ( uint8_t ) b1;
	out [10] = ( uint8_t ) b2;
	out [11] = ( uint8_t ) b3;
	out [12] = ( uint8_t ) b4;
	out [13] = ( uint8_t ) b5;
	out [14] = ( uint8_t ) b6;
	out [15] = ( uint8_t ) b7;
	return guid_bytes_valid(out) ? 0 : -1;
}

static int parse_guid_utf16le(uint16_t const *s, uint32_t chars, uint8_t out [16]) {
	char     buf [64];
	uint32_t i;
	if (!s || chars < 36 || chars >= sizeof(buf)) return -1;
	for (i = 0; i < chars && i + 1 < sizeof(buf); i++) buf [i] = ( char ) (s [i] & 0xff);
	buf [i] = '\0';
	return parse_guid_text(buf, out);
}

/* GuidAttribute(uint32, uint16, uint16, byte x8): the CustomAttribute value
   blob is exactly prolog(0x0001) + 16 GUID bytes + named-arg count(0x0000) =
   20 bytes. Match that layout precisely (not an ASCII heuristic, which both
   wrongly rejected binary GUIDs that happen to contain printable bytes and
   could not distinguish other prolog-prefixed attributes). */
static int scan_guid_binary(uint8_t const *blob, uint32_t blob_len, uint8_t out [16]) {
	int k, nz = 0;
	if (!(blob_len == 20 && blob [0] == 0x01 && blob [1] == 0x00 && blob [18] == 0x00 && blob [19] == 0x00)) return -1;
	memcpy(out, blob + 2, 16);
	for (k = 0; k < 16; k++) nz |= out [k];
	return nz ? 0 : -1;
}

/* Copies the GUID-text characters of blob starting at i (hex digits, '-', '{')
   into tmp (NUL-terminated), stopping at '}'/NUL/end, and returns the length. */
static uint32_t copy_guid_chars(uint8_t const *blob, uint32_t blob_len, uint32_t i, char *tmp, size_t cap) {
	uint32_t j;
	uint32_t len = 0;
	for (j = i; j < blob_len && len + 1 < cap; j++) {
		char c = ( char ) blob [j];
		if (c == '}' || c == 0) break;
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '-' || c == '{')
			tmp [len++] = c;
	}
	tmp [len] = '\0';
	return len;
}

static int scan_guid_ascii(uint8_t const *blob, uint32_t blob_len, uint8_t out [16]) {
	uint32_t i;
	for (i = 0; i + 36 < blob_len; i++) {
		char     tmp [64];
		uint32_t len;
		if (blob [i] != '{' && blob [i + 8] != '-') continue;
		len = copy_guid_chars(blob, blob_len, i, tmp, sizeof(tmp));
		if (len >= 36 && parse_guid_text(tmp, out) == 0) return 0;
	}
	return -1;
}

/* Length (in UTF-16 code units, capped at 40) of the candidate GUID run at ws,
   stopping at a NUL or '}'. base is ws's byte offset within the blob. */
static uint32_t utf16_run_len(uint16_t const *ws, uint32_t base, uint32_t blob_len) {
	uint32_t wc;
	for (wc = 0; wc < 40 && base + wc * 2 + 1 < blob_len; wc++)
		if (ws [wc] == 0 || ws [wc] == '}') break;
	return wc;
}

static int scan_guid_utf16(uint8_t const *blob, uint32_t blob_len, uint8_t out [16]) {
	uint32_t i;
	for (i = 0; i + 72 < blob_len; i += 2) {
		uint16_t const *ws = ( uint16_t const * ) (blob + i);
		uint32_t        wc;
		if (ws [0] != '{' && !(ws [0] >= '0' && ws [0] <= '9')) continue;
		wc = utf16_run_len(ws, i, blob_len);
		if (wc >= 36 && parse_guid_utf16le(ws, wc, out) == 0) return 0;
	}
	return -1;
}

static int scan_blob_for_guid(uint8_t const *blob, uint32_t blob_len, uint8_t out [16]) {
	if (!blob || blob_len < 18) return -1;
	if (scan_guid_binary(blob, blob_len, out) == 0) return 0;
	if (scan_guid_ascii(blob, blob_len, out) == 0) return 0;
	if (scan_guid_utf16(blob, blob_len, out) == 0) return 0;
	return -1;
}

/* Strips the ECMA compressed-unsigned length prefix from a #Blob entry so the
   caller scans the CustomAttribute value (0x01 0x00 prolog ...) directly. The
   prefix is 1/2/4 bytes selected by the top bits of the first byte. */
static void blob_skip_length_prefix(uint8_t const **blob, uint32_t *blob_len) {
	uint8_t const *p   = *blob;
	uint32_t       len = *blob_len;
	uint8_t        b0;
	uint32_t       skip = 0, vlen = 0;

	if (len < 1) return;
	b0 = p [0];
	if ((b0 & 0x80u) == 0) {
		skip = 1;
		vlen = b0 & 0x7fu;
	}
	else if ((b0 & 0xc0u) == 0x80u && len >= 2) {
		skip = 2;
		vlen = (( uint32_t ) (b0 & 0x3fu) << 8) | p [1];
	}
	else if (len >= 4) {
		skip = 4;
		vlen = (( uint32_t ) (b0 & 0x1fu) << 24) | (( uint32_t ) p [1] << 16) | (( uint32_t ) p [2] << 8) | p [3];
	}
	if (skip && skip + vlen <= len) {
		*blob     = p + skip;
		*blob_len = vlen;
	}
}

/* Coded-index widths MUST match row_size_table (case 12): the width depends
   on (maxRows << tagBits), not maxRows alone. HasCustomAttribute has 5 tag
   bits, CustomAttributeType has 3. Using idx_bytes (plain >0xffff) here
   under-sized the parent field whenever a constituent table had 2048..65535
   rows, misaligning every row read -- so no GUID was ever found. */
static uint8_t custom_attr_parent_ix(winmd_tables const *t) {
	static int const ha []  = {6, 4, 1, 2, 8, 9, 10, 0, 14, 23, 20, 17, 26, 27, 32, 35, 38, 39, 40, 42, 44, 43};
	uint32_t         max_ha = 0;
	uint32_t         i;
	for (i = 0; i < 22u; i++)
		if (t->rows [ha [i]] > max_ha) max_ha = t->rows [ha [i]];
	return ( uint8_t ) ((max_ha << 5) > 0xffffu ? 4u : 2u);
}

static uint8_t custom_attr_type_ix(winmd_tables const *t) {
	uint32_t max_ct = t->rows [6];
	if (t->rows [10] > max_ct) max_ct = t->rows [10];
	return ( uint8_t ) ((max_ct << 3) > 0xffffu ? 4u : 2u);
}

/* CustomAttribute column widths (Parent coded index, Type coded index, Value
   blob index), all sized exactly as row_size_table computes them. */
typedef struct {
	uint8_t parent_ix;
	uint8_t type_ix;
	uint8_t blob_ix;
} ca_cols;

/* Extracts the GUID from a single CustomAttribute row owned by typedef_row1.
   Returns 0 and fills out on success, 1 if the row is not a match, -1 on a
   blob that matched the owner but yielded no GUID. */
static int custom_attr_guid(winmd_meta const *m, uint8_t const *row, uint32_t typedef_row1, ca_cols c,
                            uint8_t out [16]) {
	uint32_t       parent;
	uint32_t       blob_off;
	uint8_t const *blob;
	uint32_t       blob_len;

	parent = read_idx(row, c.parent_ix);
	if ((parent & 31u) != 3u) return 1;
	if ((parent >> 5) != typedef_row1) return 1;
	blob_off = read_idx(row + c.parent_ix + c.type_ix, c.blob_ix);
	if (!blob_off || blob_off >= m->blobs.size) return 1;
	blob     = m->blobs.data + blob_off;
	blob_len = ( uint32_t ) m->blobs.size - blob_off;
	blob_skip_length_prefix(&blob, &blob_len);
	return scan_blob_for_guid(blob, blob_len, out) == 0 ? 0 : 1;
}

int winmd_typedef_uuid(winmd_meta const *m, uint32_t typedef_row1, uint8_t out [16]) {
	winmd_tables const *t;
	uint32_t            rows;
	uint32_t            i;
	ca_cols             c;

	if (!m || !out || !typedef_row1) return -1;
	t    = &m->tabs;
	rows = t->rows [WINMD_TBL_CUSTOMATTRIBUTE];
	if (!rows) return -1;
	c.parent_ix = custom_attr_parent_ix(t);
	c.type_ix   = custom_attr_type_ix(t);
	c.blob_ix   = t->blob_ix;
	for (i = 1; i <= rows; i++) {
		uint8_t const *row = winmd_row_ptr(t, WINMD_TBL_CUSTOMATTRIBUTE, i);
		if (!row) continue;
		if (custom_attr_guid(m, row, typedef_row1, c, out) == 0) return 0;
	}
	return -1;
}

void winmd_close(winmd_db *db) {
	uint32_t i;
	if (!db) return;
	if (db->typedefs) {
		for (i = 0; i < db->typedef_count; i++) {
			free(db->typedefs [i].name);
			free(db->typedefs [i].namespace_name);
		}
		free(db->typedefs);
	}
	if (db->methods) {
		for (i = 0; i < db->method_count; i++) {
			free(db->methods [i].name);
			free(db->methods [i].ret_c_type);
			free(db->methods [i].params_c);
		}
		free(db->methods);
	}
	free(db->meta);
	free(db->file_data);
	memset(db, 0, sizeof(*db));
}
