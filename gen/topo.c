#include "topo.h"

#include "arena.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

static const struct {
	cwinrt_raw_kind  raw;
	cwinrt_topo_kind topo;
} k_kind_map [] = {
	{ CWINRT_RAW_IFACE,    CWINRT_TOPO_IFACE  },
	{ CWINRT_RAW_CLASS,    CWINRT_TOPO_CLASS  },
	{ CWINRT_RAW_STRUCT,   CWINRT_TOPO_STRUCT },
	{ CWINRT_RAW_ENUM,     CWINRT_TOPO_ENUM   },
	{ CWINRT_RAW_DELEGATE, CWINRT_TOPO_IFACE  },
};

static cwinrt_topo_kind topo_kind_from_raw(cwinrt_raw_kind k) {
	uint32_t i;
	for (i = 0; i < sizeof(k_kind_map) / sizeof(k_kind_map [0]); i++)
		if (k_kind_map [i].raw == k) return k_kind_map [i].topo;
	return CWINRT_TOPO_IFACE;
}

static int topo_find_raw(cwinrt_raw_db const *raw, uint32_t token) {
	uint32_t i;
	for (i = 0; i < raw->type_count; i++)
		if (raw->types [i].token == token) return ( int ) i;
	return -1;
}

static int topo_add_edge(int arena, cwinrt_topo_graph *g, uint32_t from, uint32_t to, uint8_t kind) {
	cwinrt_topo_edge *elist;
	if (from == to) return 0;
	elist = ( cwinrt_topo_edge * ) aden(arena, (g->edge_count + 1) * sizeof(cwinrt_topo_edge));
	if (!elist) return -1;
	if (g->edge_count) memcpy(elist, g->edges, g->edge_count * sizeof(cwinrt_topo_edge));
	elist [g->edge_count].from = from;
	elist [g->edge_count].to   = to;
	elist [g->edge_count].kind = kind;
	g->edges                   = elist;
	g->edge_count++;
	return 0;
}

/* Resolve a raw token and, if it maps to a node distinct from src, add the edge. */
static int topo_edge_to_token(cwinrt_raw_db const *raw, cwinrt_topo_graph *g, uint32_t src, uint32_t token, uint8_t kind) {
	int dst = topo_find_raw(raw, token);
	if (dst < 0 || dst == ( int ) src) return 0;
	return topo_add_edge(g->arena, g, src, ( uint32_t ) dst, kind);
}

static int topo_edges_inherit(cwinrt_raw_db const *raw, cwinrt_topo_graph *g, uint32_t i, cwinrt_raw_type const *t) {
	uint32_t j;
	if (t->extends_token && topo_edge_to_token(raw, g, i, t->extends_token, 1) != 0) return -1;
	for (j = 0; j < t->iface_count; j++)
		if (topo_edge_to_token(raw, g, i, t->iface_tokens [j], 0) != 0) return -1;
	for (j = 0; j < t->ref_count; j++)
		if (topo_edge_to_token(raw, g, i, t->ref_tokens [j], 2) != 0) return -1;
	return 0;
}

/* A struct field used by value needs its type defined first. Edge
   (field_type -> container) so the field's struct/enum is emitted earlier. */
static int topo_edges_fields(cwinrt_raw_db const *raw, cwinrt_topo_graph *g, uint32_t i, cwinrt_raw_type const *t) {
	uint32_t j;
	if (t->kind != CWINRT_RAW_STRUCT) return 0;
	for (j = 0; j < t->field_count; j++) {
		uint32_t ft = t->fields [j].type_token;
		int      dep;
		if (!ft || (ft & 3u) != 0) continue; /* primitive or non-TypeDef (cross-ns) */
		dep = topo_find_raw(raw, 0x02000000u | (ft >> 2)); /* TypeDef token */
		if (dep < 0 || dep == ( int ) i) continue;
		if (topo_add_edge(g->arena, g, ( uint32_t ) dep, i, 3) != 0) return -1;
	}
	return 0;
}

static int topo_build_edges(cwinrt_raw_db const *raw, cwinrt_topo_graph *g) {
	uint32_t i;
	for (i = 0; i < raw->type_count; i++) {
		cwinrt_raw_type const *t = &raw->types [i];
		if (topo_edges_inherit(raw, g, i, t) != 0) return -1;
		if (topo_edges_fields(raw, g, i, t) != 0) return -1;
	}
	return 0;
}

/* Drop the indegree of n's successors; enqueue any that reach zero.
   Returns the queue tail advanced past newly ready nodes. */
static uint32_t topo_relax(cwinrt_topo_graph *g, uint32_t n, uint32_t *indeg, uint32_t *queue, uint32_t qtail) {
	uint32_t e;
	for (e = 0; e < g->edge_count; e++) {
		if (g->edges [e].from != n) continue;
		if (--indeg [g->edges [e].to] == 0) queue [qtail++] = g->edges [e].to;
	}
	return qtail;
}

/* Kahn's algorithm. Returns the number of nodes placed into g->order. */
static uint32_t topo_kahn(cwinrt_topo_graph *g, uint32_t *indeg, uint32_t *queue) {
	uint32_t qhead  = 0;
	uint32_t qtail  = 0;
	uint32_t placed = 0;
	uint32_t i;
	uint32_t e;

	for (e = 0; e < g->edge_count; e++) indeg [g->edges [e].to]++;
	for (i = 0; i < g->node_count; i++)
		if (indeg [i] == 0) queue [qtail++] = i;
	while (qhead < qtail) {
		uint32_t n                  = queue [qhead++];
		g->order [g->order_count++] = n;
		placed++;
		qtail = topo_relax(g, n, indeg, queue, qtail);
	}
	return placed;
}

static int topo_sort_graph(cwinrt_topo_graph *g) {
	uint32_t *indeg;
	uint32_t *queue;
	uint32_t  placed;
	uint32_t  i;

	if (!g->node_count) return 0;
	indeg    = ( uint32_t * ) calloc(g->node_count, sizeof(uint32_t));
	queue    = ( uint32_t * ) calloc(g->node_count, sizeof(uint32_t));
	g->order = ( uint32_t * ) aden(g->arena, g->node_count * sizeof(uint32_t));
	if (!indeg || !queue || !g->order) {
		free(indeg);
		free(queue);
		return -1;
	}
	placed = topo_kahn(g, indeg, queue);
	free(indeg);
	free(queue);
	/* On a cycle Kahn cannot place every node; fall back to raw order so the
	   generator still emits all types deterministically. */
	if (placed != g->node_count) {
		for (i = 0; i < g->node_count; i++) g->order [i] = i;
		g->order_count = g->node_count;
	}
	return 0;
}

static int topo_alloc_nodes(cwinrt_raw_db const *raw, cwinrt_topo_graph *out) {
	uint32_t i;
	out->nodes = ( cwinrt_topo_node * ) aden(out->arena, out->node_count * sizeof(cwinrt_topo_node));
	if (!out->nodes) {
		errmsg("topo nodes OOM");
		return -1;
	}
	for (i = 0; i < raw->type_count; i++) {
		out->nodes [i].id        = i;
		out->nodes [i].raw_index = i;
		out->nodes [i].kind      = topo_kind_from_raw(raw->types [i].kind);
	}
	return 0;
}

int cwinrt_topo_build(cwinrt_raw_db const *raw, cwinrt_topo_graph *out) {
	if (!raw || !out) return -1;
	memset(out, 0, sizeof(*out));
	out->arena = mkarena();
	if (out->arena < 0) {
		errmsg("mkarena failed");
		return -1;
	}
	out->node_count = raw->type_count;
	if (!out->node_count) return 0;
	if (topo_alloc_nodes(raw, out) != 0) return -1;
	if (topo_build_edges(raw, out) != 0) return -1;
	if (topo_sort_graph(out) != 0) return -1;
	return 0;
}

void cwinrt_topo_free(cwinrt_topo_graph *g) {
	if (!g) return;
	if (g->arena > 0) rmarena(g->arena);
	memset(g, 0, sizeof(*g));
}
