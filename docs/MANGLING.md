# Name mangling (frozen spec)

Every WinRT type and method maps to a C identifier by deterministic rules. This
document is the contract: the same metadata always produces the same C names, and
a new SDK that *inserts* members never renames existing ones (see "Stability").
If you can read these rules you can predict any symbol without grepping.

> **Shortcut:** every generated declaration carries a comment with its exact WinRT
> origin, e.g. `/* Windows.Globalization.Calendar.get_NumeralSystem() */`. To find
> the C name for a known WinRT member, grep the header for that comment.

## 1. Namespace prefix

Each namespace has a short lowercase prefix. A handful are fixed; the rest are
derived as `w` + the first ≤2 lowercased letters of each dot-segment after
`Windows.`:

| WinRT namespace | prefix | source |
| --- | --- | --- |
| `Windows.Foundation` | `wf` | fixed |
| `Windows.Foundation.Collections` | `wfoco` | fixed |
| `Windows.UI.Composition` | `wuc` | fixed |
| `Windows.Storage.Streams` | `wstst` | fixed |
| `Windows.Globalization` | `wgl` | derived (`w`+`gl`) |
| `Windows.System.Threading` | `wsyth` | derived (`w`+`sy`+`th`) |
| `Windows.Graphics.Capture` | `wgc` | fixed |

The full fixed table lives in `gen/name.c` (`g_ns_rules`). Type names uppercase the
prefix (`WGL_…`); method names keep it lowercase (`wgl_…`).

## 2. Type names — `PREFIX_TypeName`

`<UPPER_PREFIX>_<PascalName>`. The short name is kept in PascalCase; a leading `I`
on an interface is preserved.

```
Windows.Globalization.Calendar          -> WGL_Calendar
Windows.Foundation.IPropertyValue       -> WF_IPropertyValue
Windows.UI.Composition.ICompositionObject -> WUC_ICompositionObject
```

A few long type names have explicit abbreviations (in `g_type_abbrevs`) so the
*method* prefixes stay short — these also shorten the typedef:

```
Windows.UI.Composition.Compositor       -> WUC_Comp     (abbrev "comp")
Windows.UI.Composition.SpriteVisual      -> WUC_Sprite   (abbrev "sprite")
```

## 3. Method names — `prefix_type_method`

`<prefix>_<type_snake>_<method_snake>`, where snake-casing inserts `_` before each
uppercase letter and lowercases it. The type part uses the abbreviation from §2.

```
Windows.Globalization.Calendar.SetToNow()    -> wgl_calendar_set_to_now
Windows.UI.Composition.Compositor.CreateColorBrush() -> wuc_comp_create_color_brush
```

### Property accessors

WinRT exposes properties as `get_Name` / `put_Name` methods. Because the WinRT name
already contains an underscore *and* snake-casing inserts one before the capital,
property accessors carry a characteristic **double** underscore:

```
get_NumeralSystem  -> wgl_calendar_get__numeral_system
put_NumeralSystem  -> wgl_calendar_put__numeral_system
```

A regular method whose WinRT name merely starts with `Get`/`Put` (no underscore)
has a single one — this is how you tell them apart:

```
GetCalendarSystem() (method)   -> wgl_calendar_get_calendar_system
```

### Events

An `add_X` / `remove_X` event pair becomes a subscribe/unsubscribe pair named
`on_x` / `off_x`:

```
add_Changed / remove_Changed   -> wuc_composition_capabilities_on_changed
                                  wuc_composition_capabilities_off_changed
```

A method named `OnSomething` (a handler-style name) becomes `handle_something`.

### Activation

A runtime class's default constructor is `<prefix>_<type_abbrev>_new`:

```
new Calendar()      -> wgl_calendar_new(WGL_Calendar **out)
new Compositor()    -> wuc_comp_new(WUC_Comp **out)
```

## 4. Overloads — type mangle, never ordinals

WinRT allows several methods with the same name. cwinrt never uses positional
suffixes like `_1`/`_2` (those would renumber when the SDK inserts an overload).
Instead, within a same-named group:

1. **One base keeps the clean name** — the `[default]` overload, else the one with
   the fewest parameters.
2. **The others** take the WinRT `[Overload("…")]` attribute name if present;
   otherwise a deterministic suffix built from the *parameter types*:
   - per-parameter tokens: `i32 u32 i64 u64 i16 u16 u8 f32 f64 b str`, pointer → `p`,
     any other type → its name minus the namespace prefix, lowercased;
   - joined by `_`, e.g. two-arg `(int32, IFoo*)` → `…_i32_p`;
   - a parameterless overload → its arity (`…_0`);
   - if the readable mangle exceeds 44 chars it becomes `head36_<fnv1a32>` so
     distinct long signatures never collide.
3. **Last-resort collision** (same `[Overload]` name or identical mangle): append
   `_static`/`_instance` or `_x`.

All three steps depend only on the signature, not on metadata order.

## 5. Generic instantiations

Closed generic interfaces (`IVectorView<String>`, `IIterable<float>`, …) get one
concrete typedef per instantiation: `<PREFIX>_<GenericName>_<ArgMangle>`, where each
type argument mangles as a primitive token (`HSTRING`, `F32`, `F64`, `Object`, …)
or, for a class/interface argument, its own `PREFIX_TypeName`. Nested generics
recurse.

```
IVectorView<String>          -> WFOCO_IVectorView_HSTRING
IIterable<float>             -> WFOCO_IIterable_F32
IIterable<DeviceInformation> -> WFOCO_IIterable_WDEEN_DeviceInformation
IKeyValuePair<Guid,Object>   -> WFOCO_IKeyValuePair_Guid_Object
```

## 6. IID / PIID constants

Each interface emits a 16-byte IID constant named `CWINRT_IID_<c_typedef>`; a closed
generic emits its parameterized IID (PIID) under the same scheme keyed on the
instantiation typedef:

```
WF_IPropertyValue            -> CWINRT_IID_WF_IPropertyValue
WFOCO_IVectorView_HSTRING    -> CWINRT_IID_WFOCO_IVectorView_HSTRING   (in cwinrt_piids.h)
```

Pass these to `cwinrt_query` / `cwinrt_async_get` / `cwinrt_delegate_create`.

## Stability

- Names are a pure function of the pinned SDK metadata (`sdk.version`); regenerating
  on the same SDK is byte-identical.
- Because overloads mangle by type (not order) and generics mangle by argument (not
  index), an SDK that *adds* a member emits a *new* symbol and leaves every existing
  name unchanged. Removing or retyping a member is the only thing that breaks a name
  — i.e. exactly when the underlying WinRT ABI broke too.
