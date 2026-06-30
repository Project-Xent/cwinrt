#include "winmd_int.h"

#include <stdlib.h>
#include <string.h>

/* One correct copy of the low-level ECMA-335 metadata primitives shared by
   winmd.c, winmd_attr.c and winmd_query.c. Each used to be re-implemented per
   file with subtle divergences (a missing FieldList term in the method-list
   walk, an unbounded strlen string dup, CustomAttribute coded-index widths that
   ignored the tag bits). Keeping a single definition here removes that drift. */

uint32_t winmd_read_idx(uint8_t const *p, uint8_t sz) {
	if (sz == 4)
		return ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8) | (( uint32_t ) p [2] << 16) | (( uint32_t ) p [3] << 24);
	return ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8);
}

uint32_t winmd_idx_bytes(uint32_t rows) { return rows > 0xffffu ? 4u : 2u; }

/* Coded-index column width: 4 bytes if (max_rows << tag_bits) overflows 16 bits,
   else 2. The shift is essential — a constituent table with 2048..65535 rows
   needs 4 bytes once the tag bits are added, which a plain >0xffff test misses. */
uint32_t winmd_cd_rows(winmd_tables const *t, int const *ids, int n, int tag_bits) {
	uint32_t m = 0;
	int      i;
	for (i = 0; i < n; i++)
		if (t->rows [ids [i]] > m) m = t->rows [ids [i]];
	return (m << tag_bits) > 0xffffu ? 4u : 2u;
}

/* CustomAttribute Parent column: HasCustomAttribute coded index, 5 tag bits. */
uint8_t winmd_ca_parent_ix(winmd_tables const *t) {
	static int const ha [] = {6, 4, 1, 2, 8, 9, 10, 0, 14, 23, 20, 17, 26, 27, 32, 35, 38, 39, 40, 42, 44, 43};
	return ( uint8_t ) winmd_cd_rows(t, ha, ( int ) (sizeof(ha) / sizeof(ha [0])), 5);
}

/* CustomAttribute Type column: CustomAttributeType coded index, 3 tag bits. */
uint8_t winmd_ca_type_ix(winmd_tables const *t) {
	static int const ct [] = {6, 10};
	return ( uint8_t ) winmd_cd_rows(t, ct, 2, 3);
}

char const *winmd_str_at(winmd_heap const *strings, uint32_t ix) {
	if (!strings || !strings->data || !ix || ix >= strings->size) return "";
	return ( char const * ) (strings->data + ix);
}

/* NUL-bounded owned copy from #Strings. "" for ix 0; NULL on a bad heap, an
   out-of-range ix, or OOM. strnlen caps the read at the heap end so an
   unterminated trailing string cannot walk past the stream. */
char *winmd_str_dup(winmd_heap const *strings, uint32_t ix) {
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
	memcpy(p, s, n);
	p [n] = '\0';
	return p;
}

/* ECMA-335 compressed unsigned at p with `avail` bytes readable. Returns the
   header byte count (1/2/4) and stores *value, or 0 when `avail` is too small. */
uint32_t winmd_decompress(uint8_t const *p, size_t avail, uint32_t *value) {
	uint8_t b0;
	if (!p || !value || avail < 1) return 0;
	b0 = p [0];
	if ((b0 & 0x80u) == 0) {
		*value = b0;
		return 1;
	}
	if ((b0 & 0xc0u) == 0x80u) {
		if (avail < 2) return 0;
		*value = (( uint32_t ) (b0 & 0x3fu) << 8) | p [1];
		return 2;
	}
	if (avail < 4) return 0;
	*value = (( uint32_t ) (b0 & 0x1fu) << 24) | (( uint32_t ) p [1] << 16) | (( uint32_t ) p [2] << 8) | p [3];
	return 4;
}

/* TypeDef.MethodList (1-based MethodDef row) for typedef row1; 0 if invalid.
   Row layout (ECMA II.22.37): Flags(4) TypeName TypeNamespace Extends FieldList
   MethodList — MethodList sits AFTER FieldList, so the FieldList width must be
   included in the offset. */
uint32_t winmd_typedef_method_list(winmd_tables const *t, uint32_t row1) {
	static int const cd_tdr [] = {2, 1, 27};
	uint8_t const   *p;
	uint32_t         pos;
	if (!row1) return 0;
	p = winmd_row_ptr(t, WINMD_TBL_TYPEDEF, row1);
	if (!p) return 0;
	pos  = 4u + ( uint32_t ) t->string_ix * 2u;
	pos += winmd_cd_rows(t, cd_tdr, 3, 2);
	pos += t->table_ix [WINMD_TBL_FIELD];
	return winmd_read_idx(p + pos, t->table_ix [WINMD_TBL_METHODDEF]);
}
