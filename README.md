# cwinrt

Pure C projection for the Windows Runtime.

- **Use it:** [docs/INTEGRATION.md](docs/INTEGRATION.md) (link into your build) · [docs/COOKBOOK.md](docs/COOKBOOK.md) (recipes + C++/WinRT migration) · [docs/MANGLING.md](docs/MANGLING.md) (predict C symbol names)
- **Generate it:** [docs/GENERATION.md](docs/GENERATION.md) (regenerate bindings from your own SDK)

## Build

Requires [xmake](https://xmake.io) on Windows.

```text
xmake build cwinrt-rt test_smoke
xmake run test_smoke
```

### Generator (pure C)

`gen/winmd.c` reads `.winmd` as PE + ECMA-335 `#~` metadata (no `cor.h` / COM).

```text
xmake build cwinrt-gen
xmake run cwinrt-gen --winmd path\to\Windows.UI.Composition.winmd -o include/cwinrt
```

Works with MSVC and MinGW.

### Batch generation

Generates one header per `Windows.*` namespace from union metadata (`Windows.winmd`), e.g. `Windows.UI.Composition.h`, `Windows.Graphics.Capture.h`. SDK version is pinned in [sdk.version](sdk.version); scripts read it by default.

```powershell
xmake build cwinrt-gen
xmake build gen-headers          # headers only
xmake build gen-all-impl         # headers + impl/*.impl.c (~342 namespaces)
xmake build verify-bindings      # fails if any E_NOTIMPL in impl
xmake build header-compile-all   # each header compiles in its own TU
xmake build impl-compile-all     # each header+impl pair compiles in one TU
xmake build impl-link-all        # all impl units link together
```

See [docs/GENERATION.md](docs/GENERATION.md) for linking (`cwinrt-rt`, `cwinrt-bindings-all`) and CI expectations.

Single namespace:

```text
build\windows\x64\release\cwinrt-gen.exe --winmd "%ProgramFiles(x86)%\Windows Kits\10\UnionMetadata\10.0.26100.0\Windows.winmd" --ns Windows.UI.Composition -o include/cwinrt
```

All namespaces (~340):

```text
cwinrt-gen.exe --batch-union --winmd ...\Windows.winmd -o include/cwinrt
```

## Layout

- `gen/` — metadata parser and header emitter
- `rt/` — runtime (`init`, `hstring`, `factory`, `async`, `event`)
- `include/cwinrt/` — public headers (generated + runtime includes)
- `thirdparty/coetua/` — vendored utilities used by `cwinrt-gen` only (generated bindings and `rt/` do not depend on coetua)
