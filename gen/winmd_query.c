#include "winmd_int.h"
#include "winmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_idx_local(uint8_t const *p, uint8_t sz) {
	if (sz == 4)
		return ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8) | (( uint32_t ) p [2] << 16) | (( uint32_t ) p [3] << 24);
	return ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8);
}

/* ECMA-335 compressed integer: header byte count is implied by the top bits of bp[0] alone. */
static uint32_t blob_prefix_size(uint8_t b0) {
	if (!(b0 & 0x80u)) return 1;
	if ((b0 & 0xc0u) == 0x80u) return 2;
	return 4;
}

/* Decode a compressed blob length prefix at bp; returns header byte count, *len gets the length. */
static uint32_t blob_len_prefix(uint8_t const *bp, uint32_t *len) {
	uint32_t pos = blob_prefix_size(bp [0]);
	if (pos == 1) *len = bp [0];
	else if (pos == 2) *len = (( uint32_t ) (bp [0] & 0x3fu) << 8) | bp [1];
	else
		*len = (( uint32_t ) (bp [0] & 0x1fu) << 24) | (( uint32_t ) bp [1] << 16) | (( uint32_t ) bp [2] << 8)
		     | bp [3];
	return pos;
}

static uint32_t cd_rows_local(winmd_tables const *t, int const *ids, int n, int tag) {
	uint32_t m = 0;
	int      i;
	for (i = 0; i < n; i++)
		if (t->rows [ids [i]] > m) m = t->rows [ids [i]];
	return (m << tag) > 0xffff ? 4u : 2u;
}

static char const *str_heap(winmd_heap const *strings, uint32_t ix) {
	if (!strings || !strings->data || !ix || ix >= strings->size) return "";
	return ( char const * ) (strings->data + ix);
}

static char *str_dup_heap(winmd_heap const *strings, uint32_t ix) {
	char const *s;
	char       *p;
	size_t      n;
	if (!ix) {
		p = ( char * ) malloc(1);
		if (p) p [0] = '\0';
		return p;
	}
	s = str_heap(strings, ix);
	n = strlen(s);
	p = ( char * ) malloc(n + 1);
	if (p) memcpy(p, s, n + 1);
	return p;
}

static uint32_t typedef_extends_off(winmd_tables const *t) { return 4u + ( uint32_t ) t->string_ix * 2u; }

static uint32_t typedef_field_list_off(winmd_tables const *t) {
	static int const cd [] = {2, 1, 27};
	return typedef_extends_off(t) + cd_rows_local(t, cd, 3, 2);
}

static uint32_t typedef_field_list(winmd_tables const *t, uint32_t row1) {
	uint8_t const *p = winmd_row_ptr(t, WINMD_TBL_TYPEDEF, row1);
	if (!p) return 0;
	return read_idx_local(p + typedef_field_list_off(t), t->table_ix [WINMD_TBL_FIELD]);
}

static uint32_t method_param_list(winmd_tables const *t, uint32_t method_row1) {
	uint8_t const *p = winmd_row_ptr(t, WINMD_TBL_METHODDEF, method_row1);
	uint32_t       pos;
	if (!p) return 0;
	pos = 8u + t->string_ix + t->blob_ix;
	return read_idx_local(p + pos, t->table_ix [WINMD_TBL_PARAM]);
}

uint32_t winmd_typedef_token(uint32_t typedef_row1) { return 0x02000000u | typedef_row1; }

uint32_t winmd_typedef_extends_coded(winmd_meta const *m, uint32_t typedef_row1) {
	uint8_t const   *p;
	static int const cd_tdr [] = {2, 1, 27};
	uint32_t         cd_sz;
	if (!m || !typedef_row1) return 0;
	p = winmd_row_ptr(&m->tabs, WINMD_TBL_TYPEDEF, typedef_row1);
	if (!p) return 0;
	cd_sz = cd_rows_local(&m->tabs, cd_tdr, 3, 2);
	return read_idx_local(p + typedef_extends_off(&m->tabs), ( uint8_t ) cd_sz);
}

/* Read the Name then Namespace string columns starting at offset pos in row p, and format "<ns>.<name>". */
static void format_type_full_name(winmd_meta const *m, uint8_t const *p, uint32_t pos, char *buf, size_t bufsz) {
	char const *name = str_heap(&m->strings, read_idx_local(p + pos, m->tabs.string_ix));
	char const *ns   = str_heap(&m->strings, read_idx_local(p + pos + m->tabs.string_ix, m->tabs.string_ix));
	if (ns && ns [0]) snprintf(buf, bufsz, "%s.%s", ns, name ? name : "");
	else snprintf(buf, bufsz, "%s", name ? name : "");
}

/* TypeRefs put a ResolutionScope coded index before the Name column; TypeDefs put 4-byte Flags. */
static uint32_t typeref_name_off(winmd_tables const *t) {
	static int const cd_tdr [] = {2, 1, 27};
	return cd_rows_local(t, cd_tdr, 3, 2);
}

int winmd_coded_type_full_name(winmd_meta const *m, uint32_t coded, char *buf, size_t bufsz) {
	uint32_t       tag;
	uint32_t       rid;
	uint8_t const *p;

	if (!m || !buf || bufsz < 2) return -1;
	tag = coded & 3u;
	rid = coded >> 2;
	if (!rid || tag > 1u) {
		buf [0] = '\0';
		return 0;
	}
	if (tag == 0) {
		p = winmd_row_ptr(&m->tabs, WINMD_TBL_TYPEDEF, rid);
		if (!p) return -1;
		format_type_full_name(m, p, 4, buf, bufsz);
		return 0;
	}
	p = winmd_row_ptr(&m->tabs, WINMD_TBL_TYPEREF, rid);
	if (!p) return -1;
	format_type_full_name(m, p, typeref_name_off(&m->tabs), buf, bufsz);
	return 0;
}

bool winmd_coded_typedef_is_enum(winmd_meta const *m, uint32_t coded) {
	uint32_t       tag;
	uint32_t       rid;
	uint8_t const *p;
	uint32_t       flags;

	if (!m) return false;
	tag = coded & 3u;
	rid = coded >> 2;
	if (!rid || tag != 0u) return false;
	p = winmd_row_ptr(&m->tabs, WINMD_TBL_TYPEDEF, rid);
	if (!p) return false;
	flags = ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8) | (( uint32_t ) p [2] << 16) | (( uint32_t ) p [3] << 24);
	return (flags & 0xe0u) == 0x100u;
}

/* If InterfaceImpl row i has Class == typedef_row1, store its Interface coded index in *iface; 1 = match. */
static int iface_impl_match(winmd_meta const *m, uint32_t i, uint32_t typedef_row1, uint32_t *iface) {
	winmd_tables const *t = &m->tabs;
	uint8_t const      *p = winmd_row_ptr(t, WINMD_TBL_INTERFACEIMPL, i);
	uint32_t            cls;
	if (!p) return 0;
	cls = read_idx_local(p, t->table_ix [WINMD_TBL_TYPEDEF]);
	if (cls != typedef_row1) return 0;
	*iface = read_idx_local(p + t->table_ix [WINMD_TBL_TYPEDEF], ( uint8_t ) typeref_name_off(t));
	return 1;
}

int winmd_typedef_interface_impls(winmd_meta const *m, uint32_t typedef_row1, uint32_t **out, uint32_t *count) {
	uint32_t  n;
	uint32_t  i;
	uint32_t  k = 0;
	uint32_t *buf;

	if (!m || !out || !count) return -1;
	*out   = NULL;
	*count = 0;
	n      = m->tabs.rows [WINMD_TBL_INTERFACEIMPL];
	if (!n) return 0;
	buf = ( uint32_t * ) malloc(n * sizeof(uint32_t));
	if (!buf) return -1;
	for (i = 1; i <= n; i++) {
		uint32_t iface;
		if (iface_impl_match(m, i, typedef_row1, &iface)) buf [k++] = iface;
	}
	if (!k) {
		free(buf);
		return 0;
	}
	*out = ( uint32_t * ) realloc(buf, k * sizeof(uint32_t));
	if (!*out) *out = buf;
	*count = k;
	return 0;
}

void       winmd_iface_tokens_free(uint32_t *p) { free(p); }

/* Decode a constant blob payload of ELEMENT_TYPE ctype. Covers the integer
 * element types that back WinRT enums — Int32 (0x08) and UInt32 (0x09, used by
 * flag enums) are the common ones; other widths handled for completeness. */
static int decode_const_payload(uint8_t ctype, uint8_t const *blob, int64_t *value) {
	switch (ctype) {
	case 0x02: /* BOOLEAN */
	case 0x05: *value = ( int64_t ) blob [0]; return 0;          /* U1 */
	case 0x04: *value = ( int64_t ) ( int8_t ) blob [0]; return 0; /* I1 */
	case 0x06: /* I2 */
	case 0x07: { /* U2 */
		uint16_t v = ( uint16_t ) (( uint16_t ) blob [0] | (( uint16_t ) blob [1] << 8));
		*value     = ctype == 0x07 ? ( int64_t ) v : ( int64_t ) ( int16_t ) v;
		return 0;
	}
	case 0x08: /* I4 */
	case 0x09: { /* U4 */
		uint32_t v
		  = ( uint32_t ) blob [0] | (( uint32_t ) blob [1] << 8) | (( uint32_t ) blob [2] << 16) | (( uint32_t ) blob [3] << 24);
		*value = ctype == 0x09 ? ( int64_t ) v : ( int64_t ) ( int32_t ) v;
		return 0;
	}
	case 0x0a: /* I8 */
	case 0x0b: { /* U8 */
		uint64_t v = 0;
		int      j;
		for (j = 0; j < 8; j++) v |= ( uint64_t ) blob [j] << (j * 8);
		*value = ( int64_t ) v;
		return 0;
	}
	default: return -1;
	}
}

/* HasConstant coded-index size for the Constant.Parent column. */
static uint32_t const_parent_cd(winmd_tables const *t) {
	static int const hc [] = {4, 8, 23};
	return cd_rows_local(t, hc, 3, 2);
}

/* Decode Constant row i if it targets field_row1: 0/-1 = decoded result, -2 = not this field. */
static int const_row_for_field(winmd_meta const *m, uint32_t i, uint32_t field_row1, int64_t *value) {
	uint32_t       cd = const_parent_cd(&m->tabs);
	uint8_t const *p  = winmd_row_ptr(&m->tabs, WINMD_TBL_CONSTANT, i);
	uint32_t       parent;
	uint32_t       blob_ix;
	uint8_t const *bp;
	uint32_t       blen;
	if (!p) return -2;
	/* Constant row layout (ECMA-335 II.22.9): Type(1 byte) + padding(1 byte),
	 * then Parent (HasConstant coded index, cd bytes), then Value (blob index). */
	parent = read_idx_local(p + 2, ( uint8_t ) cd);
	if ((parent & 3u) != 0 || (parent >> 2) != field_row1) return -2;
	blob_ix = read_idx_local(p + 2 + cd, m->tabs.blob_ix);
	if (!blob_ix || blob_ix >= m->blobs.size) return -1;
	bp = m->blobs.data + blob_ix;
	return decode_const_payload(p [0], bp + blob_len_prefix(bp, &blen), value);
}

static int field_constant_value(winmd_meta const *m, uint32_t field_row1, int64_t *value) {
	uint32_t n = m->tabs.rows [WINMD_TBL_CONSTANT];
	uint32_t i;
	for (i = 1; i <= n; i++) {
		int r = const_row_for_field(m, i, field_row1, value);
		if (r != -2) return r;
	}
	return -1;
}

/* Populate one winmd_field_info from Field row; -1 if the row's name dup failed, 0 ok. */
static int fill_field_info(winmd_meta const *m, uint32_t row, winmd_field_info *f) {
	uint8_t const *p = winmd_row_ptr(&m->tabs, WINMD_TBL_FIELD, row);
	uint32_t       name_ix;
	if (!p) return 0;
	f->flags       = ( uint32_t ) read_idx_local(p, 2);
	name_ix        = read_idx_local(p + 2, m->tabs.string_ix);
	f->name        = str_dup_heap(&m->strings, name_ix);
	f->sig_blob    = read_idx_local(p + 2 + m->tabs.string_ix, m->tabs.blob_ix);
	f->has_const   = false;
	f->const_value = 0;
	if (field_constant_value(m, row, &f->const_value) == 0) f->has_const = true;
	return f->name ? 0 : -1;
}

int winmd_typedef_fields(winmd_meta const *m, uint32_t typedef_row1, winmd_field_info **out, uint32_t *count) {
	uint32_t          start;
	uint32_t          end;
	uint32_t          row;
	winmd_field_info *fields;
	uint32_t          n    = 0;
	uint32_t          td_n = m->tabs.rows [WINMD_TBL_TYPEDEF];

	if (!m || !out || !count) return -1;
	*out   = NULL;
	*count = 0;
	start  = typedef_field_list(&m->tabs, typedef_row1);
	if (typedef_row1 < td_n) end = typedef_field_list(&m->tabs, typedef_row1 + 1);
	else end = m->tabs.rows [WINMD_TBL_FIELD] + 1;
	if (!start || start >= end) return 0;
	fields = ( winmd_field_info * ) calloc(end - start, sizeof(*fields));
	if (!fields) return -1;
	for (row = start; row < end; row++) {
		uint8_t const *p = winmd_row_ptr(&m->tabs, WINMD_TBL_FIELD, row);
		if (!p) continue;
		if (fill_field_info(m, row, &fields [n]) != 0) {
			winmd_field_info_free(fields, n);
			return -1;
		}
		n++;
	}
	*out   = fields;
	*count = n;
	return 0;
}

void winmd_field_info_free(winmd_field_info *fields, uint32_t count) {
	uint32_t i;
	if (!fields) return;
	for (i = 0; i < count; i++) free(fields [i].name);
	free(fields);
}

/* Highest Param.Sequence (1..256) across rows [start,end); 0 if none. */
static uint32_t param_max_seq(winmd_meta const *m, uint32_t start, uint32_t end) {
	uint32_t row;
	uint32_t max_seq = 0;
	for (row = start; row < end; row++) {
		uint8_t const *p = winmd_row_ptr(&m->tabs, WINMD_TBL_PARAM, row);
		uint32_t       seq;
		if (!p) continue;
		seq = read_idx_local(p + 2, 2);
		if (seq == 0 || seq > 256u) continue;
		if (seq > max_seq) max_seq = seq;
	}
	return max_seq;
}

/* Fill arr[seq-1] with duped param names from rows [start,end); -1 on OOM (arr freed), 0 ok. */
static int param_fill_names(winmd_meta const *m, uint32_t start, uint32_t end, char **arr, uint32_t max_seq) {
	uint32_t row;
	for (row = start; row < end; row++) {
		uint8_t const *p = winmd_row_ptr(&m->tabs, WINMD_TBL_PARAM, row);
		uint32_t       seq;
		uint32_t       name_ix;
		if (!p) continue;
		seq = read_idx_local(p + 2, 2);
		if (seq == 0 || seq > max_seq) continue;
		name_ix       = read_idx_local(p + 4, m->tabs.string_ix);
		arr [seq - 1] = str_dup_heap(&m->strings, name_ix);
		if (!arr [seq - 1]) {
			winmd_param_names_free(arr, max_seq);
			return -1;
		}
	}
	return 0;
}

int winmd_method_param_names(winmd_meta const *m, uint32_t method_row1, char ***names, uint32_t *count) {
	uint32_t start;
	uint32_t end;
	char   **arr;
	uint32_t max_seq;

	if (!m || !names || !count) return -1;
	*names = NULL;
	*count = 0;
	start  = method_param_list(&m->tabs, method_row1);
	if (method_row1 < m->tabs.rows [WINMD_TBL_METHODDEF]) end = method_param_list(&m->tabs, method_row1 + 1);
	else end = m->tabs.rows [WINMD_TBL_PARAM] + 1;
	if (!start || start >= end) return 0;
	max_seq = param_max_seq(m, start, end);
	if (!max_seq) return 0;
	arr = ( char ** ) calloc(max_seq, sizeof(char *));
	if (!arr) return -1;
	if (param_fill_names(m, start, end, arr, max_seq) != 0) return -1;
	*names = arr;
	*count = max_seq;
	return 0;
}

void winmd_param_names_free(char **names, uint32_t count) {
	uint32_t i;
	if (!names) return;
	for (i = 0; i < count; i++) free(names [i]);
	free(names);
}

static bool ns_prefix_ok(char const *ns, char const *prefix) {
	size_t plen;
	if (!prefix || !*prefix) return true;
	if (!ns || !*ns) return false;
	plen = strlen(prefix);
	if (strncmp(ns, prefix, plen) != 0) return false;
	return ns [plen] == '\0' || ns [plen] == '.';
}

static int ns_list_has(char **list, uint32_t n, char const *ns) {
	uint32_t i;
	for (i = 0; i < n; i++)
		if (strcmp(list [i], ns) == 0) return 1;
	return 0;
}

static int ns_cmp(void const *a, void const *b) {
	return strcmp(*( char const *const * ) a, *( char const *const * ) b);
}

/* Append a copy of ns to the growable string list; -1 on OOM (list freed via *list), 0 ok. */
static int ns_list_append(char ***list, uint32_t *cap, uint32_t *n, char const *ns) {
	char *copy;
	if (*n >= *cap) {
		uint32_t nc    = *cap ? *cap * 2 : 64;
		char   **grown = ( char ** ) realloc(*list, nc * sizeof(char *));
		if (!grown) return -1;
		*list = grown;
		*cap  = nc;
	}
	copy = ( char * ) malloc(strlen(ns) + 1);
	if (!copy) return -1;
	memcpy(copy, ns, strlen(ns) + 1);
	(*list) [(*n)++] = copy;
	return 0;
}

/* True if ns passes the prefix filter, is non-empty, and is not already in list. */
static int ns_should_add(char **list, uint32_t n, char const *ns, char const *prefix) {
	if (!ns_prefix_ok(ns, prefix)) return 0;
	if (!ns || !*ns) return 0;
	return !ns_list_has(list, n, ns);
}

int winmd_collect_namespaces(winmd_db const *db, char const *prefix, char ***out, uint32_t *count) {
	char   **list = NULL;
	uint32_t cap  = 0;
	uint32_t n    = 0;
	uint32_t i;

	if (!db || !out || !count) return -1;
	*out   = NULL;
	*count = 0;
	for (i = 0; i < db->typedef_count; i++) {
		char const *ns = db->typedefs [i].namespace_name;
		if (!ns_should_add(list, n, ns, prefix)) continue;
		if (ns_list_append(&list, &cap, &n, ns) != 0) {
			winmd_namespaces_free(list, n);
			return -1;
		}
	}
	if (!n) return 0;
	qsort(list, n, sizeof(char *), ns_cmp);
	*out   = list;
	*count = n;
	return 0;
}

void winmd_namespaces_free(char **names, uint32_t count) {
	uint32_t i;
	if (!names) return;
	for (i = 0; i < count; i++) free(names [i]);
	free(names);
}

uint8_t const *winmd_blob_at(winmd_meta const *m, uint32_t blob_ix, uint32_t *len_out) {
	uint8_t const *bp;
	uint32_t       blen;
	uint32_t       pos;
	if (len_out) *len_out = 0;
	if (!m || !blob_ix || blob_ix >= m->blobs.size) return NULL;
	bp  = m->blobs.data + blob_ix;
	pos = blob_prefix_size(bp [0]);
	/* The multi-byte prefix reads bytes past bp[0]: bound-check before decoding them. */
	if (pos > 1 && blob_ix + pos > m->blobs.size) return NULL;
	blob_len_prefix(bp, &blen);
	if (( uint64_t ) blob_ix + pos + blen > m->blobs.size) return NULL;
	if (len_out) *len_out = blen;
	return bp + pos;
}

uint32_t winmd_typespec_sig_blob(winmd_meta const *m, uint32_t typespec_row1) {
	uint8_t const *sp;
	if (!m) return 0;
	sp = winmd_row_ptr(&m->tabs, WINMD_TBL_TYPESPEC, typespec_row1);
	if (!sp) return 0;
	return read_idx_local(sp, m->tabs.blob_ix);
}

int winmd_coded_to_typedef_row(winmd_db const *db, winmd_meta const *m, uint32_t coded, uint32_t *row1) {
	uint32_t tag = coded & 3u;
	uint32_t rid = coded >> 2;
	if (!rid || !row1) return -1;
	if (tag == 0u) { /* TypeDef */
		*row1 = rid;
		return 0;
	}
	if (tag == 1u) { /* TypeRef: match by full name */
		char     full [256];
		uint32_t r;
		if (winmd_coded_type_full_name(m, coded, full, sizeof(full)) != 0) return -1;
		r = winmd_typedef_find_by_name(db, full);
		if (!r) return -1;
		*row1 = r;
		return 0;
	}
	return -1; /* TypeSpec or unknown */
}

/* Map a base type's full name to a kind; WINMD_KIND_CLASS if none of the well-known bases match. */
static int kind_from_base_name(char const *base) {
	static struct {
		char const *name;
		int         kind;
	} const map [] = {
	  {"System.Enum",			  WINMD_KIND_ENUM	  },
	  {"System.ValueType",		  WINMD_KIND_STRUCT	  },
	  {"System.MulticastDelegate", WINMD_KIND_DELEGATE},
	  {"System.Delegate",		  WINMD_KIND_DELEGATE},
	};
	size_t i;
	for (i = 0; i < sizeof(map) / sizeof(map [0]); i++)
		if (strcmp(base, map [i].name) == 0) return map [i].kind;
	return WINMD_KIND_CLASS;
}

static uint32_t typedef_flags(winmd_meta const *m, uint32_t typedef_row1) {
	uint8_t const *p = winmd_row_ptr(&m->tabs, WINMD_TBL_TYPEDEF, typedef_row1);
	if (!p) return 0;
	return ( uint32_t ) p [0] | (( uint32_t ) p [1] << 8) | (( uint32_t ) p [2] << 16) | (( uint32_t ) p [3] << 24);
}

int winmd_typedef_classify(winmd_meta const *m, uint32_t typedef_row1) {
	uint32_t flags;
	uint32_t ext;
	char     base [128];

	if (!m || !typedef_row1) return WINMD_KIND_OTHER;
	if (!winmd_row_ptr(&m->tabs, WINMD_TBL_TYPEDEF, typedef_row1)) return WINMD_KIND_OTHER;
	flags = typedef_flags(m, typedef_row1);
	if ((flags & 0xe0u) == 0x100u) return WINMD_KIND_ENUM; /* ClassSemantics Enumeration */
	if (flags & 0x20u) return WINMD_KIND_INTERFACE;        /* Interface */
	ext = winmd_typedef_extends_coded(m, typedef_row1);
	if (winmd_coded_type_full_name(m, ext, base, sizeof(base)) != 0) return WINMD_KIND_CLASS;
	return kind_from_base_name(base);
}

uint32_t winmd_typedef_default_iface(winmd_meta const *m, uint32_t typedef_row1) {
	uint32_t ii_n;
	uint32_t i;

	if (!m || !typedef_row1) return 0;
	ii_n = m->tabs.rows [WINMD_TBL_INTERFACEIMPL];
	for (i = 1; i <= ii_n; i++) {
		uint32_t iface;
		if (!iface_impl_match(m, i, typedef_row1, &iface)) continue;
		/* InterfaceImpl == HasCustomAttribute tag 5; [Default] marks the default interface. */
		if (winmd_ca_has_attr_on(m, 5u, i, "DefaultAttribute")) return iface;
	}
	return 0;
}

uint32_t winmd_typedef_find_by_name(winmd_db const *db, char const *winrt_full_name) {
	uint32_t i;
	if (!db || !winrt_full_name) return 0;
	for (i = 0; i < db->typedef_count; i++) {
		char const *ns   = db->typedefs [i].namespace_name;
		char const *name = db->typedefs [i].name;
		char        full [256];
		if (!name) continue;
		if (ns && ns [0]) snprintf(full, sizeof(full), "%s.%s", ns, name);
		else snprintf(full, sizeof(full), "%s", name);
		if (strcmp(full, winrt_full_name) == 0) return i + 1;
	}
	return 0;
}
