set_project("coetua")
set_languages("c17")
set_warnings("all", "extra")

-- Common warning policy
if is_plat("windows") then
    -- MSVC CRT "unsafe" standard-library warnings (C4996)
    add_defines("_CRT_SECURE_NO_WARNINGS")
end

if is_plat("mingw") then
    add_cxflags("-Wno-logical-op-parentheses")
elseif is_plat("windows") then
    add_cxflags("/wd4996")
end

-- 64-bit file offsets (fseeko/ftello)
if not is_plat("windows") or is_plat("mingw") then
    add_defines("_FILE_OFFSET_BITS=64")
end

-- ASan mode:  xmake f --asan=y -c && xmake build
option("asan")
    set_default(false)
    set_showmenu(true)
    set_description("Enable AddressSanitizer")
option_end()

if has_config("asan") then
    set_policy("build.sanitizer.address", true)
    add_defines("COETUA_ASAN")
end

add_includedirs("src")

-- Collect all .c files in src/ as individual static lib units
local units = os.files("src/*.c")
for i, f in ipairs(units) do
    units[i] = path.basename(f)
    target(units[i])
        set_kind("static")
        add_files(f)
end

-- Umbrella static library
target("coetua")
    add_headerfiles("src/*.h")
    set_kind("static")
    if #units > 0 then
        add_deps(units)
    end

-- Tests
for _, tp in ipairs(os.files("tests/*.c")) do
    local test = path.basename(tp)
    target(test)
        add_deps("coetua")
        add_tests(test)
        add_files(tp)
end
