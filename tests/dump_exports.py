#!/usr/bin/env python3
"""Dump and verify the export directory of a built GFSDK_Aftermath_Lib.x64.dll.

    python3 tests/dump_exports.py <path-to-dll>

Prints the export table and checks it against the nine names/ordinals of the original
binary.  Exits non-zero on any mismatch.  Pure stdlib PE parsing, so it runs on any host.
"""

import os
import struct
import sys

EXPECTED = [
    (1, "GFSDK_Aftermath_DX11_CreateContextHandle"),
    (2, "GFSDK_Aftermath_DX11_Initialize"),
    (3, "GFSDK_Aftermath_DX12_CreateContextHandle"),
    (4, "GFSDK_Aftermath_DX12_Initialize"),
    (5, "GFSDK_Aftermath_GetData"),
    (6, "GFSDK_Aftermath_GetDeviceStatus"),
    (7, "GFSDK_Aftermath_GetPageFaultInformation"),
    (8, "GFSDK_Aftermath_ReleaseContextHandle"),
    (9, "GFSDK_Aftermath_SetEventMarker"),
]

# Addresses of the nine exports in the original binary.  MSVC emits functions in source
# order, so a faithful reconstruction reproduces this *relative* layout; the check below
# is what keeps src/aftermath_api.cpp in the same order as the disassembly.
ORIGINAL_LAYOUT = [
    (9, 0x180004CC0),
    (5, 0x180004E40),
    (6, 0x1800051D0),
    (7, 0x1800053D0),
    (4, 0x180005550),
    (2, 0x180005570),
    (3, 0x180005580),
    (1, 0x180005590),
    (8, 0x1800055A0),
]


def read_exports(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("%s is not a PE image" % path)

    machine, section_count = struct.unpack_from("<HH", data, pe + 4)
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    pe32plus = struct.unpack_from("<H", data, opt)[0] == 0x20B
    export_rva = struct.unpack_from("<I", data, opt + (112 if pe32plus else 96))[0]

    sections = []
    for i in range(section_count):
        _, vsize, vaddr, rawsize, rawptr = struct.unpack_from("<8sIIII", data, opt + opt_size + i * 40)
        sections.append((vaddr, max(vsize, rawsize), rawptr))

    def offset(rva):
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        raise SystemExit("rva 0x%X is not mapped by any section" % rva)

    def cstring(off):
        return data[off:data.index(b"\0", off)].decode()

    if export_rva == 0:
        raise SystemExit("%s has no export directory" % path)

    base = offset(export_rva)
    (_flags, _stamp, _major, _minor, name_rva, ordinal_base, func_count, name_count,
     funcs_rva, names_rva, ords_rva) = struct.unpack_from("<IIHHIIIIIII", data, base)

    functions = [struct.unpack_from("<I", data, offset(funcs_rva) + 4 * i)[0] for i in range(func_count)]
    table = {}
    for i in range(name_count):
        name = cstring(offset(struct.unpack_from("<I", data, offset(names_rva) + 4 * i)[0]))
        index = struct.unpack_from("<H", data, offset(ords_rva) + 2 * i)[0]
        table[index + ordinal_base] = (name, functions[index])

    return {
        "machine": machine,
        "pe32plus": pe32plus,
        "module_name": cstring(offset(name_rva)),
        "ordinal_base": ordinal_base,
        "function_count": func_count,
        "name_count": name_count,
        "exports": table,
    }


def annotate(message):
    """Emit a GitHub Actions error annotation (readable without the log archive)."""
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::error::%s" % message.replace("\n", " "))


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)

    try:
        info = read_exports(sys.argv[1])
    except SystemExit as error:
        annotate("%s: %s" % (sys.argv[1], error))
        raise

    print("machine           : 0x%04X%s" % (info["machine"], "  (x64)" if info["machine"] == 0x8664 else ""))
    print("PE32+             : %s" % info["pe32plus"])
    print("export dll name   : %s" % info["module_name"])
    print("ordinal base      : %d" % info["ordinal_base"])
    print("functions / names : %d / %d" % (info["function_count"], info["name_count"]))
    print()
    print("%-8s %-45s %s" % ("ordinal", "name", "rva"))
    for ordinal in sorted(info["exports"]):
        name, rva = info["exports"][ordinal]
        print("%-8d %-45s 0x%08X" % (ordinal, name, rva))

    print()
    failures = 0
    for ordinal, name in EXPECTED:
        actual = info["exports"].get(ordinal, ("<missing>", 0))[0]
        ok = actual == name
        failures += not ok
        print("  [%s] @%d %s%s" % ("PASS" if ok else "FAIL", ordinal, name,
                                   "" if ok else "   (found %s)" % actual))

    print()
    expected_order = [ordinal for ordinal, _ in ORIGINAL_LAYOUT]
    actual_order = [o for o in sorted(info["exports"], key=lambda o: info["exports"][o][1])
                    if o in expected_order]
    layout_ok = actual_order == expected_order
    failures += not layout_ok
    print("  [%s] function layout matches the original order (@%s)"
          % ("PASS" if layout_ok else "FAIL", ", @".join(str(o) for o in expected_order)))
    if not layout_ok:
        print("         built order: @%s" % ", @".join(str(o) for o in actual_order))

    unexpected = sorted(o for o in info["exports"] if o > len(EXPECTED))
    if unexpected:
        failures += 1
        print("  [FAIL] unexpected exports at ordinals %s" % unexpected)
    if info["machine"] != 0x8664:
        failures += 1
        print("  [FAIL] not an x64 image")

    print()
    print("export table matches the original binary" if failures == 0
          else "%d mismatch(es)" % failures)

    if failures:
        annotate("%s: %d mismatch(es).  table = %s" % (
            sys.argv[1],
            failures,
            " | ".join("@%d %s 0x%X" % (o, info["exports"][o][0], info["exports"][o][1])
                       for o in sorted(info["exports"]))))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
