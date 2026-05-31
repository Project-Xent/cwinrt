#pragma once

#include "model.h"
#include "winmd.h"

/* Sorted typedef row index for Windows.* namespaces (built once per winmd). */
typedef struct cwinrt_typedef_ns_index {
	char    **names;
	uint32_t *offsets;
	uint32_t *td_indices;
	uint32_t  ns_count;
	uint32_t  td_total;
	int       arena;
} cwinrt_typedef_ns_index;

/* Parse .winmd (ECMA-335, pure C) into cwinrt_raw_db. filter_ns may be NULL. */
int cwinrt_parse_winmd(char const *path, char const *filter_ns, cwinrt_raw_db *out);

/* Parse one namespace from an already-open winmd (no disk reload). td_ix may be NULL. */
int cwinrt_parse_winmd_db(
  winmd_db const *wm, cwinrt_typedef_ns_index const *td_ix, char const *filter_ns, cwinrt_raw_db *out
);

int cwinrt_typedef_ns_index_build(winmd_db const *wm, char const *prefix, int arena, cwinrt_typedef_ns_index *out);

int cwinrt_typedef_ns_index_range(
  cwinrt_typedef_ns_index const *td_ix, char const *filter_ns, uint32_t *begin, uint32_t *end
);

void cwinrt_typedef_ns_index_free(cwinrt_typedef_ns_index *td_ix);

void cwinrt_raw_db_free(cwinrt_raw_db *db);
