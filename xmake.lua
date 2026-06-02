set_project("cwinrt")
set_version("0.1.0")
set_languages("c17")
set_warnings("all", "extra")
-- Without this the "release" build carried no /O2, leaving the winmd parser /
-- sig decoder unoptimized (full regen ~350s). Optimize for speed across targets.
set_optimize("fastest")

if is_plat("windows") then
    add_defines("_CRT_SECURE_NO_WARNINGS", "UNICODE", "_UNICODE")
end

if is_plat("mingw") then
    add_cxflags("-Wno-logical-op-parentheses")
elseif is_plat("windows") then
    add_cxflags("/wd4996")
end

-- Emit each function/datum into its own section so the linker (--gc-sections /
-- /OPT:REF) can drop unused wrappers at function granularity. Without it,
-- referencing one function from a namespace pulls that impl TU's entire wrapper
-- set into the binary (~46KB+ of dead code per consumer). File-scope: inherited
-- by every target defined below.
if is_plat("mingw") then
    add_cflags("-ffunction-sections", "-fdata-sections")
elseif is_plat("windows") then
    add_cflags("/Gy", "/Gw")
end

includes("thirdparty/coetua/xmake_cwinrt.lua")

add_includedirs("include")
add_includedirs("gen")

-- Runtime (MSVC + MinGW on Windows)
target("cwinrt-rt")
    set_kind("static")
    add_files("rt/*.c")
    add_headerfiles("include/cwinrt/bootstrap.h")
    add_headerfiles("include/cwinrt/*.h")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

-- Code generator (pure C ECMA-335 winmd reader)
target("cwinrt-gen")
    set_kind("binary")
    add_files("gen/*.c")
    add_deps("coetua")
    add_includedirs("thirdparty/coetua/src")
    if is_plat("windows") then
        -- Deep winmd signatures (e.g. Windows.Security.Isolation) need >1MB default stack.
        add_ldflags("/STACK:8388608", {tools = {"link"}})
    elseif is_plat("mingw") then
        add_ldflags("-Wl,--stack,8388608", {tools = {"link"}})
    end
    if not is_plat("windows", "mingw", "linux", "macosx") then
        set_enabled(false)
    end

-- Smoke test (consumes rt + generated headers)
target("test_smoke")
    set_kind("binary")
    add_files("tests/test_smoke.c")
    add_deps("cwinrt-rt")
    add_includedirs("include")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("test_abi")
    set_kind("binary")
    add_files("tests/abi/test_abi.c")
    add_deps("cwinrt-rt")
    add_includedirs("include")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("cwinrt-bindings-composition")
    set_kind("static")
    add_files("include/cwinrt/impl/Windows.UI.Composition.impl.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("cwinrt-bindings-foundation")
    set_kind("static")
    add_files("include/cwinrt/impl/Windows.Foundation.impl.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("cwinrt-bindings-interactions")
    set_kind("static")
    add_files("include/cwinrt/impl/Windows.UI.Composition.Interactions.impl.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("cwinrt-bindings-viewmanagement")
    set_kind("static")
    add_files("include/cwinrt/impl/Windows.UI.ViewManagement.impl.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("cwinrt-bindings-numberformatting")
    set_kind("static")
    add_files("include/cwinrt/impl/Windows.Globalization.NumberFormatting.impl.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("cwinrt-bindings-input")
    set_kind("static")
    add_files("include/cwinrt/impl/Windows.UI.Input.impl.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

-- All generated impl units (present after xmake gen-all-impl)
target("cwinrt-bindings-all")
    set_kind("static")
    add_files("include/cwinrt/impl/*.impl.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

-- Public integration surface. Downstream projects add ONE line, `add_deps("cwinrt")`,
-- and inherit the header path, the runtime + every generated binding, AND the full
-- platform link recipe -- no add_includedirs/add_syslinks repetition. The {public}
-- config is what propagates to dependents (xmake interface deps). See docs/INTEGRATION.md.
target("cwinrt")
    set_kind("phony")
    add_deps("cwinrt-rt", "cwinrt-bindings-all")
    add_includedirs("include", {public = true})
    -- MSVC pulls uuid / IInspectable from its SDK automatically; mingw does not.
    -- IID_IUnknown comes from uuid; IID_IInspectable / IID_IActivationFactory are
    -- defined by rt/cwinrt_guids.c. We must NOT link winstorecompat for those two
    -- GUIDs: it also replaces desktop Win32 APIs (GetStartupInfo, CreateFileW, ...)
    -- with abort() stubs, which crashes the CRT before main in any desktop app.
    if is_plat("mingw") then
        add_syslinks("uuid", "runtimeobject", "ole32", "oleaut32", {public = true})
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32", {public = true})
    end

-- 4b integration gate: a consumer wired with ONLY `add_deps("cwinrt")` (no include
-- dir, no syslinks). If it builds + runs, the public surface above is intact.
target("consume_cwinrt")
    set_kind("binary")
    add_files("tests/conform/consume_cwinrt.c")
    add_deps("cwinrt")

-- link_all: link every impl TU + a stub main into one binary so the linker
-- surfaces cross-TU duplicate symbols (LNK2005) the per-file compile gate cannot.
-- Impl objects are linked directly, not via an archive.
target("link_all")
    set_kind("binary")
    add_files("include/cwinrt/impl/*.impl.c")
    add_files("tests/conform/link_all_main.c")
    add_includedirs("include")
    add_deps("cwinrt-rt")
    if is_plat("windows", "mingw") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

-- Real-hardware end-to-end: activation, cwinrt_query cast, and a value-type
-- round-trip down a deep slot chain (validates vtable slots on hardware).
target("e2e_composition")
    set_kind("binary")
    add_files("tests/conform/e2e_composition.c")
    add_deps("cwinrt")

-- Headless variant (no interactive desktop required), so it runs in any
-- session/CI. Same purpose: validate vtable slots + the cast/IID on hardware.
target("e2e_headless")
    set_kind("binary")
    add_files("tests/conform/e2e_headless.c")
    add_deps("cwinrt")

-- Headless: the delegate shim + async await actually fire.
target("e2e_async")
    set_kind("binary")
    add_files("tests/conform/e2e_async.c")
    add_deps("cwinrt")

target("e2e_piid")
    set_kind("binary")
    add_files("tests/conform/e2e_piid.c")
    add_deps("cwinrt")

target("e2e_box")
    set_kind("binary")
    add_files("tests/conform/e2e_box.c")
    add_deps("cwinrt")

target("e2e_async_progress")
    set_kind("binary")
    add_files("tests/conform/e2e_async_progress.c")
    add_deps("cwinrt")

target("test_conform")
    set_kind("binary")
    add_files("tests/conform/test_conform.c")
    add_deps("cwinrt-rt")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32", "dcomp")
    else
        set_enabled(false)
    end

target("test_header_compile")
    set_kind("binary")
    add_files("tests/conform/test_header_compile.c")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    end

target("test_header_compile_smoke")
    set_kind("binary")
    add_files("tests/conform/header_shards/compile_smoke.c")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    end

target("test_conform_factory")
    set_kind("binary")
    add_files("tests/conform/test_factory.c")
    add_deps("cwinrt-rt")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("test_conform_dispatch")
    set_kind("binary")
    add_files("tests/conform/test_dispatch.c")
    add_deps("cwinrt-rt", "cwinrt-bindings-foundation")
    add_links("cwinrt-bindings-foundation")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("test_conform_dispatch_composition_abi")
    set_kind("binary")
    add_files("tests/conform/test_dispatch_composition_abi.c")
    add_deps("cwinrt-rt", "cwinrt-bindings-composition")
    add_links("cwinrt-bindings-composition")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("test_conform_dispatch_composition_slots")
    set_kind("binary")
    add_files("tests/conform/test_dispatch_composition_slots.c")
    add_deps("cwinrt-rt", "cwinrt-bindings-composition")
    add_links("cwinrt-bindings-composition")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("test_conform_dispatch_compositor")
    set_kind("binary")
    add_files("tests/conform/test_dispatch_compositor.c")
    add_deps("cwinrt-rt", "cwinrt-bindings-composition")
    add_links("cwinrt-bindings-composition")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
        add_ldflags("/MANIFEST:EMBED", {force = true})
        add_ldflags("/MANIFESTINPUT:" .. path.join(os.projectdir(), "tests/conform/test_compositor.app.manifest"), {force = true})
    end

target("test_diag_activate")
    set_kind("binary")
    add_files("tests/conform/test_diag_activate.c")
    add_deps("cwinrt-rt")
    add_includedirs("include")
    if is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    else
        set_enabled(false)
    end

target("test_conform_async")
    set_kind("binary")
    add_files("tests/conform/test_async.c")
    add_deps("cwinrt-rt")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    end

target("test_conform_type_mapping")
    set_kind("binary")
    add_files("tests/conform/test_type_mapping.c")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    end

target("test_conform_array_mapping")
    set_kind("binary")
    add_files("tests/conform/test_array_mapping.c")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    end

target("test_conform_golden")
    set_kind("binary")
    add_files("tests/conform/golden_guid.cpp")
    add_deps("cwinrt-rt", "cwinrt-bindings-foundation")
    add_links("cwinrt-bindings-foundation")
    add_includedirs("include")
    set_languages("c++17")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32", "oleaut32")
        add_cxflags("/EHsc")
    else
        set_enabled(false)
    end

-- Phony targets (xmake 3.x: custom task() is not registered in this project)
target("gen-headers")
    set_kind("phony")
    on_build(function()
        import("core.project.config")
        local plat = config.plat() or "windows"
        local arch = config.arch() or "x64"
        local mode = config.mode() or "release"
        local gen = path.join(os.projectdir(), "build", plat, arch, mode, "cwinrt-gen.exe")
        if not os.isfile(gen) then
            os.exec("xmake build cwinrt-gen")
        end
        local script = path.join(os.projectdir(), "scripts", "gen_all_windows.ps1")
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. script)
    end)

target("gen-all-impl")
    set_kind("phony")
    on_build(function()
        if not os.isfile(path.join(os.projectdir(), "build", "windows", "x64", "release", "cwinrt-gen.exe")) then
            os.exec("xmake build cwinrt-gen")
        end
        local script = path.join(os.projectdir(), "scripts", "gen_all_windows.ps1")
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. script .. " -Impl")
    end)

target("verify-bindings")
    set_kind("phony")
    on_build(function()
        local script = path.join(os.projectdir(), "scripts", "verify_impl.ps1")
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. script)
    end)

target("header-compile-all")
    set_kind("phony")
    on_build(function()
        local report = path.join(os.projectdir(), "scripts", "header_compile_report.ps1")
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. report)
    end)

target("impl-compile-all")
    set_kind("phony")
    on_build(function()
        local report = path.join(os.projectdir(), "scripts", "impl_compile_report.ps1")
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. report)
    end)

target("test_impl_link_all")
    set_kind("binary")
    add_files("tests/conform/test_impl_link_all.c")
    add_deps("cwinrt-rt", "cwinrt-bindings-all")
    add_links("cwinrt-bindings-all", "cwinrt-rt")
    add_includedirs("include")
    if is_plat("mingw") then
        set_enabled(false)
    elseif is_plat("windows") then
        add_syslinks("runtimeobject", "ole32", "oleaut32")
    else
        set_enabled(false)
    end

-- Build the link binary as a dependency, NOT via a nested `xmake build`: an
-- os.exec("xmake ...") inside on_build spawns a second xmake that blocks on the
-- parent's project lock while the parent blocks on the child -> deadlock (hangs
-- until CI's job timeout). add_deps builds it in-process, no second xmake.
target("impl-link-all")
    set_kind("phony")
    -- test_impl_link_all is disabled on mingw (see above); only depend on it where built.
    if not is_plat("mingw") then
        add_deps("test_impl_link_all")
    end

target("register-conform-sparse")
    set_kind("phony")
    on_build(function()
        local script = path.join(os.projectdir(), "scripts", "register_conform_sparse.ps1")
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. script)
    end)

-- Test binaries are deps (built in-process), not nested `xmake build` calls,
-- which would deadlock on the project lock (see impl-link-all). The ps1 checks
-- run after, in on_build.
target("test-conform-mapping")
    set_kind("phony")
    -- The mapping-test binaries are disabled on mingw (see above); don't depend on
    -- disabled targets there or the build graph reports them as unknown.
    if not is_plat("mingw") then
        add_deps("test_conform_type_mapping", "test_conform_array_mapping")
    end
    on_build(function()
        local naming = path.join(os.projectdir(), "scripts", "conform_check_naming.ps1")
        local types = path.join(os.projectdir(), "scripts", "conform_check_types.ps1")
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. naming)
        os.exec("powershell -NoProfile -ExecutionPolicy Bypass -File " .. types)
    end)

-- Samples: real demos. Each links via the consumable umbrella (add_deps("cwinrt")
-- inherits headers + runtime + all bindings + the base WinRT link recipe) plus the
-- extra system libs its scenario needs (DWM backdrop, Direct3D for capture).
for _, name in ipairs({"mica", "acrylic", "flyout", "capture"}) do
    target("sample_" .. name)
        set_kind("binary")
        add_files("samples/" .. name .. "/main.c")
        add_deps("cwinrt")
        if is_plat("windows", "mingw") then
            add_syslinks("user32", "dwmapi", "d3d11", "dxgi", "dxguid")
        end
end
