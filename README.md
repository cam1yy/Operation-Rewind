# Operation-Rewind

Source reconstruction of `GFSDK_Aftermath_Lib.x64.dll` (NVIDIA Nsight Aftermath,
library revision 1.3 / API version `0x13`), rebuilt as a Visual Studio 2022 x64
DLL project.

The decompiled listing has been reconciled and split into clean `.h` / `.cpp`
files: no `BYTEn`/`LOBYTE` macros, no `DAT_`/`FUN_`/`data_` placeholders, no
`__fastcall` noise and no `_QWORD`/`__int64` aliases remain in the build. The
addresses and symbol names of the original are kept, but only in comments, where
they serve as the provenance for each recovered global and id.

## Layout

```
include/
  GFSDK_Aftermath.h            public interface, nine entry points
  GFSDK_Aftermath_Defines.h    result codes, feature flags, status enums, handle type
  GFSDK_Aftermath_DX11.h       D3D11 declarations
  GFSDK_Aftermath_DX12.h       D3D12 declarations
  defs.h                       decompiler type shims (opt-in, see below)

src/
  aftermath_internal.h         internal declarations, context handle layout
  aftermath_globals.cpp        the five mutable globals
  aftermath_core.cpp           the real implementations
  aftermath_exports.cpp        the nine exported entry points
  dllmain.cpp                  module lifetime + CRT scaffolding
  exports.def                  the export table
  nvapi/
    nvapi_ids.h                recovered NVAPI interface ids
    nvapi_loader.h/.cpp        nvapi64.dll discovery and interface cache
    aftermath_driver.h/.cpp    typed wrappers over the driver interfaces

build/
  GFSDK_Aftermath.sln          Visual Studio 2022 solution
  GFSDK_Aftermath.vcxproj      v143, x64, /MT static CRT, exports.def

tests/
  host_smoke_test.cpp          161-check behavioural test (host buildable)
  public_headers_c_compat.c    the public headers must also compile as C99
  check_exports.sh             exports.def vs source, and the recovered order
  check_artifacts.sh           no decompiler identifier outside a comment
  windows_shim/windows.h       tiny windows.h stand-in for the `wincheck` target

docs/
  ANALYSIS_NOTES.md            provenance for every recovered value + open questions
```

## Building

**Windows / MSVC x64 (the real target)**

```
msbuild build\GFSDK_Aftermath.sln /p:Configuration=Release /p:Platform=x64
```

Output: `bin\x64\Release\GFSDK_Aftermath_Lib.x64.dll`. Verify the export table
with `dumpbin /exports`.

**Any host (verification only)** — compiles every translation unit with
`-Wall -Wextra -Werror` and runs four gates:

```
make -f Makefile.host test
```

| Target | What it proves |
|---|---|
| `test` (default) | 161 behavioural checks against stubbed driver entry points |
| `c-compat` | the public headers compile as C99 *and* C++17 |
| `wincheck` | the `#ifndef AFTERMATH_TEST_BUILD` branches — the loader and `DllMain`, i.e. what MSVC actually compiles — type-check against `tests/windows_shim/windows.h` |
| `artifacts` | no decompiler identifier survives outside a comment |
| `exports` | `exports.def` matches the source exactly, and the nine names sort into the export-table indices seen in the listing |

This cannot produce the DLL — that needs MSVC, the Windows loader and
`nvapi64.dll`. It validates the reconstructed *logic*, which is the part worth
testing.

## Notes on the reconstruction

Every non-obvious constant carries a provenance tag in the source:

* `[D]` — recovered from the disassembly
* `[H]` — taken from the published Aftermath header
* `[I]` — inferred; listed under "Open questions" in `docs/ANALYSIS_NOTES.md`

**`docs/ANALYSIS_NOTES.md` is the important file.** It records the recovered
result-code table, the state layout, the device-status translation, the
`0xBAD0`-prefix rule that distinguishes a public result from a driver-internal
status, the ten corrections the listing forced on the first reconstruction, and
the remaining open questions with the exact location of each in the source.

Three things worth knowing before reading the code:

* `Initialize()` contains a latch that makes a *second* call report success
  without doing anything after a first call fails. It is not a bug in this
  reconstruction — it is what the binary does, and the test asserts it. See
  `docs/ANALYSIS_NOTES.md` §4.
* The recovered `DllMain` is literally `return TRUE;`. There is no teardown
  routine and no driver shutdown call anywhere in the image; the
  `aftermath::Shutdown()` in this tree exists for the host test.

* The `_0` suffixes on several decompiled function names
  (`GFSDK_Aftermath_GetData_0`) are **analysis-database artifacts**, not export
  names. They are stripped everywhere; `src/exports.def` is authoritative.
* This revision is **older than the publicly available SDK headers**. Its
  `GFSDK_Aftermath_Device_Status` is 0-based with only four documented values,
  and its `GFSDK_Aftermath_Version_API` is `0x13`, not `535`. Copying the modern
  enumerations in would be wrong — see `docs/ANALYSIS_NOTES.md` §3.

`include/defs.h` supplies the standard replacements for the decompiler's type
vocabulary so that any function still in its raw listing form can be compiled
next to the cleaned ones for side-by-side diffing. It is guarded by
`AFTERMATH_ALLOW_IDA_TYPES` and is not included by the public headers.
