# Cookbook

Task-oriented recipes. Every one is distilled from a runtime test under
`tests/conform/` that activates real WinRT and passes on hardware — the cited file
is the runnable, complete version. Symbol names follow [MANGLING.md](MANGLING.md);
integration (headers + link libs) is in [INTEGRATION.md](INTEGRATION.md).

All examples assume:

```c
#include <cwinrt/init.h>
/* + the per-namespace header(s) you use, e.g. <cwinrt/Windows.Globalization.h> */
```

and are bracketed by init/uninit. Errors are `HRESULT`; check with `FAILED(hr)`.
Every interface pointer you receive owns a reference — release it with
`((IUnknown *)p)->lpVtbl->Release((IUnknown *)p)`.

## Initialize the runtime

```c
HRESULT hr = cwinrt_init(RO_INIT_MULTITHREADED);   /* or RO_INIT_SINGLETHREADED for UI/Composition */
/* ... */
cwinrt_uninit();   /* also releases cached activation factories */
```

## Activate a runtime class

The default constructor is `<type>_new` (§3 of MANGLING). It returns a typed pointer.

```c
WGL_Calendar *cal = NULL;
hr = wgl_calendar_new(&cal);
/* ... use cal ... */
((IUnknown *)cal)->lpVtbl->Release((IUnknown *)cal);
```

For a class with no default constructor, get its factory/statics by name and call a
`Create…` method (see *Async with result* below for a `…Factory` example):

```c
WSTST_IDataWriterFactory *fac = NULL;
hr = cwinrt_factory_get_statics(L"Windows.Storage.Streams.DataWriter",
                                &CWINRT_IID_WSTST_IDataWriterFactory, (void **)&fac);
```

*(full example: `tests/conform/e2e_headless.c`, `e2e_async_progress.c`)*

## Cast / QueryInterface

`cwinrt_query` is the one cast primitive. Pass the `CWINRT_IID_<Type>` constant for
the interface you want; you get a new reference (release it separately).

```c
IUnknown *obj = NULL;
hr = cwinrt_query(brush, &CWINRT_IID_WUC_ICompositionObject, (void **)&obj);
```

*(`tests/conform/e2e_composition.c`, `e2e_piid.c`)*

## Call methods down a vtable; round-trip a value type

Generated methods take the typed `self` first, `[out]` retvals last. By-value
structs (`WF_Point`, `WUI_Color`, `WF_DateTime`, …) cross the ABI as real values.

```c
WUI_Color set = { 255, 10, 20, 30 }, got = { 0 };
hr = wuc_composition_color_brush_put__color(brush, set);   /* property setter */
hr = wuc_composition_color_brush_get__color(brush, &got);  /* property getter */
```

*(`tests/conform/e2e_composition.c`)*

## Strings (HSTRING)

`cwinrt_hstring` is `HSTRING`. Build from UTF-16 or UTF-8; free what you create.
For a string *literal*, the stack macro avoids an allocation.

```c
cwinrt_hstring s = NULL;
hr = cwinrt_hstring_from(L"hello", &s);
/* pass s to a WinRT method ... */
cwinrt_hstring_free(s);

/* literal, no heap: */
CWINRT_HSTRING_STACK(lit, L"Windows.Storage.Streams.DataWriter");
/* `lit` is valid until end of scope */
```

UTF-8 in/out: `cwinrt_hstring_from_utf8`, `cwinrt_hstring_to_utf8`.

## Collections via a parameterized IID

A method returning `IVectorView<String>` gives you the concrete typedef
`WFOCO_IVectorView_HSTRING`. QI it by its PIID, then call slots (here `get_Size`).

```c
WFOCO_IVectorView_HSTRING *langs = NULL;
hr = wgl_calendar_get__languages(cal, &langs);

void *view = NULL;
hr = cwinrt_query(langs, &CWINRT_IID_WFOCO_IVectorView_HSTRING, &view);
```

*(`tests/conform/e2e_piid.c`)*

## Boxing / unboxing (PropertyValue)

Box a scalar or by-value struct through `Windows.Foundation.PropertyValue`, unbox via
`IPropertyValue` getters.

```c
IInspectable      *boxed = NULL;
WF_IPropertyValue *pv    = NULL;
uint8_t            got   = 0;

hr = wf_property_value_create_u_int8(0xAB, &boxed);
hr = cwinrt_query(boxed, &CWINRT_IID_WF_IPropertyValue, (void **)&pv);
hr = wf_property_value_get_u_int8(pv, &got);   /* got == 0xAB */
```

The same shape works for `_single`, `_point`, `_date_time`, `_guid`, … *(`tests/conform/e2e_box.c`)*

## Delegates and events

Wrap a C callback as a WinRT delegate with `cwinrt_delegate_create`, passing the
delegate's IID. Pass the resulting object where WinRT wants the delegate.

```c
static void on_work(void *sender, void *args, void *ctx) { /* ... */ }

IUnknown *handler = NULL;
hr = cwinrt_delegate_create(&CWINRT_IID_WSYTH_WorkItemHandler, on_work, NULL, &handler);
hr = wsyth_thread_pool_run_async((WSYTH_WorkItemHandler *)handler, &action);
/* release handler after the call returns */
```

For `add_X`/`remove_X` events, the generated `on_x`/`off_x` functions take a
`cwinrt_event_fn` directly and return/consume a `cwinrt_token` — no manual delegate
needed. *(delegate path: `tests/conform/e2e_async.c`)*

## Async — await an operation

Pick the entry point by the *shape* of the async pointer (the C type name tells you):

| You hold | Await with |
| --- | --- |
| `IAsyncAction`, `IAsyncOperation<T>` | `cwinrt_async_wait` / `cwinrt_async_get` |
| `…WithProgress<…>` | `cwinrt_async_wait_with_progress` / `cwinrt_async_get_with_progress` |

The WithProgress interfaces insert a `put/get_Progress` pair, moving `Completed` to a
later vtable slot — calling the wrong one hits `put_Progress` and hangs.

```c
/* no result: */
hr = cwinrt_async_wait((IUnknown *)action, 10000);          /* ms, or INFINITE */

/* with progress, then read the typed result off the concrete operation: */
hr = cwinrt_async_wait_with_progress((IUnknown *)op, INFINITE);
uint32_t stored = 0;
hr = wstst_data_writer_store_operation_get_results(op, &stored);
```

`cwinrt_async_get(op, &CWINRT_IID_<ResultIface>, &result)` waits *and* casts the
result in one call. *(`tests/conform/e2e_async.c`, `e2e_async_progress.c`)*

---

# Migrating from C++/WinRT

cwinrt is the same WinRT ABI without C++. The mental mapping:

| C++/WinRT | cwinrt |
| --- | --- |
| `winrt::init_apartment()` | `cwinrt_init(RO_INIT_MULTITHREADED)` |
| `Calendar c;` (or `Calendar c{};`) | `WGL_Calendar *c; wgl_calendar_new(&c);` |
| `c.SetToNow();` | `wgl_calendar_set_to_now(c);` |
| `auto y = c.Year();` (property get) | `int32_t y; wgl_calendar_get__year(c, &y);` |
| `c.NumeralSystem(v);` (property set) | `wgl_calendar_put__numeral_system(c, v);` |
| `obj.as<ICompositionObject>()` | `cwinrt_query(obj, &CWINRT_IID_WUC_ICompositionObject, &out)` |
| `obj.try_as<T>()` | `cwinrt_query(...)` and check `HRESULT` |
| `winrt::hstring{L"x"}` | `cwinrt_hstring_from(L"x", &hs)` (free it) / `CWINRT_HSTRING_STACK` |
| `co_await op;` / `op.get();` | `cwinrt_async_wait((IUnknown*)op, INFINITE);` |
| `co_await progressOp;` | `cwinrt_async_wait_with_progress((IUnknown*)op, INFINITE);` |
| `box_value(x)` | `wf_property_value_create_*(x, &boxed)` |
| `unbox_value<T>(o)` | `cwinrt_query(o, &CWINRT_IID_WF_IPropertyValue, &pv)` + `wf_property_value_get_*` |
| `event += handler;` | `…_on_event(self, fn, ctx)` → returns `cwinrt_token` |
| `event -= token;` | `…_off_event(self, token)` |
| RAII release at scope end | explicit `p->lpVtbl->Release(p)` |
| `winrt::guid_of<T>()` | `CWINRT_IID_<T>` constant |

Key differences to keep in mind: no exceptions (check every `HRESULT`), no automatic
ref-counting (release every interface you receive), `[out]` parameters instead of
return values, and property getters/setters are explicit `get__`/`put__` functions.
