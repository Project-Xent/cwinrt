#include "gen_piids.h"

#include "arena.h"
#include "bio.h"
#include "name.h"
#include "piid.h"
#include "sig.h"
#include "sigbuild.h"
#include "winmd.h"
#include "winmd_int.h"

#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Print a computed PIID in canonical (cppwinrt-comment) form. */
static void print_piid(char const *name, uint8_t const *g) {
	printf("%s %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X\n", name, g [3], g [2], g [1], g [0],
	       g [5], g [4], g [7], g [6], g [8], g [9], g [10], g [11], g [12], g [13], g [14], g [15]);
}

/* Resolve an open generic interface/delegate by WinRT name and return its
   [Guid] in winmd layout. Returns 0 on success. */
static int open_generic_guid(winmd_db const *wm, char const *full_name, uint8_t out [16]) {
	uint32_t row = winmd_typedef_find_by_name(wm, full_name);
	if (!row) {
		fprintf(stderr, "piid2: type not found: %s\n", full_name);
		return -1;
	}
	return winmd_typedef_uuid(wm->meta, row, out);
}

/* Compute PIIDs for a curated set of generic instantiations and print them so they
   can be diffed against the authoritative cppwinrt guid_v values
   (tests/piid/cppwinrt_piids.cpp). */
#define PI1(disp, type, a0)                                                                                            \
	do {                                                                                                              \
		char const *args [] = {a0};                                                                                 \
		if (open_generic_guid(wm, type, og) == 0 && cwinrt_piid_compute(og, args, 1, piid) == 0)                    \
			print_piid(disp, piid);                                                                                 \
	} while (0)
#define PI2(disp, type, a0, a1)                                                                                        \
	do {                                                                                                              \
		char const *args [] = {a0, a1};                                                                             \
		if (open_generic_guid(wm, type, og) == 0 && cwinrt_piid_compute(og, args, 2, piid) == 0)                    \
			print_piid(disp, piid);                                                                                 \
	} while (0)

/* Basic instantiations whose args are primitive/string/object basic-sig strings. */
static void selftest_piid2_basic(winmd_db *wm) {
	uint8_t og [16], piid [16];
	PI1("Windows.Foundation.Collections.IIterable`1<String>", "Windows.Foundation.Collections.IIterable`1", "string");
	PI1("Windows.Foundation.Collections.IIterable`1<Object>", "Windows.Foundation.Collections.IIterable`1",
	    "cinterface(IInspectable)");
	PI1("Windows.Foundation.Collections.IVector`1<String>", "Windows.Foundation.Collections.IVector`1", "string");
	PI1("Windows.Foundation.Collections.IVectorView`1<String>", "Windows.Foundation.Collections.IVectorView`1",
	    "string");
	PI2("Windows.Foundation.Collections.IMap`2<String,String>", "Windows.Foundation.Collections.IMap`2", "string",
	    "string");
	PI2("Windows.Foundation.Collections.IMapView`2<String,String>", "Windows.Foundation.Collections.IMapView`2",
	    "string", "string");
	PI2("Windows.Foundation.Collections.IKeyValuePair`2<String,Object>",
	    "Windows.Foundation.Collections.IKeyValuePair`2", "string", "cinterface(IInspectable)");
	PI1("Windows.Foundation.IReference`1<Int32>", "Windows.Foundation.IReference`1", "i4");
	PI1("Windows.Foundation.IReference`1<Boolean>", "Windows.Foundation.IReference`1", "b1");
	PI1("Windows.Foundation.IAsyncOperation`1<Boolean>", "Windows.Foundation.IAsyncOperation`1", "b1");
	PI1("Windows.Foundation.AsyncOperationCompletedHandler`1<Boolean>",
	    "Windows.Foundation.AsyncOperationCompletedHandler`1", "b1");
	PI1("Windows.Foundation.EventHandler`1<Object>", "Windows.Foundation.EventHandler`1",
	    "cinterface(IInspectable)");
	PI2("Windows.Foundation.TypedEventHandler`2<Object,Object>", "Windows.Foundation.TypedEventHandler`2",
	    "cinterface(IInspectable)", "cinterface(IInspectable)");
}

/* sigbuild-driven: the arg signature is built recursively from metadata
   (struct/enum/runtimeclass), exercising cwinrt_sigbuild_typedef. */
#define PIT(disp, gen, argtype)                                                                                       \
	do {                                                                                                            \
		char     asig [2048];                                                                                       \
		uint32_t arow = winmd_typedef_find_by_name(wm, argtype);                                                   \
		if (arow && cwinrt_sigbuild_typedef(wm, arow, asig, sizeof(asig)) == 0) {                                   \
			char const *aa [] = {asig};                                                                            \
			if (open_generic_guid(wm, gen, og) == 0 && cwinrt_piid_compute(og, aa, 1, piid) == 0)                  \
				print_piid(disp, piid);                                                                            \
		}                                                                                                           \
		else fprintf(stderr, "piid2: sigbuild failed for %s\n", argtype);                                          \
	} while (0)

static void selftest_piid2_sigbuild(winmd_db *wm) {
	uint8_t og [16], piid [16];
	PIT("Windows.Foundation.IReference`1<DateTime>", "Windows.Foundation.IReference`1", "Windows.Foundation.DateTime");
	PIT("Windows.Foundation.IReference`1<TimeSpan>", "Windows.Foundation.IReference`1", "Windows.Foundation.TimeSpan");
	PIT("Windows.Foundation.IReference`1<Point>", "Windows.Foundation.IReference`1", "Windows.Foundation.Point");
	PIT("Windows.Foundation.IReference`1<Rect>", "Windows.Foundation.IReference`1", "Windows.Foundation.Rect");
	PIT("Windows.Foundation.IReference`1<Color>", "Windows.Foundation.IReference`1", "Windows.UI.Color");
	PIT("Windows.Foundation.IReference`1<DayOfWeek>", "Windows.Foundation.IReference`1",
	    "Windows.Globalization.DayOfWeek");
	PIT("Windows.Foundation.Collections.IIterable`1<StorageFile>", "Windows.Foundation.Collections.IIterable`1",
	    "Windows.Storage.StorageFile");
	PIT("Windows.Foundation.IAsyncOperation`1<StorageFile>", "Windows.Foundation.IAsyncOperation`1",
	    "Windows.Storage.StorageFile");
}

#undef PIT
#undef PI1
#undef PI2

/* IVector`1<IKeyValuePair`2<String,Object>> — the inner arg is the inner type's
   full pinterface(...) signature string, not a hashed guid. */
static void selftest_piid2_nested_vector(winmd_db *wm) {
	uint8_t     og [16], inner_og [16], piid [16];
	char const *kvp_args [] = {"string", "cinterface(IInspectable)"};
	char        inner_sig [512];
	if (open_generic_guid(wm, "Windows.Foundation.Collections.IKeyValuePair`2", inner_og) == 0
	    && cwinrt_sig_pinterface(inner_og, kvp_args, 2, inner_sig, sizeof(inner_sig)) > 0
	    && open_generic_guid(wm, "Windows.Foundation.Collections.IVector`1", og) == 0) {
		char const *vargs [] = {inner_sig};
		if (cwinrt_piid_compute(og, vargs, 1, piid) == 0)
			print_piid("Windows.Foundation.Collections.IVector`1<IKeyValuePair<String,Object>>", piid);
	}
}

/* IReference`1<Guid> — System.Guid is not a Windows TypeDef, so its arg sig is the
   basic "g16"; verify against cppwinrt directly. */
static void selftest_piid2_guid(winmd_db *wm) {
	uint8_t     og [16], piid [16];
	char const *gg [] = {"g16"};
	if (open_generic_guid(wm, "Windows.Foundation.IReference`1", og) == 0 && cwinrt_piid_compute(og, gg, 1, piid) == 0)
		print_piid("Windows.Foundation.IReference`1<Guid>", piid);
}

/* Nested generic-interface arg: IMapView`2<String, IVectorView`1<String>>. */
static void selftest_piid2_nested_mapview(winmd_db *wm) {
	uint8_t     og [16], inner_og [16], piid [16];
	char const *vv_args [] = {"string"};
	char        vv_sig [512];
	if (open_generic_guid(wm, "Windows.Foundation.Collections.IVectorView`1", inner_og) == 0
	    && cwinrt_sig_pinterface(inner_og, vv_args, 1, vv_sig, sizeof(vv_sig)) > 0
	    && open_generic_guid(wm, "Windows.Foundation.Collections.IMapView`2", og) == 0) {
		char const *margs [] = {"string", vv_sig};
		if (cwinrt_piid_compute(og, margs, 2, piid) == 0)
			print_piid("Windows.Foundation.Collections.IMapView`2<String,IVectorView<String>>", piid);
	}
}

int gen_selftest_piid2(char const *winmd_path) {
	winmd_db wm;
	if (winmd_open(winmd_path, &wm) != 0) return -1;
	selftest_piid2_basic(&wm);
	selftest_piid2_nested_vector(&wm);
	selftest_piid2_sigbuild(&wm);
	selftest_piid2_guid(&wm);
	selftest_piid2_nested_mapview(&wm);
	winmd_close(&wm);
	return 0;
}

static int piid_name_is_ident(char const *s) {
	size_t i;
	if (!s || !s [0]) return 0;
	for (i = 0; s [i]; i++)
		if (!((s [i] >= 'A' && s [i] <= 'Z') || (s [i] >= 'a' && s [i] <= 'z') || (s [i] >= '0' && s [i] <= '9')
		      || s [i] == '_'))
			return 0;
	return 1;
}

/* Growable parallel arrays of mangled C names and their 16-byte PIIDs. */
typedef struct piid_set {
	char   **names;
	uint8_t *piids;
	uint32_t n;
	uint32_t cap;
} piid_set;

static int piid_set_has(piid_set const *s, char const *cname) {
	uint32_t j;
	for (j = 0; j < s->n; j++)
		if (strcmp(s->names [j], cname) == 0) return 1;
	return 0;
}

/* Append (cname, piid); returns 0, or -1 on allocation failure. */
static int piid_set_add(piid_set *s, char const *cname, uint8_t const piid [16]) {
	if (s->n >= s->cap) {
		uint32_t nc = s->cap ? s->cap * 2 : 512;
		char   **nn = ( char ** ) realloc(s->names, nc * sizeof(char *));
		uint8_t *np = ( uint8_t * ) realloc(s->piids, nc * 16);
		if (!nn || !np) return -1;
		s->names = nn;
		s->piids = np;
		s->cap   = nc;
	}
	s->names [s->n] = ( char * ) malloc(strlen(cname) + 1);
	if (!s->names [s->n]) return -1;
	memcpy(s->names [s->n], cname, strlen(cname) + 1);
	memcpy(s->piids + ( size_t ) s->n * 16, piid, 16);
	s->n++;
	return 0;
}

static void piid_set_free(piid_set *s) {
	uint32_t i;
	for (i = 0; i < s->n; i++) free(s->names [i]);
	free(s->names);
	free(s->piids);
}

/* Collect a deduped PIID per QI-able generic instantiation in the TypeSpec table.
   Names use the SAME mangling the projection uses for opaque forward-decls (via
   winmd_parse_type_blob), so consumers can QI with &CWINRT_IID_<mangled>. */
static void piid_set_collect(winmd_db *wm, piid_set *s) {
	uint32_t ts_n = wm->meta->tabs.rows [WINMD_TBL_TYPESPEC];
	uint32_t i;
	for (i = 1; i <= ts_n; i++) {
		char     cname [1024];
		char     sig [4096];
		uint8_t  piid [16];
		uint32_t blob_ix = winmd_typespec_sig_blob(wm->meta, i);
		size_t   ln;

		if (!blob_ix || winmd_parse_type_blob(wm->meta, blob_ix, cname, sizeof(cname)) != 0) continue;
		ln = strlen(cname);
		while (ln && cname [ln - 1] == '*') cname [--ln] = '\0';
		if (!piid_name_is_ident(cname)) continue; /* arrays/byref/open-generic: not a QI-able instantiation */
		if (cwinrt_sigbuild_typespec(wm, i, sig, sizeof(sig), NULL, 0) != 0) continue;
		cwinrt_piid_from_sig(sig, strlen(sig), piid);
		if (piid_set_has(s, cname)) continue;
		if (piid_set_add(s, cname, piid) != 0) break;
	}
}

/* Write the PIID header + definition unit. Returns 0, or -1 if a file can't open. */
static int piid_set_write(piid_set const *s, char const *out_dir) {
	char     hpath [1024];
	char     cpath [1024];
	omode    mod = {.w = true, .t = true};
	int      arena;
	int      bh, bc;
	uint32_t i;

	snprintf(hpath, sizeof(hpath), "%s/cwinrt_piids.h", out_dir);
	snprintf(cpath, sizeof(cpath), "%s/impl/cwinrt_piids.impl.c", out_dir);
	arena = mkarena();
	if (arena < 0) {
		errmsg("emit-piids mkarena");
		return -1;
	}
	bh = bopen(arena, hpath, mod);
	bc = bopen(arena, cpath, mod);
	if (bh < 0 || bc < 0) {
		if (bh >= 0) rmbio(bh);
		if (bc >= 0) rmbio(bc);
		rmarena(arena);
		errmsg("emit-piids: cannot open output");
		return -1;
	}
	bprint(bh, "/* Generated by cwinrt-gen; do not edit. Parameterized interface IDs\n"
	           "   (PIIDs) for WinRT generic instantiations. QI with &CWINRT_IID_<name>. */\n"
	           "#ifndef CWINRT_PIIDS_H\n#define CWINRT_PIIDS_H\n\n"
	           "#include <windows.h>\n\n"
	           "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
	bprint(bc, "/* Generated by cwinrt-gen; do not edit. PIID definitions. */\n"
	           "#include <windows.h>\n#include <cwinrt/cwinrt_piids.h>\n\n");
	for (i = 0; i < s->n; i++) {
		uint8_t const *g = s->piids + ( size_t ) i * 16;
		char           sym [1100];
		cwinrt_name_iid_symbol(s->names [i], sym, sizeof(sym));
		bprint(bh, "extern const IID %s;\n", sym);
		bprint(bc, "const IID %s = { 0x%02X%02X%02X%02X, 0x%02X%02X, 0x%02X%02X, "
		           "{ 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X } };\n",
		       sym, g [3], g [2], g [1], g [0], g [5], g [4], g [7], g [6], g [8], g [9], g [10], g [11], g [12],
		       g [13], g [14], g [15]);
	}
	bprint(bh, "\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
	rmbio(bh);
	rmbio(bc);
	rmarena(arena);
	printf("emit-piids: wrote %u PIIDs to %s and %s\n", s->n, hpath, cpath);
	return 0;
}

/* Collect PIIDs from the TypeSpec table and write the header + definition unit. */
int gen_emit_piids(char const *winmd_path, char const *out_dir) {
	winmd_db wm;
	piid_set s;

	if (!out_dir) out_dir = "include/cwinrt";
	if (winmd_open(winmd_path, &wm) != 0) return -1;
	memset(&s, 0, sizeof(s));
	piid_set_collect(&wm, &s);
	piid_set_write(&s, out_dir);
	piid_set_free(&s);
	winmd_close(&wm);
	return 0;
}

/* A growable set of owned strings, used to dedupe signatures. */
typedef struct str_set {
	char   **items;
	uint32_t n;
	uint32_t cap;
} str_set;

static int str_set_has(str_set const *s, char const *v) {
	uint32_t j;
	for (j = 0; j < s->n; j++)
		if (strcmp(s->items [j], v) == 0) return 1;
	return 0;
}

/* Append a copy of v; returns 0, or -1 if the backing array can't grow. */
static int str_set_add(str_set *s, char const *v) {
	if (s->n >= s->cap) {
		uint32_t nc = s->cap ? s->cap * 2 : 256;
		char   **ns = ( char ** ) realloc(s->items, nc * sizeof(char *));
		if (!ns) return -1;
		s->items = ns;
		s->cap   = nc;
	}
	s->items [s->n] = ( char * ) malloc(strlen(v) + 1);
	if (s->items [s->n]) memcpy(s->items [s->n++], v, strlen(v) + 1);
	return 0;
}

static void str_set_free(str_set *s) {
	uint32_t i;
	for (i = 0; i < s->n; i++) free(s->items [i]);
	free(s->items);
}

/* Enumerate every generic instantiation referenced by the TypeSpec table, build
   its WinRT signature, and print "<display-name> <PIID>" (deduped by signature).
   Feeds codegen and a broad differential coverage report. */
int gen_dump_piids(char const *winmd_path) {
	winmd_db wm;
	uint32_t ts_n;
	uint32_t i;
	str_set  seen;

	if (winmd_open(winmd_path, &wm) != 0) return -1;
	memset(&seen, 0, sizeof(seen));
	ts_n = wm.meta->tabs.rows [WINMD_TBL_TYPESPEC];
	for (i = 1; i <= ts_n; i++) {
		char    sig [4096];
		char    name [1024];
		uint8_t piid [16];
		if (cwinrt_sigbuild_typespec(&wm, i, sig, sizeof(sig), name, sizeof(name)) != 0) continue;
		if (str_set_has(&seen, sig)) continue;
		if (str_set_add(&seen, sig) != 0) break;
		cwinrt_piid_from_sig(sig, strlen(sig), piid);
		print_piid(name [0] ? name : sig, piid);
	}
	str_set_free(&seen);
	winmd_close(&wm);
	return 0;
}

/* Print classification, default-iface token, and built signature for one type. */
int gen_dump_sig(char const *winmd_path, char const *type_name) {
	winmd_db wm;
	uint32_t row;
	char     sig [4096];
	int      sb;

	if (winmd_open(winmd_path, &wm) != 0) return -1;
	row = winmd_typedef_find_by_name(&wm, type_name);
	printf("name=%s row=%u\n", type_name, row);
	if (row) {
		printf("  classify=%d default_iface=0x%08x\n", winmd_typedef_classify(wm.meta, row),
		       winmd_typedef_default_iface(wm.meta, row));
		sb = cwinrt_sigbuild_typedef(&wm, row, sig, sizeof(sig));
		printf("  sigbuild rc=%d sig=%s\n", sb, sb == 0 ? sig : "(fail)");
	}
	winmd_close(&wm);
	return 0;
}

int gen_selftest_piid_rfc(void) {
	/* RFC-4122 v5 test vector: DNS namespace + "www.example.com". */
	static uint8_t const dns [16]
	  = {0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1, 0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
	uint8_t u [16];
	int     k;
	cwinrt_uuid_v5(dns, "www.example.com", 15, u);
	printf("uuidv5(dns,www.example.com) = ");
	for (k = 0; k < 16; k++) printf("%02x", u [k]);
	printf("\n  expected            = 2ed6657de927568b95e12665a8aea6a2\n");
	return 0;
}
