#pragma once

#include "model.h"

/* Assign unique c_name on each raw method (overload-aware, per-type ns prefix). */
void  cwinrt_assign_method_names(cwinrt_raw_db *db);

/* WinRT-style method comment: Type.Method(arg1, arg2) */
char *cwinrt_format_method_comment(
  int arena, char const *type_full, char const *method_name, char **param_names, uint32_t param_name_count,
  char const *overload_name
);

/* e.g. wuc_comp_new */
char *cwinrt_name_class_new(char const *ns_prefix, char const *winrt_ns, char const *type_short, int arena);
