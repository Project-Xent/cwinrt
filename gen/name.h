#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

char const *cwinrt_name_ns_prefix(char const *winrt_ns);

/* Lowercase type slug for C method names (e.g. Compositor -> comp). */
char const *cwinrt_name_type_abbrev(char const *winrt_ns, char const *short_name);

char       *cwinrt_name_type(char const *ns_prefix, char const *winrt_ns, char const *short_name, int arena);

char       *cwinrt_name_method(char const *ns_prefix, char const *type_short, char const *method_name, int arena);

char       *cwinrt_name_method_unique(
  char const *ns_prefix, char const *type_short, char const *method_name, uint32_t method_token, int arena
);

void cwinrt_name_winrt_to_c(char const *winrt_full, char *buf, size_t bufsz);

/* Sanitize WinRT short name for C identifiers (trailing version digits, generics). */
void cwinrt_name_sanitize_short(char *dst, char const *src, size_t cap);

bool cwinrt_name_is_c_keyword(char const *name);

/* Safe C parameter identifier (keywords get a trailing '_'). */
void cwinrt_name_sanitize_param_ident(char *dst, char const *src, size_t cap);

void cwinrt_name_header_from_ns(char const *filter_ns, char *hdr, size_t hdr_sz);

void cwinrt_name_guard_from_ns(char const *filter_ns, char *guard, size_t guard_sz);

void cwinrt_name_include_for_ns(char const *winrt_ns, char *inc, size_t inc_sz);

void cwinrt_name_iid_symbol(char const *c_typedef, char *buf, size_t bufsz);
