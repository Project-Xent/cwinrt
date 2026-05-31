# Integrating cwinrt

cwinrt ships C headers (`include/cwinrt/*.h`), one `*.impl.c` per Windows namespace
(`include/cwinrt/impl/`), and a small runtime (`rt/*.c`). To consume it you compile the
runtime + the impl units you use, put `include/` on the header path, and link the WinRT
libraries. The supported, lowest-friction way to do all of that is the `cwinrt` xmake
target.

## xmake (supported path)

```lua
includes("path/to/cwinrt/xmake.lua")   -- or add cwinrt as a subproject

target("myapp")
    set_kind("binary")
    add_files("src/*.c")
    add_deps("cwinrt")                 -- one line: headers + runtime + all bindings + link libs
```

`add_deps("cwinrt")` is the entire integration surface. The `cwinrt` target re-exports,
as public interface config, the `include/` header path and the platform link recipe, and
pulls in the runtime plus every generated binding. You do **not** repeat `add_includedirs`
or `add_syslinks` — that is the whole point of the target.

```c
#include <cwinrt/init.h>
#include <cwinrt/Windows.Globalization.h>

int main(void) {
    cwinrt_init(RO_INIT_MULTITHREADED);
    WGL_Calendar *cal = NULL;
    wgl_calendar_new(&cal);
    /* ... */
    cwinrt_uninit();
}
```

`tests/conform/consume_cwinrt.c` is exactly this, wired with nothing but
`add_deps("cwinrt")`, and runs as the `consume_cwinrt` CI gate — proof the surface works.

### Trimming the binding set

`cwinrt` links `cwinrt-bindings-all` (every namespace). Because each binding is a separate
static-archive member, the linker only pulls the namespaces you reference, so an unused
namespace costs nothing in the final binary. If you want to bound *compile* time instead,
depend on `cwinrt-rt` directly and add only the impl files you need:

```lua
target("myapp")
    set_kind("binary")
    add_files("src/*.c", "path/to/cwinrt/include/cwinrt/impl/Windows.Globalization.impl.c")
    add_deps("cwinrt-rt")
    add_includedirs("path/to/cwinrt/include")
    -- then the link recipe below, by hand
```

## Other toolchains (MSVC / mingw direct)

No CMake or pkg-config is shipped — cwinrt's consumers build with xmake. If you drive a
compiler directly, the two facts you need are the include path and the **link recipe**:

| toolchain | link libraries |
| --- | --- |
| MSVC (`cl`/`link`) | `runtimeobject.lib ole32.lib oleaut32.lib` (the SDK supplies `uuid`/`IInspectable` automatically) |
| mingw / llvm-mingw (clang or gcc) | `-lwinstorecompat -luuid -lruntimeobject -lole32 -loleaut32` |

The mingw recipe has one non-obvious trap: the COM/WinRT IID GUID symbols are split across
libs. `IID_IUnknown` is in `libuuid.a`, but `IID_IInspectable` and `IID_IActivationFactory`
live in **`libwinstorecompat.a`** — omit `-lwinstorecompat` and you get undefined-symbol
link errors. This recipe is verified by `scripts/mingw_link.sh`, which links the entire
generated surface for `x86_64`/`aarch64`/`i686`/`armv7` (the `mingw-link` CI job runs it).

Minimum compiler flags: `-std=c17` (or `/std:c17`), `-I <cwinrt>/include`. On MSVC, define
`UNICODE`/`_UNICODE`. The generated headers and impl compile clean under MSVC `/W4` and
clang/gcc `-Wall -Wextra -Werror`.

## SDK version

Headers are generated from the Windows SDK Union metadata pinned in `sdk.version`
(currently `10.0.26100.0`). The committed headers under `include/cwinrt` are the
baseline ABI; regenerating against the same SDK is byte-stable. See
[GENERATION.md](GENERATION.md) to regenerate against a different SDK.
