#include <cwinrt/cast.h>

/* The single cast primitive: an explicit QueryInterface to a known IID.
   Callers pass the CWINRT_IID_<Type> constant from the namespace header. */
HRESULT cwinrt_query(void *obj, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!obj || !iid)
        return E_POINTER;
    return ((IUnknown *)obj)->lpVtbl->QueryInterface((IUnknown *)obj, iid, out);
}
