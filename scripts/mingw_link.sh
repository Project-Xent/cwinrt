#!/usr/bin/env bash
# Prove the ENTIRE generated surface (rt + every impl) links under
# llvm-mingw for a given target triple, locking the non-MSVC link-library recipe.
# MSVC pulls these from uuid.lib/runtimeobject automatically; mingw does not (see
# the recipe below). IID_IInspectable / IID_IActivationFactory come from rt's
# cwinrt_guids.c, compiled in below -- NOT winstorecompat, whose abort() API stubs
# crash desktop apps before main (see rt/cwinrt_guids.c).
#
#   scripts/mingw_link.sh <arch> [--run]
#     <arch>  one of: x86_64 aarch64 i686 armv7
#     --run   execute the linked binary afterwards (host-arch only; a cross binary
#             cannot run on the x64 CI runner, so callers pass --run for x86_64 only)
#
# Toolchain: <arch>-w64-mingw32-clang must be on PATH, or set LLVM_MINGW_BIN to its
# bin dir. Tested with llvm-mingw 20260324 (clang 22), ucrt, all four triples.
set -euo pipefail

arch="${1:?usage: mingw_link.sh <arch> [--run]}"
run="${2:-}"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cc="${arch}-w64-mingw32-clang"
if [ -n "${LLVM_MINGW_BIN:-}" ]; then cc="${LLVM_MINGW_BIN}/${cc}"; fi
command -v "$cc" >/dev/null 2>&1 || { echo "FAIL: $cc not found on PATH"; exit 1; }

obj="$(mktemp -d)"
trap 'rm -rf "$obj"' EXIT

export CWINRT_CC="$cc"
export CWINRT_CFLAGS="-std=c17 -I $root/include -Wall -Wextra -c"
export CWINRT_OBJ="$obj"
compile_one() {  # source basenames are unique across rt/ + impl/ + the test, so no .o collision
  $CWINRT_CC $CWINRT_CFLAGS -o "$CWINRT_OBJ/$(basename "$1").o" "$1"
}
export -f compile_one

n_impl="$(ls "$root"/include/cwinrt/impl/*.impl.c | wc -l)"
echo "== $arch: compiling rt + $n_impl impl + e2e_headless"
{ ls "$root"/rt/*.c "$root"/include/cwinrt/impl/*.impl.c; echo "$root/tests/conform/e2e_headless.c"; } \
  | xargs -P "$(nproc 2>/dev/null || echo 4)" -I{} bash -c 'compile_one "$@"' _ {}

# The recipe. uuid carries IID_IUnknown; IID_IInspectable / IID_IActivationFactory
# come from the compiled rt/cwinrt_guids.c (above), so winstorecompat is neither
# needed nor wanted (its abort() Win32 stubs crash desktop apps -- see that file).
libs=(-luuid -lruntimeobject -lole32 -loleaut32)
exe="$obj/e2e_headless_${arch}.exe"
echo "== $arch: linking -> $(basename "$exe")"
# 351 object paths overflow the Windows command line, so pass them via a clang
# @response file. clang reads rsp paths literally (no msys /c/ -> C:\ translation),
# so cd into the obj dir and list bare relative basenames.
( cd "$obj" && printf '%s\n' *.o > objs.rsp \
  && "$cc" -static -o "e2e_headless_${arch}.exe" @objs.rsp "${libs[@]}" )
echo "PASS: $arch full-surface link clean (recipe: ${libs[*]})"

if [ "$run" = "--run" ]; then
  echo "== $arch: running e2e_headless"
  "$exe"
  echo "PASS: $arch e2e_headless ran"
fi
