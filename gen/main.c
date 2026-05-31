#include "arena.h"
#include "emit.h"
#include "emit_impl.h"
#include "gen_piids.h"
#include "map.h"
#include "name.h"
#include "parse.h"
#include "slot.h"
#include "topo.h"
#include "winmd.h"
#include "winmd_int.h"
#include "sig.h"

#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <process.h>
  #include <windows.h>
#endif

typedef struct gen_args {
	char const *winmd;
	char const *out_dir;
	char const *impl_dir;
	char const *filter_ns;
	char const *header;
	char const *refs_dir;
	bool        batch_union;
	bool        list_ns;
	bool        batch_refs;
	bool        emit_impl;
	bool        impl_explicit;
	bool        no_impl;
	bool        continue_on_error;
	uint32_t    jobs;
} gen_args;

typedef struct gen_batch_ctx {
	gen_args const                *a;
	winmd_db const                *wm;
	cwinrt_typedef_ns_index const *td_ix;
	cwinrt_value_type_set const   *vts;
	char                         **nss;
	uint32_t                       n;
	volatile long                  next;
	volatile long                  failures;
} gen_batch_ctx;

/* Arena-allocated copy of s (NUL included). */
static char *batch_strdup(int arena, char const *s) {
	size_t n = strlen(s) + 1;
	char  *p = ( char * ) aden(arena, n);
	if (p) memcpy(p, s, n);
	return p;
}

/* qsort comparator ordering enum defs by mangled C name. */
static int cmp_enum_def(void const *a, void const *b) {
	return strcmp((( cwinrt_enum_def const * ) a)->cname, (( cwinrt_enum_def const * ) b)->cname);
}

/* Member values mirror parse_enrich_type: explicit const, else a running counter
   over non-const fields. */
static cwinrt_enum_member_def *
build_enum_members(int arena, winmd_field_info const *wf, uint32_t fn) {
	cwinrt_enum_member_def *mem = ( cwinrt_enum_member_def * ) aden(arena, fn * sizeof(cwinrt_enum_member_def));
	uint32_t                fi, ei = 0;
	if (!mem) return NULL;
	for (fi = 0; fi < fn; fi++) {
		mem [fi].name  = batch_strdup(arena, wf [fi].name ? wf [fi].name : "");
		mem [fi].value = wf [fi].has_const ? wf [fi].const_value : ( int64_t ) (ei++);
	}
	return mem;
}

/* True if typedef row i+1 is a Windows.* enum (extends System.Enum). */
static int is_windows_enum(winmd_db const *wm, uint32_t i) {
	winmd_row_typedef const *td = &wm->typedefs [i];
	char                     base [256];
	uint32_t                 ex;
	if (!td->name || !td->namespace_name || !td->namespace_name [0]) return 0;
	if (strncmp(td->namespace_name, "Windows", 7) != 0) return 0;
	ex = winmd_typedef_extends_coded(wm->meta, i + 1);
	if (winmd_coded_type_full_name(wm->meta, ex, base, sizeof(base)) != 0) return 0;
	return strcmp(base, "System.Enum") == 0;
}

/* Build one enum def for typedef row i+1, or skip. Returns 1 if *def filled, 0 to
   skip, -1 on OOM. */
static int build_enum_def(winmd_db const *wm, int arena, uint32_t i, cwinrt_enum_def *def) {
	winmd_row_typedef const *td = &wm->typedefs [i];
	uint32_t                 fn = 0;
	char                     full [384], cname [192];
	winmd_field_info        *wf = NULL;
	cwinrt_enum_member_def  *mem;

	if (!is_windows_enum(wm, i)) return 0;
	if (winmd_typedef_fields(wm->meta, i + 1, &wf, &fn) != 0 || !fn) {
		if (wf) winmd_field_info_free(wf, fn);
		return 0;
	}
	snprintf(full, sizeof(full), "%s.%s", td->namespace_name, td->name);
	cwinrt_name_winrt_to_c(full, cname, sizeof(cname));
	mem = build_enum_members(arena, wf, fn);
	winmd_field_info_free(wf, fn);
	if (!mem) return -1;
	def->cname        = batch_strdup(arena, cname);
	def->members      = mem;
	def->member_count = fn;
	return def->cname ? 1 : -1;
}

/* Collect every enum (extends System.Enum) in the union winmd, with its members,
   so consumers can inline the full definition (cross-ns enums cannot be opaque
   forward-declared and the include chain is cycle-fragile). */
static int build_value_type_set(winmd_db const *wm, int arena, cwinrt_value_type_set *out) {
	uint32_t         i, n = 0;
	cwinrt_enum_def *defs;
	memset(out, 0, sizeof(*out));
	if (!wm || !wm->typedef_count) return 0;
	defs = ( cwinrt_enum_def * ) aden(arena, wm->typedef_count * sizeof(cwinrt_enum_def));
	if (!defs) return -1;
	for (i = 0; i < wm->typedef_count; i++) {
		int rc = build_enum_def(wm, arena, i, &defs [n]);
		if (rc < 0) return -1;
		if (rc > 0) n++;
	}
	qsort(defs, n, sizeof(cwinrt_enum_def), cmp_enum_def);
	out->enums      = defs;
	out->enum_count = n;
	return 0;
}

/* Derive the .impl.c basename from a header basename ("Foo.h" -> "Foo.impl.c"). */
static void impl_basename_from_header(char const *header, char *impl, size_t impl_sz) {
	size_t n = strlen(header);
	if (n + 8 >= impl_sz || n < 2) {
		snprintf(impl, impl_sz, "unknown.impl.c");
		return;
	}
	memcpy(impl, header, n - 2);
	memcpy(impl + n - 2, ".impl.c", 8);
}

typedef void (*argset_fn)(gen_args *a, char const *val);

/* One setter per CLI flag; bodies record the flag into gen_args. */
static void as_winmd(gen_args *a, char const *v) { a->winmd = v; }
static void as_out(gen_args *a, char const *v) { a->out_dir = v; }
static void as_impl_dir(gen_args *a, char const *v) { a->impl_dir = v; }
static void as_ns(gen_args *a, char const *v) { a->filter_ns = v; }
static void as_header(gen_args *a, char const *v) { a->header = v; }
static void as_jobs(gen_args *a, char const *v) { a->jobs = ( uint32_t ) strtoul(v, NULL, 10); }
static void as_batch_refs(gen_args *a, char const *v) { a->batch_refs = true; a->refs_dir = v; }
static void as_batch_union(gen_args *a, char const *v) { ( void ) v; a->batch_union = true; }
static void as_impl(gen_args *a, char const *v) { ( void ) v; a->emit_impl = true; a->impl_explicit = true; }
static void as_no_impl(gen_args *a, char const *v) { ( void ) v; a->no_impl = true; }
static void as_continue(gen_args *a, char const *v) { ( void ) v; a->continue_on_error = true; }
static void as_list_ns(gen_args *a, char const *v) { ( void ) v; a->list_ns = true; }

static struct argflag {
	char const *name;
	int         takes_val;
	argset_fn   fn;
} const k_argflags [] = {
  {"--winmd", 1, as_winmd},        {"-o", 1, as_out},        {"--impl-dir", 1, as_impl_dir},
  {"--ns", 1, as_ns},              {"--header", 1, as_header}, {"--jobs", 1, as_jobs},
  {"--batch-refs", 1, as_batch_refs}, {"--all-windows", 0, as_batch_union}, {"--batch-union", 0, as_batch_union},
  {"--impl", 0, as_impl},          {"--no-impl", 0, as_no_impl}, {"--continue", 0, as_continue},
  {"--list-ns", 0, as_list_ns},
};

/* Apply flag f at argv[*i], advancing *i past a consumed value. */
static void args_apply_flag(gen_args *a, struct argflag const *f, int argc, char **argv, int *i) {
	if (!f->takes_val) f->fn(a, NULL);
	else if (*i + 1 < argc) f->fn(a, argv [++*i]);
}

/* Match argv[*i] against the flag table and apply it, advancing *i past a value. */
static void args_apply_one(gen_args *a, int argc, char **argv, int *i) {
	size_t k;
	for (k = 0; k < sizeof(k_argflags) / sizeof(k_argflags [0]); k++) {
		struct argflag const *f = &k_argflags [k];
		if (strcmp(argv [*i], f->name) != 0) continue;
		args_apply_flag(a, f, argc, argv, i);
		return;
	}
}

/* --header defaults to the Composition single-namespace name; derive it from --ns. */
static void args_default_header(gen_args *a) {
	static char hdr [128];
	size_t      n;
	if (!a->filter_ns || strcmp(a->header, "Windows.UI.Composition.h") != 0) return;
	n = strlen(a->filter_ns);
	if (n + 2 >= sizeof(hdr)) return;
	memcpy(hdr, a->filter_ns, n);
	memcpy(hdr + n, ".h", 3);
	a->header = hdr;
}

/* Print the usage banner; always returns -1 for the caller to propagate. */
static int args_usage(void) {
	fprintf(stderr,
	        "usage: cwinrt-gen --winmd <path> [-o outdir] [--ns filter] [--header name]\n"
	        "       [--impl-dir dir] [--no-impl] [--impl]\n"
	        "       cwinrt-gen --batch-union --winmd <UnionMetadata\\Windows.winmd> [-o outdir] [--impl]\n"
	        "       cwinrt-gen --list-ns --winmd <UnionMetadata\\Windows.winmd>\n"
	        "       cwinrt-gen --batch-union --winmd <path> [--jobs N]  (N>1: threaded emit)\n");
	return -1;
}

/* Fill *a from argv with defaults applied; returns 0 or -1 (usage shown). */
static int args_parse(int argc, char **argv, gen_args *a) {
	int i;
	memset(a, 0, sizeof(*a));
	a->out_dir   = "include/cwinrt";
	a->impl_dir  = "include/cwinrt/impl";
	a->filter_ns = "Windows.UI.Composition";
	a->header    = "Windows.UI.Composition.h";
	a->emit_impl = true;
	for (i = 1; i < argc; i++) args_apply_one(a, argc, argv, &i);
	if (a->no_impl) a->emit_impl = false;
	else if (a->batch_union && !a->impl_explicit) a->emit_impl = false;
	if (!a->winmd && !a->batch_refs) return args_usage();
	args_default_header(a);
	return 0;
}

/* Source of a single-namespace run: either a preloaded db (wm + td_ix + vts) or a
   winmd_path to open standalone. */
typedef struct gen_input {
	winmd_db const                *wm;
	cwinrt_typedef_ns_index const *td_ix;
	cwinrt_value_type_set const   *vts;
	char const                    *winmd_path;
} gen_input;

/* Write header (+ impl) for an already-mapped unit. */
static int gen_emit_one(gen_args const *a, char const *filter_ns, cwinrt_mapped_unit const *mapped) {
	cwinrt_emit_opts emit;
	char             hdr [128];
	char             impl [128];
	char const      *header;
	int              rc;

	cwinrt_name_header_from_ns(filter_ns, hdr, sizeof(hdr));
	header               = mapped->header_name ? mapped->header_name : hdr;
	emit.out_dir         = a->out_dir;
	emit.header_basename = header;
	emit.emit_impl       = a->emit_impl;
	impl_basename_from_header(header, impl, sizeof(impl));
	emit.impl_basename = impl;
	emit.out_dir       = a->out_dir;
	rc                 = cwinrt_emit_header(mapped, &emit);
	if (rc == 0) printf("generated %s/%s\n", a->out_dir, header);
	if (rc == 0 && a->emit_impl) {
		cwinrt_emit_opts iemit = emit;
		iemit.out_dir          = a->impl_dir;
		if (cwinrt_emit_impl(mapped, &iemit) != 0) rc = -1;
		else printf("generated %s/%s\n", a->impl_dir, impl);
	}
	return rc;
}

static int gen_run_one(gen_args const *a, char const *filter_ns, gen_input const *in) {
	cwinrt_raw_db      raw;
	cwinrt_topo_graph  topo;
	cwinrt_mapped_unit mapped;
	char const        *prefix;
	int                rc;

	memset(&raw, 0, sizeof(raw)), memset(&topo, 0, sizeof(topo)), memset(&mapped, 0, sizeof(mapped));

	/* Parse from a preloaded db (batch) or a standalone path. */
	if (in->wm) rc = cwinrt_parse_winmd_db(in->wm, in->td_ix, filter_ns, &raw);
	else if (in->winmd_path) rc = cwinrt_parse_winmd(in->winmd_path, filter_ns, &raw);
	else rc = -1;
	if (rc != 0) return -1;
	if (!raw.type_count) {
		fprintf(stderr, "no types for namespace %s in %s\n", filter_ns, in->winmd_path);
		cwinrt_raw_db_free(&raw);
		return -1;
	}
	if (cwinrt_topo_build(&raw, &topo) != 0) { cwinrt_raw_db_free(&raw); return -1; }
	if (cwinrt_slot_assign(&raw) != 0) { cwinrt_topo_free(&topo); cwinrt_raw_db_free(&raw); return -1; }
	prefix = cwinrt_name_ns_prefix(filter_ns);
	if (cwinrt_map_unit(&raw, &topo, prefix, in->vts, &mapped) != 0) {
		cwinrt_topo_free(&topo);
		cwinrt_raw_db_free(&raw);
		return -1;
	}
	rc = gen_emit_one(a, filter_ns, &mapped);
	cwinrt_mapped_free(&mapped);
	cwinrt_topo_free(&topo);
	cwinrt_raw_db_free(&raw);
	return rc;
}

/* Print a UUID in winmd byte order as a GUID-struct initializer. */
static void print_uuid(char const *label, uint8_t const u [16]) {
	printf("%s {0x%02X%02X%02X%02X,0x%02X%02X,0x%02X%02X," "{0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X}}\n",
	       label, u [3], u [2], u [1], u [0], u [5], u [4], u [7], u [6], u [8], u [9], u [10], u [11], u [12], u [13],
	       u [14], u [15]);
}

/* Parse and print one method's signature line (and its param names). */
static void dump_method_sig_one(winmd_db const *wm, winmd_row_method const *md, char **pnames, uint32_t pname_n) {
	uint32_t         row = md->token & 0x00ffffffu;
	winmd_method_sig sig;
	char             self [96];
	uint32_t         pi;

	winmd_method_info info  = {md->flags, self, md->name};
	winmd_param_names pn_arg = {( char const *const * ) pnames, pname_n};

	memset(&sig, 0, sizeof(sig));
	self [0] = '\0';
	if (!md->sig_blob || winmd_parse_sig(wm, md->sig_blob, &info, &pn_arg, &sig) != 0) {
		printf("method %s row=%u (sig parse failed)\n", md->name, row);
		return;
	}
	printf(
	  "method %s row=%u flags=0x%x ret=%s params=[%s]\n", md->name, row, md->flags,
	  sig.ret_c_type ? sig.ret_c_type : "", sig.params_c ? sig.params_c : ""
	);
	if (pname_n) {
		printf("  param_names:");
		for (pi = 0; pi < pname_n; pi++) printf(" [%u]=%s", pi, pnames [pi] ? pnames [pi] : "");
		printf("\n");
	}
	winmd_sig_free(&sig);
}

/* Dump signatures of methods whose name contains `needle` (or a single `row_want`). */
static int dump_method_sig(char const *winmd_path, char const *needle, uint32_t row_want) {
	winmd_db wm;
	uint32_t i;
	if (winmd_open(winmd_path, &wm) != 0) return -1;
	for (i = 0; i < wm.method_count; i++) {
		winmd_row_method const *md  = &wm.methods [i];
		uint32_t                row = md->token & 0x00ffffffu;
		char                  **pnames  = NULL;
		uint32_t                pname_n = 0;

		if (!md->name || !needle || !strstr(md->name, needle)) continue;
		if (row_want && row != row_want) continue;
		if (winmd_method_param_names(wm.meta, row, &pnames, &pname_n) != 0) pname_n = 0;
		dump_method_sig_one(&wm, md, pnames, pname_n);
		winmd_param_names_free(pnames, pname_n);
		if (row_want) break;
	}
	winmd_close(&wm);
	return 0;
}

/* Dump the overload name of methods whose name contains `needle`. */
static int dump_method_overloads(char const *winmd_path, char const *needle) {
	winmd_db wm;
	uint32_t i;
	if (winmd_open(winmd_path, &wm) != 0) return -1;
	for (i = 0; i < wm.method_count; i++) {
		winmd_row_method const *md = &wm.methods [i];
		char                    ovl [256];
		uint32_t                row;
		if (!md->name || !needle || !strstr(md->name, needle)) continue;
		row = md->token & 0x00ffffffu;
		if (winmd_method_overload_name(wm.meta, row, ovl, sizeof(ovl)) == 0)
			printf("method %s row=%u overload=%s\n", md->name, row, ovl);
		else printf("method %s row=%u overload=(none)\n", md->name, row);
	}
	winmd_close(&wm);
	return 0;
}

/* Dump TypeDef flags + activatable bit for types matching `needle`. */
static int dump_type_flags(char const *winmd_path, char const *needle) {
	winmd_db wm;
	uint32_t i;
	if (winmd_open(winmd_path, &wm) != 0) return -1;
	for (i = 0; i < wm.typedef_count; i++) {
		char        full [384];
		char const *ns  = wm.typedefs [i].namespace_name;
		char const *nm  = wm.typedefs [i].name;
		uint32_t    row = i + 1;
		if (!nm || !ns) continue;
		snprintf(full, sizeof(full), "%s.%s", ns, nm);
		if (needle && !strstr(full, needle) && !strstr(nm, needle)) continue;
		printf("%s flags=0x%08x activatable_ca=%d\n", full, wm.typedefs [i].flags,
		       winmd_typedef_is_activatable(wm.meta, row) ? 1 : 0);
	}
	winmd_close(&wm);
	return 0;
}

/* Dump the [Guid] of types matching `needle` in GUID-struct form. */
static int dump_guids(char const *winmd_path, char const *needle) {
	winmd_db wm;
	uint32_t i;
	if (winmd_open(winmd_path, &wm) != 0) return -1;
	for (i = 0; i < wm.typedef_count; i++) {
		char        full [384];
		uint8_t     uuid [16];
		char const *ns = wm.typedefs [i].namespace_name;
		char const *nm = wm.typedefs [i].name;
		if (!nm || !ns) continue;
		snprintf(full, sizeof(full), "%s.%s", ns, nm);
		if (needle && !strstr(full, needle) && !strstr(nm, needle)) continue;
		if (winmd_typedef_uuid(wm.meta, i + 1, uuid) == 0) print_uuid(full, uuid);
		else printf("%s (no uuid in CustomAttribute)\n", full);
	}
	winmd_close(&wm);
	return 0;
}

/* Print every Windows.* namespace in the winmd, one per line. */
static int gen_list_namespaces(char const *winmd_path) {
	winmd_db wm;
	char   **nss = NULL;
	uint32_t n   = 0;
	uint32_t i;

	if (winmd_open(winmd_path, &wm) != 0) return -1;
	if (winmd_collect_namespaces(&wm, "Windows", &nss, &n) != 0) { winmd_close(&wm); return -1; }
	winmd_close(&wm);
	for (i = 0; i < n; i++) printf("%s\n", nss [i]);
	winmd_namespaces_free(nss, n);
	return 0;
}

#ifdef _WIN32
/* Emit one namespace for a worker; returns 1 to keep going, 0 to stop the worker. */
static int gen_batch_worker_step(gen_batch_ctx *ctx, uint32_t i) {
	gen_input in = {ctx->wm, ctx->td_ix, ctx->vts, NULL};
	if (gen_run_one(ctx->a, ctx->nss [i], &in) == 0) return 1;
	fprintf(stderr, "failed: %s\n", ctx->nss [i]);
	InterlockedIncrement(&ctx->failures);
	errmsg(NULL);
	return ctx->a->continue_on_error ? 1 : 0;
}

static unsigned __stdcall gen_batch_worker(void *arg) {
	gen_batch_ctx *ctx = ( gen_batch_ctx * ) arg;
	for (;;) {
		long i = InterlockedIncrement(&ctx->next) - 1;
		if (( uint32_t ) i >= ctx->n) break;
		if (!gen_batch_worker_step(ctx, ( uint32_t ) i)) break;
	}
	return 0;
}
#endif

/* Shared resources of a batch run; freed once via batch_res_free. */
typedef struct gen_batch_res {
	winmd_db                wm;
	cwinrt_typedef_ns_index td_ix;
	cwinrt_value_type_set   vts;
	char                  **nss;
	uint32_t                n;
	int                     arena;
} gen_batch_res;

static void batch_res_free(gen_batch_res *r) {
	if (r->arena >= 0) rmarena(r->arena);
	winmd_namespaces_free(r->nss, r->n);
	winmd_close(&r->wm);
}

/* Open the winmd, collect namespaces, and build the shared index/value-type set
   into an arena. Returns 0 with *r owning everything, or -1 (nothing to free). */
static int batch_res_open(gen_args const *a, gen_batch_res *r) {
	memset(r, 0, sizeof(*r));
	r->arena = -1;
	if (winmd_open(a->winmd, &r->wm) != 0) return -1;
	if (winmd_collect_namespaces(&r->wm, "Windows", &r->nss, &r->n) != 0) { winmd_close(&r->wm); return -1; }
	r->arena = mkarena();
	if (r->arena < 0
	    || cwinrt_typedef_ns_index_build(&r->wm, "Windows", r->arena, &r->td_ix) != 0
	    || build_value_type_set(&r->wm, r->arena, &r->vts) != 0) {
		batch_res_free(r);
		return -1;
	}
	return 0;
}

#ifdef _WIN32
/* Spawn `jobs` workers over a shared ctx; return failure count (-1 = spawn failed). */
static int batch_run_threads(gen_batch_ctx *ctx, uint32_t jobs) {
	HANDLE  *threads = ( HANDLE * ) calloc(jobs, sizeof(HANDLE));
	int      failures = 0;
	uint32_t ti;
	if (!threads) return -1;
	for (ti = 0; ti < jobs; ti++) {
		uintptr_t th = _beginthreadex(
		  NULL, 16u * 1024u * 1024u, gen_batch_worker, ctx, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL
		);
		if (!th) {
			failures = -1;
			break;
		}
		threads [ti] = ( HANDLE ) th;
	}
	if (failures == 0) {
		WaitForMultipleObjects(jobs, threads, TRUE, INFINITE);
		failures = ( int ) ctx->failures;
	}
	for (ti = 0; ti < jobs; ti++)
		if (threads [ti]) CloseHandle(threads [ti]);
	free(threads);
	return failures;
}

static int gen_batch_threaded(gen_args const *a, gen_batch_res *r, uint32_t jobs) {
	SYSTEM_INFO   si;
	gen_batch_ctx ctx;
	int           failures, ok;

	GetSystemInfo(&si);
	if (jobs > ( uint32_t ) si.dwNumberOfProcessors * 2) jobs = ( uint32_t ) si.dwNumberOfProcessors * 2;
	if (jobs > r->n) jobs = r->n;
	memset(&ctx, 0, sizeof(ctx));
	ctx.a = a, ctx.wm = &r->wm, ctx.td_ix = &r->td_ix, ctx.vts = &r->vts, ctx.nss = r->nss, ctx.n = r->n;
	printf("batch-union: %u namespaces, winmd loaded once, %u worker threads\n", r->n, jobs);
	failures = batch_run_threads(&ctx, jobs);
	if (failures < 0) return -1;
	if (failures > 0 && !a->continue_on_error) return -1;
	ok = ( int ) r->n - failures;
	printf("batch-union done: %d ok, %d failed\n", ok, failures);
	return failures ? -1 : 0;
}
#endif

/* Sequential emit over every namespace. Returns 0 / -1 like the threaded path. */
static int gen_batch_sequential(gen_args const *a, gen_batch_res *r) {
	gen_input in = {&r->wm, &r->td_ix, &r->vts, NULL};
	uint32_t  i;
	int       failures = 0, ok = 0;
	printf("batch-union: %u namespaces, winmd loaded once (indexed)\n", r->n);
	for (i = 0; i < r->n; i++) {
		if (gen_run_one(a, r->nss [i], &in) == 0) {
			ok++;
			continue;
		}
		fprintf(stderr, "failed: %s\n", r->nss [i]);
		failures++;
		if (!a->continue_on_error) return -1;
	}
	printf("batch-union done: %d ok, %d failed\n", ok, failures);
	return failures ? -1 : 0;
}

static int gen_batch_union(gen_args const *a) {
	gen_batch_res r;
	uint32_t      jobs = a->jobs;
	int           rc;

	if (batch_res_open(a, &r) != 0) return -1;
	if (!jobs) jobs = 1;
#ifdef _WIN32
	if (jobs > 1) {
		rc = gen_batch_threaded(a, &r, jobs);
		batch_res_free(&r);
		return rc;
	}
#endif
	if (jobs > 1) fprintf(stderr, "warning: --jobs ignored (not on Windows); using sequential batch\n");
	rc = gen_batch_sequential(a, &r);
	batch_res_free(&r);
	return rc;
}

/* Parse each Windows namespace once (indexed) and invoke `visit(raw, ctx)`. */
typedef void (*ns_raw_fn)(cwinrt_raw_db const *raw, void *ctx);
static int foreach_ns_rawdb(char const *winmd_path, ns_raw_fn visit, void *ctx) {
	winmd_db                wm;
	cwinrt_typedef_ns_index td_ix;
	char                  **nss = NULL;
	uint32_t                n = 0, i;
	int                     arena;

	if (winmd_open(winmd_path, &wm) != 0) return -1;
	if (winmd_collect_namespaces(&wm, "Windows", &nss, &n) != 0) { winmd_close(&wm); return -1; }
	arena = mkarena();
	memset(&td_ix, 0, sizeof(td_ix));
	cwinrt_typedef_ns_index_build(&wm, "Windows", arena, &td_ix);
	for (i = 0; i < n; i++) {
		cwinrt_raw_db raw;
		memset(&raw, 0, sizeof(raw));
		if (cwinrt_parse_winmd_db(&wm, &td_ix, nss [i], &raw) != 0) continue;
		visit(&raw, ctx);
		cwinrt_raw_db_free(&raw);
	}
	if (arena >= 0) rmarena(arena);
	winmd_namespaces_free(nss, n);
	winmd_close(&wm);
	return ( int ) n;
}

typedef struct slot_tally { uint32_t ok, bad; } slot_tally;
static void check_slots_ns(cwinrt_raw_db const *raw, void *ctx) {
	slot_tally *t = ( slot_tally * ) ctx;
	cwinrt_slot_assign(( cwinrt_raw_db * ) raw);
	cwinrt_slot_check(raw, &t->ok, &t->bad);
}

/* Validate vtable slot assignment across every namespace's interfaces against the
   WinRT ABI rule (see cwinrt_slot_check). */
static int gen_check_slots(char const *winmd_path) {
	slot_tally t = {0, 0};
	int        n = foreach_ns_rawdb(winmd_path, check_slots_ns, &t);
	if (n < 0) return -1;
	printf("slot check: %u ok, %u bad (across %u namespaces)\n", t.ok, t.bad, ( uint32_t ) n);
	return t.bad ? -1 : 0;
}

/* Print "<Full.WinRT.Name> <CANONICAL-GUID>" for every interface with a [Guid]. */
static void dump_iids_ns(cwinrt_raw_db const *raw, void *ctx) {
	uint32_t ti;
	( void ) ctx;
	for (ti = 0; ti < raw->type_count; ti++) {
		cwinrt_raw_type const *t = &raw->types [ti];
		uint8_t const         *g = t->uuid;
		if (t->kind != CWINRT_RAW_IFACE || !t->has_uuid || !t->full_name) continue;
		printf("%s %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X\n", t->full_name, g [3], g [2],
		       g [1], g [0], g [5], g [4], g [7], g [6], g [8], g [9], g [10], g [11], g [12], g [13], g [14], g [15]);
	}
}

/* Differential check: emit a canonical GUID per [Guid] interface so a checker can
   diff against cppwinrt's guid_v constants. */
static int gen_dump_iids(char const *winmd_path) {
	return foreach_ns_rawdb(winmd_path, dump_iids_ns, NULL) < 0 ? -1 : 0;
}


/* A one-shot subcommand: argv[i] is the matched flag; a1/a2 are the following
   args (NULL if absent). Returns the process exit code. */
typedef int (*subcmd_fn)(char const *a1, char const *a2, char const *a3);

/* Thin adapters mapping subcommand args to a diagnostic; result is an exit code. */
static int cmd_dump_guid(char const *a, char const *b, char const *c) { ( void ) c; return dump_guids(a, b) != 0; }
static int cmd_dump_type(char const *a, char const *b, char const *c) { ( void ) c; return dump_type_flags(a, b) != 0; }
static int cmd_dump_overload(char const *a, char const *b, char const *c) { ( void ) c; return dump_method_overloads(a, b) != 0; }
static int cmd_dump_method(char const *a, char const *b, char const *c) {
	return dump_method_sig(a, b, c ? ( uint32_t ) strtoul(c, NULL, 10) : 0) != 0;
}
static int cmd_check_slots(char const *a, char const *b, char const *c) { ( void ) b; ( void ) c; return gen_check_slots(a) != 0; }
static int cmd_dump_iids(char const *a, char const *b, char const *c) { ( void ) b; ( void ) c; return gen_dump_iids(a) != 0; }
static int cmd_selftest2(char const *a, char const *b, char const *c) { ( void ) b; ( void ) c; return gen_selftest_piid2(a) != 0; }
static int cmd_dump_piids(char const *a, char const *b, char const *c) { ( void ) b; ( void ) c; return gen_dump_piids(a) != 0; }
static int cmd_emit_piids(char const *a, char const *b, char const *c) { ( void ) c; return gen_emit_piids(a, b ? b : "include/cwinrt") != 0; }
static int cmd_dump_sig(char const *a, char const *b, char const *c) { ( void ) c; return gen_dump_sig(a, b) != 0; }
static int cmd_selftest_rfc(char const *a, char const *b, char const *c) { ( void ) a; ( void ) b; ( void ) c; return gen_selftest_piid_rfc(); }

/* Diagnostic subcommands, dispatched before normal generation. `need` is the
   count of required following arguments. */
static struct subcmd {
	char const *name;
	int         need;
	subcmd_fn   fn;
} const k_subcmds [] = {
  {"--dump-guid", 2, cmd_dump_guid},     {"--dump-type", 2, cmd_dump_type},
  {"--dump-overload", 2, cmd_dump_overload}, {"--dump-method", 2, cmd_dump_method},
  {"--check-slots", 1, cmd_check_slots}, {"--dump-iids", 1, cmd_dump_iids},
  {"--selftest-piid2", 1, cmd_selftest2}, {"--dump-piids", 1, cmd_dump_piids},
  {"--emit-piids", 1, cmd_emit_piids},   {"--dump-sig", 2, cmd_dump_sig},
  {"--selftest-piid", 0, cmd_selftest_rfc},
};

/* The subcmd whose name == argv[i] and whose required args fit, else NULL. */
static struct subcmd const *subcmd_match(char const *arg, int avail) {
	size_t k;
	for (k = 0; k < sizeof(k_subcmds) / sizeof(k_subcmds [0]); k++) {
		struct subcmd const *s = &k_subcmds [k];
		if (strcmp(arg, s->name) == 0 && s->need < avail) return s;
	}
	return NULL;
}

/* Run a matching diagnostic subcommand. Returns its exit code, or -1 if argv
   contains no subcommand (caller proceeds to normal generation). */
static int run_subcommand(int argc, char **argv) {
	int i;
	for (i = 1; i < argc; i++) {
		struct subcmd const *s = subcmd_match(argv [i], argc - i);
		if (!s) continue;
		return s->fn(s->need > 0 ? argv [i + 1] : NULL, s->need > 1 ? argv [i + 2] : NULL,
		             s->need > 2 ? argv [i + 3] : NULL);
	}
	return -1;
}

static int run_generation(gen_args const *a) {
	if (a->list_ns) return a->winmd ? gen_list_namespaces(a->winmd) != 0 : (fprintf(stderr, "--list-ns requires --winmd\n"), 1);
	if (a->batch_refs) return fprintf(stderr, "use scripts/gen_all_windows.ps1 for --batch-refs\n"), 1;
	if (a->batch_union) {
		if (!a->winmd) return fprintf(stderr, "batch-union requires --winmd\n"), 1;
		if (gen_batch_union(a) != 0) return efail(), 1;
		return 0;
	}
	{
		gen_input in = {NULL, NULL, NULL, a->winmd};
		if (gen_run_one(a, a->filter_ns, &in) != 0) return efail(), 1;
	}
	return 0;
}

int main(int argc, char **argv) {
	gen_args a;
	int      sub = run_subcommand(argc, argv);
	if (sub >= 0) return sub;
	if (args_parse(argc, argv, &a) != 0) return 1;
	return run_generation(&a);
}
