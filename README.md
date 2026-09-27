# Operation Rewind — `GFSDK_Aftermath_Lib.x64.dll`

A compilable Visual Studio 2022 (v143, x64) project that rebuilds NVIDIA's Nsight
Aftermath shim from decompiler output (Hex-Rays + Ghidra + Binary Ninja). The three
listings were reconciled function by function, stripped of decompiler artefacts, and
rewritten as ordinary C++ / Win32.

```
GFSDK_Aftermath_Lib.sln          Visual Studio 2022 solution (Debug|x64, Release|x64)
GFSDK_Aftermath_Lib.vcxproj      v143, /MT, no DirectX or NVAPI dependency
CMakeLists.txt                   same build from the command line
exports.def                      the nine exports, with the original ordinals

include/GFSDK_Aftermath.h        reconstructed public API
src/aftermath_api.cpp            the nine entry points + Initialize / CreateContextHandle
src/aftermath_internal.h         context handle layout, ApiType, shared declarations
src/dllmain.cpp                  DllMain
src/nvapi/                       late bound NVAPI: 13 entry points behind nvapi_QueryInterface
src/loader/                      NVIDIA's hardened "load nv*.dll by trusted absolute path"
src/compat/com_min.h             the two D3D v-table calls the DLL makes, without d3d11/12.h
src/compat/ida_defs.h            IDA <defs.h> replacements for MSVC (for further RE work)

docs/RECONSTRUCTION.md           method, type recovery, quirks kept, deviations, open items
docs/SYMBOL_MAP.md               every sub_XXXXXXXX and global -> name -> file
tests/smoke_test.cpp             Windows: verifies the export table and pre-init behaviour
tests/syntax_check.sh            non-Windows: type checks every TU against stub Win32 headers
```

## What the library does

It is a shim, not an implementation. Each export validates its arguments, consults four
globals of state, and forwards the call to a private NVAPI entry point that it resolves at
run time from `nvapi64.dll`:

```
 GFSDK_Aftermath_DX12_Initialize
   -> NvAPI_SYS_GetDriverAndBranchVersion (0x2926AAAD)   driver must be >= 387.84
   -> ID3D12Device::QueryInterface(IID_ID3D12DebugDevice) must FAIL
   -> nvapi_QueryInterface(0xF1EA1980)                   create the device handle
   -> nvapi_QueryInterface(0xDBE53CB2)                   enable the requested features
```

`nvapi64.dll` itself is loaded by the bundled hardened loader, which resolves the module
to an absolute path through the display driver's own registry/DriverStore records and
refuses to load it from anywhere outside `%WINDIR%` / `%ProgramFiles%`.

## Exports

| Ordinal | Name |
|---|---|
| 1 | `GFSDK_Aftermath_DX11_CreateContextHandle` |
| 2 | `GFSDK_Aftermath_DX11_Initialize` |
| 3 | `GFSDK_Aftermath_DX12_CreateContextHandle` |
| 4 | `GFSDK_Aftermath_DX12_Initialize` |
| 5 | `GFSDK_Aftermath_GetData` |
| 6 | `GFSDK_Aftermath_GetDeviceStatus` |
| 7 | `GFSDK_Aftermath_GetPageFaultInformation` |
| 8 | `GFSDK_Aftermath_ReleaseContextHandle` |
| 9 | `GFSDK_Aftermath_SetEventMarker` |

Ordinals 1–4 and 8 are the ones visible in the Ghidra listing; 5, 6, 7 and 9 follow from
the alphabetical numbering the linker applies — see `docs/RECONSTRUCTION.md §4`.

## Building

Visual Studio 2022 with the Desktop C++ workload (v143 toolset + Windows 10/11 SDK):

```
msbuild GFSDK_Aftermath_Lib.sln /p:Configuration=Release /p:Platform=x64
```

or with CMake:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `GFSDK_Aftermath_Lib.x64.dll`. The only link-time dependency is `kernel32.lib` —
SetupAPI, Advapi32 and Shell32 are resolved with `GetProcAddress`, exactly like the
original, and the CRT is linked statically (`/MT`) as in the shipping binary.

## Checking the result

```
:: Windows, after building
cl /EHsc /W4 /Iinclude tests\smoke_test.cpp
smoke_test.exe build\x64\Release\GFSDK_Aftermath_Lib.x64.dll
```

verifies that all nine exports resolve, that each sits on the expected ordinal, and that
every entry point reports `FAIL_NotInitialized` before initialisation.

On a machine without a Windows SDK:

```
tests/syntax_check.sh        # g++ -fsyntax-only against tests/win32-shim
```

## Status

* All 8 translation units and the public header compile warning-free under
  `g++ -std=c++14 -Wall -Wextra` against the stub Win32 headers, and the public header is
  additionally valid C89.
* The MSVC build itself has **not** been exercised here (no Windows SDK in this
  environment) — that is the one step left to run on a Windows machine.
* Three `.rdata` constants (two GUIDs and a `DEVPROPKEY`) were inferred from their use
  because the supplied listings covered `.text` only; they are listed with their evidence
  in `docs/RECONSTRUCTION.md §7`.

## Scope note

This is a clean-room-style rebuild of an interoperability shim for analysis and
compatibility work. The Nsight Aftermath SDK and its DLL are NVIDIA property; nothing here
redistributes NVIDIA binaries or driver code.
