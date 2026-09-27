#!/usr/bin/env bash
#
# syntax_check.sh -- type check every translation unit of the reconstruction on a host
# that has no Windows SDK.
#
# This is NOT a substitute for building with MSVC v143: it parses the sources against the
# stub Win32 headers in tests/win32-shim (declarations copied from the documented Win32
# signatures) and stops at -fsyntax-only, so it catches syntax errors, type errors,
# missing declarations and bad casts, but it says nothing about linking, ordinals or
# actual Windows behaviour.
#
# Usage:  tests/syntax_check.sh [compiler]     (default: g++)

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${1:-g++}"

SOURCES=(
    src/aftermath_api.cpp
    src/dllmain.cpp
    src/loader/nv_driver_store.cpp
    src/loader/nv_module_loader.cpp
    src/loader/nv_path_utils.cpp
    src/loader/nv_strsafe.cpp
    src/loader/nv_system_api.cpp
    src/nvapi/nvapi_bridge.cpp
)

FLAGS=(
    -std=c++14
    -fsyntax-only
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unknown-pragmas
    -Wno-cast-function-type
    -D_WIN64
    -DWIN32_LEAN_AND_MEAN
    -DNOMINMAX
    -DGFSDK_AFTERMATH_BUILD_DLL
    -include "$ROOT/tests/win32-shim/msvc_prelude.h"
    -I "$ROOT/tests/win32-shim"
    -I "$ROOT/include"
    -I "$ROOT/src"
)

failures=0
for source in "${SOURCES[@]}"; do
    printf '  %-40s' "$source"
    if "$CXX" "${FLAGS[@]}" "$ROOT/$source"; then
        echo "ok"
    else
        echo "FAILED"
        failures=$((failures + 1))
    fi
done

# The public header must also be valid C and self contained.
printf '  %-40s' "include/GFSDK_Aftermath.h (C89)"
if echo '#include "GFSDK_Aftermath.h"
int main(void) { return (int)GFSDK_Aftermath_Version_API; }' | \
   "${CXX%%+*}cc" -x c -std=c89 -pedantic -Wall -fsyntax-only \
       -include "$ROOT/tests/win32-shim/msvc_prelude.h" \
       -I "$ROOT/include" - 2>/dev/null; then
    echo "ok"
else
    echo "FAILED"
    failures=$((failures + 1))
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "All translation units parse cleanly."
else
    echo "$failures translation unit(s) failed."
fi
exit "$failures"
