#!/bin/sh
# =============================================================================
#  tests/check_exports.sh
# -----------------------------------------------------------------------------
#  Verifies the two things the reconstruction has to get right about the export
#  surface:
#
#    1. src/exports.def and src/aftermath_exports.cpp agree exactly -- no
#       exported name without an implementation, no implementation that is not
#       exported, and no missing or extra symbols.
#    2. The set is the nine names recovered from the image, and it is the set
#       whose alphabetical sort reproduces the export-table indices seen in the
#       listing (DX11 CreateContextHandle 1, DX11 Initialize 2, DX12
#       CreateContextHandle 3, DX12 Initialize 4, ReleaseContextHandle 8).
#
#  Run directly or through `make -f Makefile.host test`.
# =============================================================================

set -u

DEF=src/exports.def
SRC=src/aftermath_exports.cpp
FAILED=0

tmp_def=$(mktemp)
tmp_src=$(mktemp)
tmp_exp=$(mktemp)
trap 'rm -f "$tmp_def" "$tmp_src" "$tmp_exp"' EXIT

# -----------------------------------------------------------------------------
# 1. Names in the .def file.  Drop the LIBRARY/DESCRIPTION/EXPORTS directives.
# -----------------------------------------------------------------------------
grep -vE '^\s*(;|LIBRARY|DESCRIPTION|EXPORTS|$)' "$DEF" \
    | sed 's/;.*//' \
    | awk '{print $1}' \
    | sort -u > "$tmp_def"

# -----------------------------------------------------------------------------
# 2. Names defined in the source: the line after each GFSDK_AFTERMATH_CALL.
# -----------------------------------------------------------------------------
grep -A1 'GFSDK_AFTERMATH_CALL$' "$SRC" \
    | grep -oE 'GFSDK_Aftermath_[A-Za-z0-9_]+\(' \
    | sed 's/($//; s/(//' \
    | sort -u > "$tmp_src"

# -----------------------------------------------------------------------------
# 3. The names the recovery pass says must be there.
# -----------------------------------------------------------------------------
cat > "$tmp_exp" <<'NAMES'
GFSDK_Aftermath_DX11_CreateContextHandle
GFSDK_Aftermath_DX11_Initialize
GFSDK_Aftermath_DX12_CreateContextHandle
GFSDK_Aftermath_DX12_Initialize
GFSDK_Aftermath_GetData
GFSDK_Aftermath_GetDeviceStatus
GFSDK_Aftermath_GetPageFaultInformation
GFSDK_Aftermath_ReleaseContextHandle
GFSDK_Aftermath_SetEventMarker
NAMES

echo "export surface"
echo "  ---------------"

if ! diff -u "$tmp_def" "$tmp_src" > /dev/null; then
    echo "  FAIL  exports.def and aftermath_exports.cpp disagree:"
    diff -u "$tmp_def" "$tmp_src" | sed 's/^/        /'
    FAILED=1
else
    echo "  ok    exports.def matches the source exactly"
fi

if ! diff -u "$tmp_exp" "$tmp_def" > /dev/null; then
    echo "  FAIL  the export set is not the nine recovered names:"
    diff -u "$tmp_exp" "$tmp_def" | sed 's/^/        /'
    FAILED=1
else
    echo "  ok    all nine recovered names present, none extra"
fi

count=$(wc -l < "$tmp_def" | tr -d ' ')
if [ "$count" != "9" ]; then
    echo "  FAIL  expected 9 exports, found $count"
    FAILED=1
else
    echo "  ok    export count is 9"
fi

# -----------------------------------------------------------------------------
# 4. The alphabetical sort must place the five functions whose index is known
#    from the listing at indices 1, 2, 3, 4 and 8.
# -----------------------------------------------------------------------------
check_index()
{
    want=$1
    name=$2
    got=$(grep -n "^$name\$" "$tmp_exp" | cut -d: -f1)
    if [ "$got" != "$want" ]; then
        echo "  FAIL  $name should sort to index $want, got $got"
        FAILED=1
    fi
}

check_index 1 GFSDK_Aftermath_DX11_CreateContextHandle
check_index 2 GFSDK_Aftermath_DX11_Initialize
check_index 3 GFSDK_Aftermath_DX12_CreateContextHandle
check_index 4 GFSDK_Aftermath_DX12_Initialize
check_index 8 GFSDK_Aftermath_ReleaseContextHandle

if [ "$FAILED" = "0" ]; then
    echo "  ok    sorted order reproduces the five indices seen in the listing"
fi

# -----------------------------------------------------------------------------
# 5. Every entry point must be undecorated: the recovered names carry no
#    @-suffix, so the .def has no alias entries.
# -----------------------------------------------------------------------------
if grep -qE '^\s*[A-Za-z_]+\s*=' "$DEF"; then
    echo "  FAIL  exports.def contains an alias (name = other) entry"
    FAILED=1
else
    echo "  ok    no aliased exports"
fi

exit $FAILED
