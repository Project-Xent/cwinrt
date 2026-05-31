#pragma once

#include <unknwn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * cwinrt_query: the single cast primitive.
 *
 * QueryInterface `obj` to the interface identified by `iid` (use the per-interface
 * CWINRT_IID_<Type> constants emitted in each namespace header). Returns the new
 * reference in *out (caller releases), or sets *out = NULL on failure.
 *
 * This is an explicit, O(1) cast to a known interface -- preferred over the
 * runtime's GetIids()+blind-QI fallback used during activation.
 */
HRESULT cwinrt_query(void *obj, REFIID iid, void **out);

#ifdef __cplusplus
}
#endif
