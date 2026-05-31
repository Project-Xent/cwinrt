-- Coetua subset for cwinrt-gen (no coetua test targets).
set_languages("c17")
set_warnings("all", "extra")

if is_plat("windows") then
    add_defines("_CRT_SECURE_NO_WARNINGS")
end

if is_plat("mingw") then
    add_cxflags("-Wno-logical-op-parentheses")
elseif is_plat("windows") then
    add_cxflags("/wd4996")
end

if not is_plat("windows") or is_plat("mingw") then
    add_defines("_FILE_OFFSET_BITS=64")
end

add_includedirs("thirdparty/coetua/src")

local coetua_src = path.join(os.scriptdir(), "src")
local units = {}
for _, f in ipairs(os.files(path.join(coetua_src, "*.c"))) do
    local name = path.basename(f)
    units[#units + 1] = name
    target(name)
        set_kind("static")
        add_files(f)
        add_includedirs(coetua_src)
end

target("coetua")
    set_kind("static")
    add_headerfiles(path.join(coetua_src, "*.h"))
    if #units > 0 then
        add_deps(units)
    end
