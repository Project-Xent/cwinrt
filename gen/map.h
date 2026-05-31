#pragma once

#include "model.h"

int cwinrt_map_unit(
  cwinrt_raw_db const *raw, cwinrt_topo_graph const *topo, char const *ns_prefix,
  cwinrt_value_type_set const *vts, cwinrt_mapped_unit *out
);

void cwinrt_mapped_free(cwinrt_mapped_unit *u);

/* Normalize params_c (open generics, composition abbrev fixups). */
void cwinrt_fixup_params_c(char *params);
