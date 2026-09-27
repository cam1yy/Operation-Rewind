#!/usr/bin/env bash
#
# build_mingw.sh -- cross build the DLL on a non Windows host and verify its export table.
#
# This is a *verification* build, not the shipping one: it uses the mingw-w64 headers and
# the MinGW CRT instead of the Windows SDK and MSVC.  What it proves is nevertheless the
# interesting part:
#
#   * every translation unit compiles against the real windows.h / setupapi.h /
#     devpropdef.h / unknwn.h / winsvc.h / winreg.h for an x86_64 Windows target;
#   * the whole thing links into a real x64 PE;
#   * the export directory contains exactly the nine names on the nine ordinals of the
#     original GFSDK_Aftermath_Lib.x64.dll (checked by tests/dump_exports.py).
#
# Usage:
#   tests/build_mingw.sh                      # uses x86_64-w64-mingw32-g++ if present
#   CXX="zig c++ -target x86_64-windows-gnu" tests/build_mingw.sh
#   CXX="clang++ --target=x86_64-windows-gnu" tests/build_mingw.sh

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$ROOT/tests/.syntax-check/mingw}"

if [ -z "${CXX:-}" ]; then
    if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
        CXX="x86_64-w64-mingw32-g++"
    elif command -v zig >/dev/null 2>&1; then
        CXX="zig c++ -target x86_64-windows-gnu"
    else
        echo "no cross compiler found; set CXX (see the header of this script)" >&2
        exit 127
    fi
fi

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
    -Wall
    -Wextra
    -Wno-unused-parameter
    -U_WIN32_WINNT
    -UWINVER
    -D_WIN32_WINNT=0x0601
    -DWINVER=0x0601
    -DWIN32_LEAN_AND_MEAN
    -DNOMINMAX
    -DGFSDK_AFTERMATH_BUILD_DLL
    -D_CRT_SECURE_NO_WARNINGS
    -I "$ROOT/include"
    -I "$ROOT/src"
)

mkdir -p "$OUT"
rm -f "$OUT"/*.o "$OUT"/*.dll

echo "compiler: $CXX"
echo
objects=()
failures=0
for source in "${SOURCES[@]}"; do
    object="$OUT/$(echo "$source" | tr '/' '_').o"
    printf '  %-40s' "$source"
    if $CXX "${FLAGS[@]}" -c "$ROOT/$source" -o "$object"; then
        echo "ok"
        objects+=("$object")
    else
        echo "FAILED"
        failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ]; then
    echo
    echo "$failures translation unit(s) failed to compile."
    exit 1
fi

DLL="$OUT/GFSDK_Aftermath_Lib.x64.dll"
echo
printf '  %-40s' "link $(basename "$DLL")"
if $CXX -shared -o "$DLL" "${objects[@]}" "$ROOT/exports.def" -lkernel32 >"$OUT/link.log" 2>&1; then
    echo "ok"
else
    echo "FAILED (see $OUT/link.log)"
    exit 1
fi

echo
python3 "$ROOT/tests/dump_exports.py" "$DLL"
