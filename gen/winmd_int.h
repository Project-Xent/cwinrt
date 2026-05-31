#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct winmd_db; /* defined in winmd.h; forward-declared so param-list uses don't warn */

enum
{
	WINMD_NTABLES = 64,
};

enum
{
	WINMD_TBL_MODULE          = 0,
	WINMD_TBL_TYPEREF         = 1,
	WINMD_TBL_TYPEDEF         = 2,
	WINMD_TBL_FIELD           = 4,
	WINMD_TBL_METHODDEF       = 6,
	WINMD_TBL_PARAM           = 8,
	WINMD_TBL_INTERFACEIMPL   = 9,
	WINMD_TBL_CONSTANT        = 11,
	WINMD_TBL_CUSTOMATTRIBUTE = 12,
	WINMD_TBL_TYPESPEC        = 27,
	WINMD_TBL_NESTEDCLASS     = 41,
};

typedef struct winmd_heap {
	uint8_t *data;
	size_t   size;
} winmd_heap;

typedef struct winmd_tables {
	uint8_t *stream;
	uint8_t *rows_base;
	uint64_t valid;
	uint32_t rows [WINMD_NTABLES];
	uint32_t offset [WINMD_NTABLES];
	uint8_t  string_ix;
	uint8_t  guid_ix;
	uint8_t  blob_ix;
	uint8_t  table_ix [WINMD_NTABLES];
} winmd_tables;

typedef struct winmd_meta {
	winmd_tables tabs;
	winmd_heap   strings;
	winmd_heap   blobs;
} winmd_meta;

typedef struct winmd_field_info {
	char    *name;
	uint32_t flags;
	uint32_t sig_blob;
	int64_t  const_value;
	bool     has_const;
} winmd_field_info;

uint8_t const *winmd_row_ptr(winmd_tables const *t, int tbl, uint32_t row1);

int            winmd_type_full_name(winmd_meta const *m, uint32_t coded_token, char *buf, size_t bufsz);

/* TypeDef row1 (1-based) -> coded extends token (TypeDef/TypeRef/TypeSpec), 0 if invalid row. */
uint32_t       winmd_typedef_extends_coded(winmd_meta const *m, uint32_t typedef_row1);

/* Allocates *out (caller: winmd_iface_tokens_free). Each element is coded TypeDefOrRef token. */
int      winmd_typedef_interface_impls(winmd_meta const *m, uint32_t typedef_row1, uint32_t **out, uint32_t *count);

void     winmd_iface_tokens_free(uint32_t *p);

/* Allocates *out array and field names (caller: winmd_field_info_free). */
int      winmd_typedef_fields(winmd_meta const *m, uint32_t typedef_row1, winmd_field_info **out, uint32_t *count);

void     winmd_field_info_free(winmd_field_info *fields, uint32_t count);

/* TypeDef row1 -> [uuid] from CustomAttribute, 0 on success. */
int      winmd_typedef_uuid(winmd_meta const *m, uint32_t typedef_row1, uint8_t out [16]);

/* MethodDef row1 -> [Overload] fixed string argument; -1 if absent. */
int      winmd_method_overload_name(winmd_meta const *m, uint32_t method_row1, char *buf, size_t bufsz);

/* MethodDef row1 has [DefaultOverload] custom attribute. */
bool     winmd_method_is_default_overload(winmd_meta const *m, uint32_t method_row1);

/* TypeDef row1 has [activatable] custom attribute. */
bool     winmd_typedef_is_activatable(winmd_meta const *m, uint32_t typedef_row1);

/* Param names with Sequence >= 1, sorted by sequence. Allocates *names (winmd_param_names_free). */
int      winmd_method_param_names(winmd_meta const *m, uint32_t method_row1, char ***names, uint32_t *count);

void     winmd_param_names_free(char **names, uint32_t count);

/* Resolve coded TypeDefOrRef to WinRT full name (namespace.name). */
int      winmd_coded_type_full_name(winmd_meta const *m, uint32_t coded, char *buf, size_t bufsz);

/* True when coded token is a TypeDef with ClassSemantics Enumeration (0x100). */
bool     winmd_coded_typedef_is_enum(winmd_meta const *m, uint32_t coded);

/* typedef_row1 -> metadata token 0x02000000 | row */
uint32_t winmd_typedef_token(uint32_t typedef_row1);

/* Find the TypeDef row1 (1-based) whose WinRT full name == `winrt_full_name`
   (e.g. "Windows.Foundation.Collections.IVector`1"). 0 if not found. */
uint32_t winmd_typedef_find_by_name(const struct winmd_db *db, char const *winrt_full_name);

/* Pointer to a blob's contents (past the ECMA compressed-length prefix); sets
   *len_out to the content length. NULL on error. */
uint8_t const *winmd_blob_at(winmd_meta const *m, uint32_t blob_ix, uint32_t *len_out);

/* The blob-heap index of a TypeSpec row's signature (0 if invalid). */
uint32_t       winmd_typespec_sig_blob(winmd_meta const *m, uint32_t typespec_row1);

/* Resolve a coded TypeDefOrRef (tag 0=TypeDef,1=TypeRef,2=TypeSpec) to a TypeDef
   row1 in this file. TypeRef is matched by WinRT full name. Returns 0 and sets
   *row1 on success; -1 if unresolved (e.g. TypeSpec or missing). */
int      winmd_coded_to_typedef_row(const struct winmd_db *db, winmd_meta const *m, uint32_t coded, uint32_t *row1);

/* Coded extends token's high-byte flags say nothing about kind; classify here.
   Returns one of WINMD_KIND_*. */
enum
{
	WINMD_KIND_OTHER     = 0,
	WINMD_KIND_ENUM      = 1,
	WINMD_KIND_STRUCT    = 2,
	WINMD_KIND_INTERFACE = 3,
	WINMD_KIND_DELEGATE  = 4,
	WINMD_KIND_CLASS     = 5,
};
int      winmd_typedef_classify(winmd_meta const *m, uint32_t typedef_row1);

/* The default interface (coded TypeDefOrRef) of a runtimeclass — the
   InterfaceImpl carrying [Windows.Foundation.Metadata.Default]. 0 if none. */
uint32_t winmd_typedef_default_iface(winmd_meta const *m, uint32_t typedef_row1);

/* True if the row identified by (HasCustomAttribute tag, row1) carries a custom
   attribute whose constructor's declaring type short name == attr_short
   (post-mapping: e.g. "DefaultAttribute", "Activatable"). */
bool     winmd_ca_has_attr_on(winmd_meta const *m, uint32_t parent_tag, uint32_t parent_row1, char const *attr_short);

/* Unique namespace strings from TypeDef rows; optional prefix filter (e.g. "Windows"). */
int      winmd_collect_namespaces(const struct winmd_db *db, char const *prefix, char ***out, uint32_t *count);

void     winmd_namespaces_free(char **names, uint32_t count);
