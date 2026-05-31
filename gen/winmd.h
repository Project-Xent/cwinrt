#pragma once

#include <stddef.h>
#include <stdint.h>

/* Pure C ECMA-335 reader for .winmd (PE + #~ metadata). */

typedef struct winmd_row_typedef {
	uint32_t flags;
	uint32_t token;
	char    *name;
	char    *namespace_name;
} winmd_row_typedef;

typedef struct winmd_row_method {
	uint32_t flags;
	uint32_t token;
	uint32_t typedef_token;
	uint32_t sig_blob;
	char    *name;
	char    *ret_c_type; /* C return value type, e.g. "void", "float" */
	char    *params_c;   /* C parameter list (includes self when instance) */
} winmd_row_method;

struct winmd_meta;

typedef struct winmd_db {
	uint8_t           *file_data;
	size_t             file_size;
	winmd_row_typedef *typedefs;
	uint32_t           typedef_count;
	winmd_row_method  *methods;
	uint32_t           method_count;
	struct winmd_meta *meta;
} winmd_db;

/* Load and parse a .winmd file. Returns 0 on success. */
int  winmd_open(char const *path, winmd_db *out);

void winmd_close(winmd_db *db);
