#include "name.h"

#include "arena.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct ns_rule {
	char const *ns;
	char const *prefix;
} ns_rule;

static ns_rule const g_ns_rules [] = {
  {"Windows.UI.Composition",                  "wuc"  },
  {"Windows.UI",                              "wui"  },
  {"Windows.Foundation",                      "wf"   },
  {"Windows.Foundation.Numerics",             "wfn"  },
  {"Windows.Foundation.Collections",          "wfoco"},
  {"Windows.Graphics",                        "wg"   },
  {"Windows.Graphics.Capture",                "wgc"  },
  {"Windows.Devices.Bluetooth.Advertisement", "wdba" },
  {"Windows.Storage",                         "ws"   },
  {"Windows.Storage.Streams",                 "wstst"},
  {"Windows.System",                          "wsy"  },
  {"Windows.ApplicationModel",                "wam"  },
  {NULL,                                      NULL   }
};

typedef struct type_abbrev_rule {
	char const *ns; /* NULL = any namespace */
	char const *short_name;
	char const *abbrev;
} type_abbrev_rule;

static type_abbrev_rule const g_type_abbrevs [] = {
  {"Windows.UI.Composition", "Compositor",                "comp"          },
  {"Windows.UI.Composition", "SpriteVisual",              "sprite"        },
  {"Windows.UI.Composition", "ContainerVisual",           "container"     },
  {"Windows.UI.Composition", "Visual",                    "visual"        },
  {"Windows.UI.Composition", "DesktopWindowTarget",       "target"        },
  {"Windows.UI.Composition", "CompositionBrush",          "brush"         },
  {"Windows.UI.Composition", "CompositionBackdropBrush",  "backdrop_brush"},
  {"Windows.UI.Composition", "ConnectedAnimationService", "conn_anim_svc" },
  {"Windows.Foundation",     "Uri",                       "uri"           },
  {"Windows.Foundation",     "GuidHelper",                "guid"          },
  {NULL,                     NULL,                        NULL            }
};

#if defined(_WIN32)
  #define CWINRT_TLS __declspec(thread)
#else
  #define CWINRT_TLS __thread
#endif

static CWINRT_TLS char g_derived_prefix [16];
static CWINRT_TLS char g_type_abbrev_buf [64];

/* w + up to 2 lowercase letters per segment after "Windows." (unique, no bare "win"). */
static char const     *prefix_derive(char const *winrt_ns) {
	char const *p        = winrt_ns;
	size_t      o        = 0;

	g_derived_prefix [0] = '\0';
	if (!winrt_ns || !*winrt_ns) return "wx";
	if (strncmp(p, "Windows.", 8) == 0) {
		g_derived_prefix [o++]  = 'w';
		p                      += 8;
	}
	while (*p && o + 1 < sizeof(g_derived_prefix)) {
		int n = 0;
		if (*p == '.') {
			p++;
			continue;
		}
		while (*p && *p != '.' && n < 2 && o + 1 < sizeof(g_derived_prefix)) {
			g_derived_prefix [o++] = ( char ) tolower(( unsigned char ) *p++);
			n++;
		}
		while (*p && *p != '.') p++;
	}
	g_derived_prefix [o] = '\0';
	if (o == 0) snprintf(g_derived_prefix, sizeof(g_derived_prefix), "wx");
	return g_derived_prefix;
}

char const *cwinrt_name_ns_prefix(char const *winrt_ns) {
	int i;
	if (!winrt_ns) return "wx";
	for (i = 0; g_ns_rules [i].ns; i++)
		if (strcmp(winrt_ns, g_ns_rules [i].ns) == 0) return g_ns_rules [i].prefix;
	return prefix_derive(winrt_ns);
}

void cwinrt_name_sanitize_short(char *dst, char const *src, size_t cap) {
	size_t i = 0;
	size_t o = 0;
	size_t len;
	size_t dstart;
	if (!dst || cap < 2) {
		if (dst && cap) dst [0] = '\0';
		return;
	}
	if (!src || !src [0]) {
		dst [0] = '\0';
		return;
	}
	while (src [i] && o + 1 < cap) {
		char c = src [i];
		if (c == '`' || c == '<' || c == '>' || c == ',') c = '_';
		dst [o++] = c;
		i++;
	}
	dst [o] = '\0';
	len     = o;
	dstart  = len;
	while (dstart > 0 && dst [dstart - 1] >= '0' && dst [dstart - 1] <= '9') dstart--;
	if (dstart > 0 && dstart < len && dst [dstart - 1] != '_') {
		char tmp [256];
		snprintf(tmp, sizeof(tmp), "%.*s_%s", ( int ) dstart, dst, dst + dstart);
		snprintf(dst, cap, "%s", tmp);
	}
}

bool cwinrt_name_is_c_keyword(char const *name) {
	static char const *kw [] = {
	  "_Bool", "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
	  "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "return", "short", "signed",
	  "sizeof", "static", "struct", "switch", "typedef", "type", "union", "unsigned", "void", "volatile", "while",
	  NULL};
	int i;
	if (!name || !name [0]) return false;
	for (i = 0; kw [i]; i++)
		if (strcmp(name, kw [i]) == 0) return true;
	return false;
}

void cwinrt_name_sanitize_param_ident(char *dst, char const *src, size_t cap) {
	if (!dst || cap < 2) return;
	if (!src || !src [0]) {
		dst [0] = '\0';
		return;
	}
	snprintf(dst, cap, "%s", src);
	if (cwinrt_name_is_c_keyword(dst)) {
		size_t n = strlen(dst);
		if (n + 2 < cap) {
			dst [n]     = '_';
			dst [n + 1] = '\0';
		}
	}
}

static bool abbrev_is_break(char c) { return c == '`' || c == '<' || c == '>' || c == ','; }

static void abbrev_fallback(char const *short_name, char *dst, size_t cap) {
	size_t len;
	size_t i;
	size_t o = 0;

	if (!dst || cap < 2 || !short_name || !short_name [0]) {
		if (dst && cap) snprintf(dst, cap, "x");
		return;
	}
	/* No suffix-stripping: explicit abbreviations live in g_type_abbrevs (checked
	 * before this fallback). Stripping suffixes here (Visual/Animation/Collection/
	 * Factory) diverged function names from typedef names and manufactured collisions
	 * (e.g. NotificationVisual -> "notification" clashing with class Notification). */
	len = strlen(short_name);
	for (i = 0; i < len && o + 1 < cap; i++) {
		char c = short_name [i];
		if (abbrev_is_break(c)) break;
		if (c >= 'A' && c <= 'Z') {
			if (o > 0) dst [o++] = '_';
			dst [o++] = ( char ) (c - 'A' + 'a');
			continue;
		}
		dst [o++] = c;
	}
	dst [o] = '\0';
	if (!dst [0]) snprintf(dst, cap, "x");
}

char const *cwinrt_name_type_abbrev(char const *winrt_ns, char const *short_name) {
	int  i;
	char safe [256];

	g_type_abbrev_buf [0] = '\0';
	if (!short_name || !short_name [0]) return "x";
	cwinrt_name_sanitize_short(safe, short_name, sizeof(safe));
	if (safe [0] == 'I' && safe [1] >= 'A' && safe [1] <= 'Z') return cwinrt_name_type_abbrev(winrt_ns, safe + 1);
	for (i = 0; g_type_abbrevs [i].short_name; i++) {
		if (g_type_abbrevs [i].ns && winrt_ns && strcmp(g_type_abbrevs [i].ns, winrt_ns) != 0) continue;
		if (strcmp(g_type_abbrevs [i].short_name, safe) == 0) return g_type_abbrevs [i].abbrev;
	}
	abbrev_fallback(safe, g_type_abbrev_buf, sizeof(g_type_abbrev_buf));
	return g_type_abbrev_buf;
}

static void abbrev_to_typedef_part(char const *abbrev, char *dst, size_t cap) {
	size_t di   = 0;
	size_t i    = 0;
	bool   word = true;
	if (!dst || cap < 2) {
		if (dst && cap) dst [0] = '\0';
		return;
	}
	if (!abbrev || !abbrev [0]) {
		snprintf(dst, cap, "X");
		return;
	}
	for (i = 0; abbrev [i] && di + 1 < cap; i++) {
		char c = abbrev [i];
		if (c == '_') {
			word = true;
			continue;
		}
		if (word) {
			dst [di++] = ( char ) toupper(( unsigned char ) c);
			word       = false;
		}
		else dst [di++] = c;
	}
	dst [di] = '\0';
}

static void name_copy_lower(char *dst, char const *src, size_t cap) {
	size_t i = 0;
	while (src [i] && i + 1 < cap) {
		char c = src [i];
		if (c >= 'A' && c <= 'Z') c = ( char ) (c - 'A' + 'a');
		dst [i] = c;
		i++;
	}
	dst [i] = '\0';
}

static void safe_to_typedef_part(char const *safe, char *dst, size_t cap) {
	size_t di   = 0;
	size_t i    = 0;
	bool   word = true;
	if (!safe || !dst || cap < 2) {
		if (dst && cap) dst [0] = '\0';
		return;
	}
	if (safe [0] == 'I' && safe [1] >= 'A' && safe [1] <= 'Z') {
		dst [di++] = 'I';
		i          = 1;
		word       = true;
	}
	for (; safe [i] && di + 1 < cap; i++) {
		char c = safe [i];
		if (c == '_') {
			word       = true;
			dst [di++] = '_';
			continue;
		}
		if (word) {
			dst [di++] = ( char ) toupper(( unsigned char ) c);
			word       = false;
		}
		else dst [di++] = c;
	}
	dst [di] = '\0';
}

/* True if `safe` has an explicit abbreviation entry for namespace `winrt_ns`. */
static bool type_has_abbrev(char const *winrt_ns, char const *safe) {
	int i;
	for (i = 0; g_type_abbrevs [i].short_name; i++) {
		if (g_type_abbrevs [i].ns && winrt_ns && strcmp(g_type_abbrevs [i].ns, winrt_ns) != 0) continue;
		if (strcmp(g_type_abbrevs [i].short_name, safe) == 0) return true;
	}
	return false;
}

static void ns_prefix_upper(char const *ns_prefix, char *upper, size_t cap) {
	size_t plen = strlen(ns_prefix);
	size_t j;
	for (j = 0; j < plen && j + 1 < cap; j++)
		upper [j] = ( char ) (ns_prefix [j] >= 'a' && ns_prefix [j] <= 'z' ? ns_prefix [j] - 'a' + 'A' : ns_prefix [j]);
	upper [j] = '\0';
}

char *cwinrt_name_type(char const *ns_prefix, char const *winrt_ns, char const *short_name, int arena) {
	char  *buf;
	char   safe [256];
	char   typedef_part [128];
	char   upper [16];
	size_t len;
	if (!ns_prefix || !short_name) return NULL;
	cwinrt_name_sanitize_short(safe, short_name, sizeof(safe));
	if (type_has_abbrev(winrt_ns, safe))
		abbrev_to_typedef_part(cwinrt_name_type_abbrev(winrt_ns, safe), typedef_part, sizeof(typedef_part));
	else safe_to_typedef_part(safe, typedef_part, sizeof(typedef_part));
	len = strlen(ns_prefix) + 1 + strlen(typedef_part) + 8;
	buf = ( char * ) aden(arena, len);
	if (!buf) return NULL;
	ns_prefix_upper(ns_prefix, upper, sizeof(upper));
	snprintf(buf, len, "%s_%s", upper, typedef_part);
	return buf;
}

static void name_snake(char *dst, char const *src, size_t cap) {
	size_t di = 0;
	size_t i  = 0;
	if (!src || cap < 2) return;
	for (i = 0; src [i] && di + 2 < cap; i++) {
		char c = src [i];
		if (c == '`' || c == '<' || c == '>' || c == ',') c = '_';
		if (c >= 'A' && c <= 'Z') {
			if (di > 0) dst [di++] = '_';
			dst [di++] = ( char ) (c - 'A' + 'a');
			continue;
		}
		dst [di++] = c;
	}
	dst [di] = '\0';
}

char *cwinrt_name_method(char const *ns_prefix, char const *type_short, char const *method_name, int arena) {
	return cwinrt_name_method_unique(ns_prefix, type_short, method_name, 0, arena);
}

char *cwinrt_name_method_unique(
  char const *ns_prefix, char const *type_short, char const *method_name, uint32_t method_token, int arena
) {
	char  *buf;
	char   type_snake [256];
	char   meth_snake [256];
	size_t len;
	if (!ns_prefix || !type_short || !method_name) return NULL;
	name_snake(type_snake, type_short, sizeof(type_snake));
	name_snake(meth_snake, method_name, sizeof(meth_snake));
	len = strlen(ns_prefix) + strlen(type_snake) + strlen(meth_snake) + 16;
	buf = ( char * ) aden(arena, len);
	if (!buf) return NULL;
	if (method_token) snprintf(buf, len, "%s_%s_%s_m%06x", ns_prefix, type_snake, meth_snake, method_token & 0xffffffu);
	else snprintf(buf, len, "%s_%s_%s", ns_prefix, type_snake, meth_snake);
	return buf;
}

static void name_typedef_part(char const *winrt_ns, char const *shortn, char *part, size_t cap) {
	char        safe [256];
	char const *abbrev;
	int         i;
	bool        use_abbrev = false;

	if (!part || cap < 2) {
		if (part && cap) part [0] = '\0';
		return;
	}
	if (!shortn || !shortn [0]) {
		snprintf(part, cap, "X");
		return;
	}
	cwinrt_name_sanitize_short(safe, shortn, sizeof(safe));
	for (i = 0; g_type_abbrevs [i].short_name; i++) {
		if (g_type_abbrevs [i].ns && winrt_ns && strcmp(g_type_abbrevs [i].ns, winrt_ns) != 0) continue;
		if (strcmp(g_type_abbrevs [i].short_name, safe) == 0) {
			use_abbrev = true;
			break;
		}
	}
	if (use_abbrev) {
		abbrev = cwinrt_name_type_abbrev(winrt_ns, safe);
		abbrev_to_typedef_part(abbrev, part, cap);
	}
	else safe_to_typedef_part(safe, part, cap);
}

void cwinrt_name_winrt_to_c(char const *winrt_full, char *buf, size_t bufsz) {
	char const *dot;
	char const *shortn;
	char        nsbuf [128];
	char const *ns = "";
	char const *pfx;
	char        upper [16];
	char        part [128];
	size_t      plen;
	size_t      j;

	if (!buf || bufsz < 2) return;
	if (!winrt_full || !*winrt_full) {
		buf [0] = '\0';
		return;
	}
	dot    = strrchr(winrt_full, '.');
	shortn = dot ? dot + 1 : winrt_full;
	if (dot && dot > winrt_full) {
		size_t n = ( size_t ) (dot - winrt_full);
		if (n >= sizeof(nsbuf)) n = sizeof(nsbuf) - 1;
		memcpy(nsbuf, winrt_full, n);
		nsbuf [n] = '\0';
		ns        = nsbuf;
	}
	pfx  = cwinrt_name_ns_prefix(ns);
	plen = strlen(pfx);
	for (j = 0; j < plen && j + 1 < sizeof(upper); j++)
		upper [j] = ( char ) (pfx [j] >= 'a' && pfx [j] <= 'z' ? pfx [j] - 'a' + 'A' : pfx [j]);
	upper [j] = '\0';
	name_typedef_part(ns, shortn, part, sizeof(part));
	snprintf(buf, bufsz, "%s_%s", upper, part);
}

void cwinrt_name_header_from_ns(char const *filter_ns, char *hdr, size_t hdr_sz) {
	if (!hdr || hdr_sz < 2) return;
	if (!filter_ns || !*filter_ns) {
		snprintf(hdr, hdr_sz, "cwinrt.h");
		return;
	}
	snprintf(hdr, hdr_sz, "%s.h", filter_ns);
}

void cwinrt_name_guard_from_ns(char const *filter_ns, char *guard, size_t guard_sz) {
	size_t i;
	if (!guard || guard_sz < 8) return;
	snprintf(guard, guard_sz, "CWINRT_");
	i = strlen(guard);
	if (!filter_ns) {
		strncat(guard, "H", guard_sz - i - 1);
		return;
	}
	for (; *filter_ns && i + 2 < guard_sz; filter_ns++) {
		char c = *filter_ns;
		if (c == '.') c = '_';
		else if (c >= 'a' && c <= 'z') c = ( char ) (c - 'a' + 'A');
		guard [i++] = c;
	}
	guard [i] = '\0';
	strncat(guard, "_H", guard_sz - i - 1);
}

void cwinrt_name_include_for_ns(char const *winrt_ns, char *inc, size_t inc_sz) {
	if (!inc || inc_sz < 8) return;
	if (!winrt_ns || !*winrt_ns) {
		snprintf(inc, inc_sz, "<cwinrt/cwinrt.h>");
		return;
	}
	snprintf(inc, inc_sz, "<cwinrt/%s.h>", winrt_ns);
}

void cwinrt_name_iid_symbol(char const *c_typedef, char *buf, size_t bufsz) {
	if (!buf || bufsz < 8) return;
	if (!c_typedef || !*c_typedef) {
		snprintf(buf, bufsz, "IID_unknown");
		return;
	}
	snprintf(buf, bufsz, "CWINRT_IID_%s", c_typedef);
}
