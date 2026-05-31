#pragma once

#include <stdint.h>

typedef struct winmd_db   winmd_db;
typedef struct winmd_meta winmd_meta;

/* Parsed ECMA-335 method signature as C fragments (heap-allocated). */
typedef struct winmd_method_sig {
	char *ret_c_type;
	char *params_c;
} winmd_method_sig;

/* Parse a standalone type signature blob (e.g. TypeSpec). */
int winmd_parse_type_blob(winmd_meta const *m, uint32_t blob_ix, char *buf, size_t bufsz);

/* Parse a FieldSig blob (skips the leading 0x06 FIELD calling-convention byte). */
int winmd_parse_field_blob(winmd_meta const *m, uint32_t blob_ix, char *buf, size_t bufsz);

/* MethodDef identity inputs. self_c_type may be NULL for static methods. */
typedef struct winmd_method_info {
	uint32_t    method_flags;
	char const *self_c_type;
	char const *method_name;
} winmd_method_info;

/* Param-table names backing a method signature. */
typedef struct winmd_param_names {
	char const *const *names;
	uint32_t           count;
} winmd_param_names;

/* Parse MethodDef signature blob. */
int winmd_parse_sig(
  winmd_db const *db, uint32_t blob_ix, winmd_method_info const *info, winmd_param_names const *pnames,
  winmd_method_sig *out
);

void winmd_sig_free(winmd_method_sig *sig);

/* Append coded type tokens (CLASS/VALUETYPE/GENERICINST) from a method signature blob. */
int  winmd_sig_collect_type_tokens(
  winmd_meta const *m, uint32_t blob_ix, uint32_t *out, uint32_t *inout_count, uint32_t max
);

/* Append coded type tokens from a FieldSig blob (skips the leading 0x06 byte). */
int  winmd_field_collect_type_tokens(
  winmd_meta const *m, uint32_t blob_ix, uint32_t *out, uint32_t *inout_count, uint32_t max
);
