#pragma once

#include "model.h"
#include <stdbool.h>

/* Class static params vs iface method params (iface includes *self). */
bool cwinrt_params_match_iface_static(char const *class_params, char const *iface_params);

/* Match a class instance wrapper's params against an interface method's params,
   skipping the leading `*self` on both sides. Distinguishes overloads. */
bool cwinrt_params_match_instance(char const *class_params, char const *iface_params);

/* Split param list at top-level commas; writes arg names into out (comma-separated). */
int  cwinrt_params_to_call_args(char const *params_c, char *out, size_t outsz);

/* True if signature has an instance pointer parameter. */
bool cwinrt_params_has_self(char const *params_c);

/* IClassShort + I*Statics* / I*Factory* / I*Static* naming convention. */
bool cwinrt_iface_relates_class(char const *iface_short, char const *class_short);

/* IClass + suffix interfaces used for class static API (not a separate runtime class). */
bool cwinrt_iface_serves_class_static(cwinrt_raw_db const *raw, char const *iface_short, char const *class_short);

/* True for ILauncherStatics-style facades (factory_get_statics), not ICoreApplication2. */
bool cwinrt_dispatch_iface_is_statics_facade(char const *iface_c_typedef);

/* ICoreImmersiveApplication for CoreApplication-style WinRT naming. */
bool cwinrt_iface_is_core_immersive_pattern(char const *iface_short, char const *class_short);

/* IWebUIActivationStatics for WebUIApplication-style activation statics. */
bool cwinrt_iface_is_activation_statics_pattern(char const *iface_short, char const *class_short);

int  cwinrt_static_iface_rank(char const *iface_short, char const *class_short);

/* Skip iface method when the same c_name is projected on a runtime class. */
bool cwinrt_mapped_skip_iface_method_dup(
  cwinrt_mapped_unit const *unit, cwinrt_mapped_type const *t, cwinrt_mapped_method const *m
);
