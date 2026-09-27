#!/bin/sh
# =============================================================================
#  tests/check_artifacts.sh
# -----------------------------------------------------------------------------
#  Scans the reconstructed sources for decompiler identifiers.
#
#  The reconstruction is allowed to quote an address or an IDA-generated symbol
#  in a COMMENT -- that is how each global and each NVAPI id is tied back to the
#  image, and losing it would lose the provenance of the work.  What must not
#  survive into compiled code is the identifiers themselves:
#
#      BYTEn / LOBYTE / HIBYTE / WORDn / LODWORD / HIWORD   IDA type helpers
#      _QWORD / _DWORD / __int64 / __fastcall               IDA type spellings
#      DAT_x / FUN_x / data_x / unk_x                       IDA placeholder names
#      sub_XXXXXX / qword_XXXXXX / dword_XXXXXX / byte_XXXXXX
#
#  include/defs.h is excluded: it exists precisely to provide these spellings,
#  behind AFTERMATH_ALLOW_IDA_TYPES, for anyone who wants to paste a raw
#  decompiler listing next to the reconstruction.
#
#  Run directly or through `make -f Makefile.host test`.
# =============================================================================

set -u

FILES="
src/aftermath_core.cpp
src/aftermath_exports.cpp
src/aftermath_globals.cpp
src/aftermath_internal.h
src/dllmain.cpp
src/exports.def
src/nvapi/aftermath_driver.cpp
src/nvapi/aftermath_driver.h
src/nvapi/nvapi_ids.h
src/nvapi/nvapi_loader.cpp
src/nvapi/nvapi_loader.h
include/GFSDK_Aftermath.h
include/GFSDK_Aftermath_DX11.h
include/GFSDK_Aftermath_DX12.h
include/GFSDK_Aftermath_Defines.h
tests/host_smoke_test.cpp
"

PATTERN='\b(BYTE[0-9]+|LOBYTE|HIBYTE|WORD[0-9]+|LODWORD|HIWORD|_QWORD|_DWORD|__int64|__fastcall|DAT_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|data_[0-9a-fA-F]+|unk_[0-9a-fA-F]+|sub_[0-9A-Fa-f]{6}|qword_[0-9A-Fa-f]{4,}|dword_[0-9A-Fa-f]{4,}|byte_[0-9A-Fa-f]{4,})\b'

FAILED=0
echo "decompiler artefacts"
echo "  --------------------"

for f in $FILES; do
    if [ ! -f "$f" ]; then
        echo "  FAIL  missing file $f"
        FAILED=1
        continue
    fi

    # Strip line comments and block-comment bodies before scanning, so the
    # provenance notes in the headers do not trip the check.
    hits=$(sed -e 's://.*::' -e 's:/\*.*\*/::' "$f" | grep -nE "$PATTERN" || true)

    if [ -n "$hits" ]; then
        echo "  FAIL  $f contains decompiler identifiers in code:"
        echo "$hits" | sed 's/^/        /'
        FAILED=1
    fi
done

if [ "$FAILED" = "0" ]; then
    echo "  ok    no decompiler identifiers outside comments"
fi

# -----------------------------------------------------------------------------
# The reverse check: the shim that provides those spellings must exist, and it
# must be opt-in, so that the public headers never drag it in.
# -----------------------------------------------------------------------------
if [ ! -f include/defs.h ]; then
    echo "  FAIL  include/defs.h (the opt-in shim) is missing"
    FAILED=1
elif ! grep -q "AFTERMATH_ALLOW_IDA_TYPES" include/defs.h; then
    echo "  FAIL  include/defs.h is not gated on AFTERMATH_ALLOW_IDA_TYPES"
    FAILED=1
else
    if grep -qE '#\s*include\s*[<"]defs\.h[>"]' include/GFSDK_Aftermath.h \
                                             include/GFSDK_Aftermath_Defines.h \
                                             include/GFSDK_Aftermath_DX11.h \
                                             include/GFSDK_Aftermath_DX12.h; then
        echo "  FAIL  a public header includes defs.h"
        FAILED=1
    else
        echo "  ok    defs.h is opt-in and not reachable from the public headers"
    fi
fi

exit $FAILED
