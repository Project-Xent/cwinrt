#pragma once

#include "model.h"

int  cwinrt_topo_build(cwinrt_raw_db const *raw, cwinrt_topo_graph *out);

void cwinrt_topo_free(cwinrt_topo_graph *g);
