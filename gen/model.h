#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Global registry of every enum across all namespaces (C name + members).
   A cross-namespace enum cannot be opaque-forward-declared in C (illegal) and
   relying on the defining header's include is fragile under include cycles
   (e.g. Storage <-> Storage.Streams). So each consumer emits the full enum
   definition inline (guarded), making it available regardless of include order.
   Structs remain opaque-forward-declared (legal) and value-by-value structs are
   pulled in via their defining header by the map stage. */
typedef struct cwinrt_enum_member_def {
	char   *name;  /* member identifier without the type prefix, e.g. "None" */
	int64_t value;
} cwinrt_enum_member_def;

typedef struct cwinrt_enum_def {
	char                   *cname; /* e.g. "WSTST_InputStreamOptions" */
	cwinrt_enum_member_def *members;
	uint32_t                member_count;
} cwinrt_enum_def;

typedef struct cwinrt_value_type_set {
	cwinrt_enum_def *enums; /* sorted by cname */
	uint32_t         enum_count;
} cwinrt_value_type_set;

static inline cwinrt_enum_def const *cwinrt_value_type_find_enum(cwinrt_value_type_set const *s, char const *cname) {
	uint32_t lo, hi;
	if (!s || !s->enums || !cname) return NULL;
	lo = 0;
	hi = s->enum_count;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		int      c   = strcmp(s->enums [mid].cname, cname);
		if (c < 0) lo = mid + 1;
		else if (c > 0) hi = mid;
		else return &s->enums [mid];
	}
	return NULL;
}

/* Stage boundaries: raw (parse) -> topo -> mapped -> emit. No C concepts in topo. */

typedef enum
{
	CWINRT_RAW_IFACE,
	CWINRT_RAW_CLASS,
	CWINRT_RAW_STRUCT,
	CWINRT_RAW_ENUM,
	CWINRT_RAW_DELEGATE,
} cwinrt_raw_kind;

typedef struct cwinrt_raw_field {
	char   *name;
	char   *c_type;
	int64_t enum_value;
	bool    has_enum_value;
	uint32_t type_token; /* coded TypeDefOrRef of the field's type, 0 if primitive */
} cwinrt_raw_field;

typedef struct cwinrt_raw_type {
	uint32_t          token;
	cwinrt_raw_kind   kind;
	char             *full_name;
	char             *ns;
	char             *short_name;
	uint32_t          flags;         /* TypeDef flags from winmd */
	uint8_t           uuid [16];     /* from [uuid] custom attribute, zero if unknown */
	bool              has_uuid;
	uint32_t          extends_token; /* TypeDef token of base, 0 if none */
	uint32_t         *iface_tokens;
	uint32_t          iface_count;
	cwinrt_raw_field *fields;
	uint32_t          field_count;
	uint32_t         *ref_tokens;
	uint32_t          ref_count;
	bool              is_activatable;
} cwinrt_raw_type;

typedef struct cwinrt_raw_method {
	uint32_t type_token;
	uint32_t method_token;
	char    *name;
	char    *overload_name;       /* WinRT [Overload] string, if any */
	char   **param_names;         /* Param table names (sequence >= 1), arena-owned */
	uint32_t param_name_count;
	bool     is_default_overload; /* [DefaultOverload] on this MethodDef */
	char    *c_name;              /* final exported C symbol */
	char    *params_c;
	char    *ret_c_type;
	bool     is_static;
	uint32_t vtable_slot;
	uint32_t dispatch_token;
} cwinrt_raw_method;

typedef struct cwinrt_raw_db {
	int                arena;
	cwinrt_raw_type   *types;
	uint32_t           type_count;
	cwinrt_raw_method *methods;
	uint32_t           method_count;
	char             **extern_types; /* full WinRT names outside filter ns */
	uint32_t           extern_type_count;
	char              *filter_ns;
} cwinrt_raw_db;

typedef enum
{
	CWINRT_TOPO_IFACE,
	CWINRT_TOPO_CLASS,
	CWINRT_TOPO_STRUCT,
	CWINRT_TOPO_ENUM,
} cwinrt_topo_kind;

typedef struct cwinrt_topo_node {
	uint32_t         id;
	cwinrt_topo_kind kind;
	uint32_t         raw_index;
} cwinrt_topo_node;

typedef struct cwinrt_topo_edge {
	uint32_t from;
	uint32_t to;
	uint8_t  kind; /* 0=implements 1=extends 2=ref */
} cwinrt_topo_edge;

typedef struct cwinrt_topo_graph {
	int               arena;
	cwinrt_topo_node *nodes;
	uint32_t          node_count;
	cwinrt_topo_edge *edges;
	uint32_t          edge_count;
	uint32_t         *order; /* node ids in dependency order */
	uint32_t          order_count;
} cwinrt_topo_graph;

typedef struct cwinrt_mapped_field {
	char *c_type;
	char *name;
} cwinrt_mapped_field;

typedef struct cwinrt_mapped_enum_member {
	char   *name;
	int64_t value;
} cwinrt_mapped_enum_member;

typedef struct cwinrt_mapped_method {
	char    *winrt_name;
	char    *c_name;
	char    *c_sig;
	char    *comment;
	char    *params_c;
	char    *static_class_winrt; /* class full name for static methods */
	char    *delegate_c_name;    /* matching method on statics interface */
	char    *dispatch_iface_c;   /* e.g. WF_IGuidHelperStatics */
	bool     dispatch_has_iid;
	bool     is_static;
	uint32_t vtable_slot;
} cwinrt_mapped_method;

typedef enum
{
	CWINRT_MAP_IFACE,
	CWINRT_MAP_CLASS,
	CWINRT_MAP_STRUCT,
	CWINRT_MAP_ENUM,
} cwinrt_mapped_kind;

typedef struct cwinrt_mapped_type {
	char                      *winrt_name;
	char                      *c_typedef;
	char                      *c_prefix;
	uint8_t                    uuid [16];
	bool                       has_uuid;
	cwinrt_mapped_kind         kind;
	bool                       is_activatable;
	char                      *activate_c_name; /* e.g. wuc_compositor_new */
	uint32_t                   topo_id;
	cwinrt_mapped_field       *fields;
	uint32_t                   field_count;
	cwinrt_mapped_enum_member *enum_members;
	uint32_t                   enum_member_count;
	cwinrt_mapped_method      *methods;
	uint32_t                   method_count;
} cwinrt_mapped_type;

typedef struct cwinrt_mapped_unit {
	int                 arena;
	char               *header_name;
	char               *include_guard;
	char               *ns_prefix;
	char               *filter_ns;
	cwinrt_mapped_type *types;
	uint32_t            type_count;
	char              **includes;
	uint32_t            include_count;
	char              **extern_forwards; /* C typedef names */
	uint32_t            extern_forward_count;
	cwinrt_value_type_set const *value_types; /* global value-type name set (not owned) */
} cwinrt_mapped_unit;

typedef struct cwinrt_emit_opts {
	char const *out_dir;
	char const *header_basename;
	char const *impl_basename;
	bool        emit_impl;
} cwinrt_emit_opts;
