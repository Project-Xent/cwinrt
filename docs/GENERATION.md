# Binding generation

## Prerequisites

- Windows SDK with Union metadata matching [sdk.version](../sdk.version) (currently `10.0.26100.0`), e.g.  
  `%ProgramFiles(x86)%\Windows Kits\10\UnionMetadata\10.0.26100.0\Windows.winmd`
- `xmake build cwinrt-gen`

Regenerating bindings on a different SDK revision may produce large diffs; keep `sdk.version` in sync with CI and committed headers.

## Generate all headers

```powershell
xmake build gen-headers
# or
powershell -File scripts/gen_all_windows.ps1
```

Writes `include/cwinrt/Windows.*.h` (one per `Windows.*` namespace).

## Generate all headers + vtable impl

```powershell
xmake build gen-all-impl
# or
powershell -File scripts/gen_all_windows.ps1 -Impl
```

Uses `cwinrt-gen --batch-union --impl`, writing:

- Headers: `include/cwinrt/`
- Thunks: `include/cwinrt/impl/*.impl.c`

Batch union defaults to **headers only**; pass `--impl` (or `-Impl` in the script) for `.impl.c`.

## Verify impl quality

```powershell
xmake build verify-bindings
# or
powershell -File scripts/verify_impl.ps1
```

Fails if any `.impl.c` still contains `E_NOTIMPL`.

## Linking bindings

| Target | Use |
|--------|-----|
| `cwinrt-rt` | Factory, bootstrap, activation |
| `cwinrt-bindings-foundation` | Foundation only |
| `cwinrt-bindings-composition` | UI Composition only |
| `cwinrt-bindings-all` | All `impl/*.impl.c` (after `gen-all-impl`) |

Example:

```text
xmake build myapp
# in xmake.lua: add_deps("cwinrt-rt", "cwinrt-bindings-all")
```

For smaller binaries, depend on individual `cwinrt-bindings-*` targets or compile only the `.impl.c` files you need.

## Header compile gate

Each `Windows.*.h` must compile alone (C prefixes collide if multiple namespaces share one TU).

```powershell
xmake build test_header_compile_smoke
xmake build header-compile-all
```

`header-compile-all` runs `scripts/header_compile_report.ps1` (one `.c` per header). The gate passes when **all 342** headers compile alone (`build_header_compile_shards.ps1 -MinPass 342`).

**Impl compile gate:** `xmake build impl-compile-all` compiles one TU per namespace that includes both the header and `impl/*.impl.c`, catching `conflicting types` between declarations and definitions. Pass threshold: **342** namespaces (`build_impl_compile_shards.ps1 -MinPass 342`).

**Full link gate:** `xmake build impl-link-all` links all 342 impl units via `cwinrt-bindings-all` into `test_impl_link_all`, surfacing duplicate symbols across namespaces.

After changing the generator, run `xmake build gen-all-impl` and re-run all three gates. Generated `compile_Windows_*.c` and `compile_impl_*.c` files are gitignored; `compile_smoke.c` is committed for fast CI smoke.

## What you can ship with

After `gen-all-impl` and `verify-bindings`:

- **342** `Windows.*` namespaces with headers + vtable/static thunks (`cwinrt-bindings-all`).
- Call WinRT from **pure C** (MSVC; MinGW for runtime smoke) via `cwinrt-rt` + selected or all bindings.
- **Static class APIs** (`get_Current`, `Create*`, policy getters) via generated facades + `cwinrt_factory_get_statics`.
- **Instance methods** on default interfaces where slots are mapped (Composition, Foundation.Uri, etc. in conform tests).

Current limitations: open generics are not fully type-accurate (some iterator returns map to `void**`), and not every delegate handler has a typedef.

## Conform / sparse package

See [tests/conform/README.md](../tests/conform/README.md) and `xmake register-conform-sparse` for Compositor runtime tests that need a sparse MSIX.
