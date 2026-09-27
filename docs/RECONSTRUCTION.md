# Reconstruction notes — `GFSDK_Aftermath_Lib.x64.dll`

This document records *how* the sources in this repository were derived from the three
decompiler listings, what is evidence and what is inference, and every place where the
reconstruction deliberately differs from the machine code.

---

## 1. What the DLL actually is

`GFSDK_Aftermath_Lib.x64.dll` is a thin shim. It contains no GPU logic of its own: the
nine exported functions validate their arguments, keep four globals of state, and forward
everything to private NVAPI entry points inside the installed NVIDIA display driver.

It is built out of three layers, and the reconstruction keeps that split:

```
  src/aftermath_api.cpp        the nine exports + two shared workers   (0x180004690..0x1800055A0)
  src/nvapi/nvapi_bridge.cpp   13 generated NVAPI stubs + loader glue  (0x180001000..0x180001C00)
  src/loader/*.cpp             NVIDIA's hardened "load nv*.dll" code   (0x180001CD0..0x180004580)
  src/dllmain.cpp              DllMain                                 (0x18000639C)
```

The loader layer is not Aftermath specific — it is the standard NVAPI "secure load
library" code that every NVIDIA user-mode component links in. It exists so that
`nvapi64.dll` is loaded **by absolute path from a trusted directory** (the DriverStore or
`%SystemRoot%\System32`) instead of via the process search path.

Everything above `0x1800055B0` other than `DllMain` is statically linked MSVC runtime
(`__scrt_*`, `__acrt_*`, `__vcrt_*`, `_initterm`, heap/locale/stdio, `memcpy`, `wcs*`,
`operator new`, `free`). **None of it was reimplemented** — it comes back by itself when
the project is linked against the static CRT, which is also why the project is configured
with `/MT`.

## 2. Reconciling the three listings

The three tools disagree in presentation, never in semantics. The rules used:

| Question | Which listing was authoritative |
|---|---|
| Control flow shape | Hex-Rays (structured `if/else`, visible early returns) |
| Exact constants & struct offsets | Ghidra (keeps raw hex, does not fold `+ 0x10` into field names) |
| Array bounds & stack layout | Binary Ninja (its `&stack0x…` sentinels expose the *end* of a stack array) |
| Signedness | whichever tool produced a *sane* type; cross-checked against the API being called |

Concrete examples:

* **`ResolveDriverModulePath`** — Hex-Rays renders the prefix loop as
  `while (v5 != &v16)`, which says nothing about the array size. Binary Ninja renders the
  same comparison as `&stack0xfffffffffffffff8`, i.e. one slot past `var_10`, proving the
  array is exactly two entries (`L"system32\\"`, `L"\\SystemRoot\\system32\\"`).
* **`GFSDK_Aftermath_GetDeviceStatus`** — Ghidra's
  `if ((iVar1 == 0) || (iVar2 = FUN_180001740(...), iVar2 == 0))` makes the
  comma-expression assignment explicit and shows that the second NVAPI id is a *fallback*,
  only called when the first one fails. Hex-Rays' `v3 == 0 || (v3 = …) == 0` says the same
  thing less obviously.
* **`Initialize`, DX12 branch** — all three agree that `QueryInterface` returning `0`
  (`S_OK`) is the *failure* path (`0xBAD0000A`). That is the point: a successful
  `QueryInterface` means the D3D12 debug layer is loaded, and Aftermath refuses to
  coexist with it.

### Decompiler artefacts that were removed

| Artefact in the listings | Rewritten as |
|---|---|
| `v5 = -1; do { v6 = *v4++; --v5; } while (v6); len = -v5 - 2;` | `wcslen(...)` |
| `_InterlockedAdd(dword_18001B9D8, 1u)` (an ARM-only MSVC intrinsic; the instruction is `lock xadd`) | `InterlockedIncrement` / `InterlockedDecrement` |
| `(*(void (**)(void))(**(_QWORD **)(v11 + 8) + 896LL))(...)` | `GetD3D11DeviceContextType(...)` — v-table index `0x380/8 == 112`, `ID3D11DeviceContext::GetType` |
| `(*(...)(*v4 + 64LL))(v4)` | `GetD3D12CommandListType(...)` — v-table index `0x40/8 == 8`, `ID3D12CommandList::GetType` |
| `(**a4)(a4, &unk_1800189F0, &v12)` | `pDevice->QueryInterface(IID_ID3D12DebugDevice, …)` |
| `*(_QWORD *)v10 = -1160773628;` | `StoreResultInMarkerData(contextData, GFSDK_Aftermath_Result_FAIL_InvalidParameter)` |
| `*(_DWORD *)(v7 + 4) = a1; *(_QWORD *)(v7 + 8) = a2;` | the `ContextHandleImpl` struct |
| `qword_18001BB98`, `qword_18001E3A0`, … (13 unrelated globals) | one `g_entryPointCache[]` array with named slot indices — they are a single `0x34C8` byte block that `sub_180001000` clears with one `memset` |
| `LOBYTE(v) / BYTE1(v) / _QWORD / __int64` | real types; the IDA macros live in `src/compat/ida_defs.h` for future pasting, and nothing in the build uses them |

## 3. Type recovery

### `GFSDK_Aftermath_Result`

Anchored by the arithmetic, not by guesswork: the codes the binary returns are
`0xBAD00000` plus `1,2,4,5,6,7,8,A,B,C,D,E,F,10`, and the *meaning* of the anchors is
unambiguous from the call sites —

| Value | Where it is produced | Name |
|---|---|---|
| `0xBAD00001` | `version != 19` | `FAIL_VersionMismatch` |
| `0xBAD00002` | every entry point when `g_initialized == false` | `FAIL_NotInitialized` |
| `0xBAD00004` | null arguments | `FAIL_InvalidParameter` |
| `0xBAD00006` | `apiType` neither 0 nor 1 | `FAIL_ApiError` |
| `0xBAD0000A` | `QueryInterface(debug device)` succeeded | `FAIL_D3DDebugLayerNotCompatible` |
| `0xBAD0000B` | `…EnableFeatures` failed | `FAIL_DriverInitFailed` |
| `0xBAD0000C` | driver version `< 0x9784` | `FAIL_DriverVersionNotSupported` |
| `0xBAD0000E` | `ID3D12CommandList::GetType() == BUNDLE` | `FAIL_GetDataOnBundle` |
| `0xBAD0000F` | `ID3D11DeviceContext::GetType() == DEFERRED` | `FAIL_GetDataOnDeferredContext` |
| `0xBAD00010` | feature-flag bit not set | `FAIL_FeatureNotEnabled` |

`0xA` → "D3D debug layer", `0xE` → "bundle", `0xF` → "deferred context" and `0x10` →
"feature not enabled" pin the numbering exactly, so the remaining names follow the
published enum with no ambiguity.

### NVAPI status → Aftermath result

One mapping, inlined at every call site in the binary:

| `NvAPI_Status` | → `GFSDK_Aftermath_Result` |
|---|---|
| `NVAPI_OK` (0) | `Success` |
| `NVAPI_INVALID_CALL` (-134), `NVAPI_NOT_SUPPORTED` (-104), `NVAPI_NO_IMPLEMENTATION` (-3) | `FAIL_NvApiIncompatible` |
| `NVAPI_TOO_MANY_UNIQUE_STATE_OBJECTS` (-133), `NVAPI_WAS_STILL_DRAWING` (-131), `NVAPI_ERROR` (-1) | `Fail` |
| `NVAPI_FILE_NOT_FOUND` (-132) | `FAIL_GettingContextDataWithNewCommandList` |
| `NVAPI_OUT_OF_MEMORY` (-130) | `FAIL_OutOfMemory` |
| `NVAPI_INVALID_ARGUMENT` (-5) | `FAIL_InvalidParameter` |
| anything else | `FAIL_Unknown` |

### `GFSDK_Aftermath_Device_Status`

`sub_1800051D0` maps the NVAPI device state onto `0..4`: `1 → Active`, `2/3/4 → Timeout`,
`5 → OutOfMemory`, `6/7 → PageFault`, anything else keeps the pre-seeded `Unknown` (4).

### `GFSDK_Aftermath_ContextData`

Recovered from the indexing in `sub_180004E40`: stride `16`, fields written at `+0`
(pointer), `+8` (uint32) and `+12` (uint32, always `3` = `Context_Status_Invalid` on the
error paths).

### Context handle

`operator new(0x18)` in `sub_180004AC0`, written as `{0, apiType}` in the first qword, the
caller's D3D object at `+8` and the NVAPI handle at `+0x10` — see
`aftermath::ContextHandleImpl`, which carries a `static_assert` on its size.

### The v-table indices

Two, both byte offsets in the listings: `0x380` on the object stored at `+8` of a DX11
handle and `0x40` on the object stored at `+8` of a DX12 handle. `0x380/8 = 112` is
`ID3D11DeviceContext::GetType` and `0x40/8 = 8` is `ID3D12CommandList::GetType`; the
constants they are compared against (`1`) are `D3D11_DEVICE_CONTEXT_DEFERRED` and
`D3D12_COMMAND_LIST_TYPE_BUNDLE`. The rebuild calls them through
`src/compat/com_min.h` rather than including `d3d11.h`/`d3d12.h`, because the original has
no DirectX dependency either.

## 4. Exports and entry point

The Ghidra listing labels five thunks with their ordinal: `@1` at `0x5590`, `@2` at
`0x5570`, `@3` at `0x5580`, `@4` at `0x5550` and `@8` at `0x55A0`. Sorting all nine
exported names alphabetically reproduces those five positions exactly:

```
1 GFSDK_Aftermath_DX11_CreateContextHandle     <- matches the listing (0x5590)
2 GFSDK_Aftermath_DX11_Initialize              <- matches the listing (0x5570)
3 GFSDK_Aftermath_DX12_CreateContextHandle     <- matches the listing (0x5580)
4 GFSDK_Aftermath_DX12_Initialize              <- matches the listing (0x5550)
5 GFSDK_Aftermath_GetData
6 GFSDK_Aftermath_GetDeviceStatus
7 GFSDK_Aftermath_GetPageFaultInformation
8 GFSDK_Aftermath_ReleaseContextHandle         <- matches the listing (0x55A0)
9 GFSDK_Aftermath_SetEventMarker
```

That is what a linker does when a `.def` file lists exports without explicit ordinals, so
the missing four (`5`, `6`, `7`, `9`) are not a guess — they are the only assignment
consistent with the five known ones. `exports.def` states all nine explicitly anyway.

x64 has a single calling convention, so the exported names are undecorated; `__cdecl` is
kept in the public header purely for source compatibility.

`DllMain` (`0x18000639C`) is a bare `return TRUE`. The DLL does not call
`DisableThreadLibraryCalls`, has no TLS callbacks and no static initialisers of its own —
all Aftermath state is created lazily by `GFSDK_Aftermath_DX1x_Initialize`.

## 5. Quirks kept on purpose

These look like bugs. They are reproduced because the goal is a drop-in rebuild.

1. **The initialisation guard is never released.** `Initialize` claims
   `g_initializeGuard` with `InterlockedCompareExchange` and never resets it, so a *failed*
   initialisation can never be retried: every later call returns `Success` while
   `g_initialized` stays `false` and all other entry points keep answering
   `FAIL_NotInitialized`. `tests/smoke_test.cpp` asserts this behaviour.
2. **`if (enter == NULL || leave != NULL) { enter = leave = NULL; }`** in
   `BindModuleEntryPoints`. All three decompilers agree on `!=`; the source almost
   certainly meant `leave == NULL`. Kept verbatim, with a comment.
3. **`CreateContextHandle` does not null-check its out parameter**, and leaks the
   allocation when `apiType` is invalid.
4. **`Initialize` leaks the debug-device reference** on the
   `FAIL_D3DDebugLayerNotCompatible` path (it only releases it when `QueryInterface`
   *failed*).
5. **`GetData` reports the *last* NVAPI status**, seeded with `NVAPI_ERROR`. If every
   handle in the array is null or unusable, no NVAPI call happens and the function returns
   `GFSDK_Aftermath_Result_Fail` — which is what `-1` maps to.
6. **`GetData` writes the failure code into `markerData`**, sign extended from 32 bits,
   and leaves `markerSize` untouched.
7. **`FindDriverModuleByEnumeratingValues` clears only `0x8000` of its `0x10000` byte
   buffer.** Harmless, kept.
8. **`GetDriverStoreDirectory` allocates `2 × RequiredSize`** although `RequiredSize` is
   already a byte count. Kept.

## 6. Deliberate deviations

Everything else is 1:1. These are the exceptions, all of them cases where the original
code contains something that is provably unreachable, undefined, or an artefact of the
compiler rather than of the source:

| # | Original | Reconstruction | Why |
|---|---|---|---|
| 1 | `StringCopyWorkerW`'s `if (cchDest == 0) { pszDest--; … }` writes a `NUL` *before* the buffer | returns `STRSAFE_E_INVALID_PARAMETER` | dead code: all five public wrappers reject `cchDest == 0` first |
| 2 | `isalpha()` / `isdigit()` called with raw UTF-16 code units | explicit ASCII tests | the CRT versions are undefined for values > 255 and assert in debug builds; the call sites mean "drive letter" and "decimal digit" |
| 3 | `NvLoadLibrary` reaches `_wcsnicmp` with a possibly-null name | explicit null check | nothing inside the DLL passes null; the alternative is a crash |
| 4 | `operator new` paired with `free()` | `new (std::nothrow)` paired with `delete` | same CRT heap on MSVC, but a matching pair; the `nothrow` form also lets `CreateContextHandle` return `FAIL_OutOfMemory` instead of throwing |
| 5 | 13 hand-cloned NVAPI stubs | one variadic helper + 13 one-line wrappers | identical code generation, 500 fewer lines |
| 6 | `GetProcAddress` calls scattered through the helpers | `src/loader/nv_system_api.cpp` | same laziness and same failure behaviour, one place to look |
| 7 | `dword_18001B000` holds *"is not Windows 7 or greater"* | `g_isWindows7OrGreater` holds the positive sense | readability; the branch is identical |

## 7. Inferred values — verify against `.rdata` before shipping

The listings covered `.text` only, so three constants had to be inferred from the way they
are used. All three are named and commented in the source; if you have the binary, dump
the bytes and confirm:

| Address | Assumed value | Basis |
|---|---|---|
| `0x1800189F0` | `IID_ID3D12DebugDevice` `{3FEBD6DD-4973-4787-8194-E45F9E28923E}` | it is the IID that `Initialize` passes to `ID3D12Device::QueryInterface`, and a *successful* query yields `FAIL_D3DDebugLayerNotCompatible` |
| `0x180011348` | `DEVPKEY_Device_DriverInfPath` `{A8B865DD-2E3D-4094-AD97-E593A70C75D6}, 5` | 20-byte `DEVPROPKEY` whose value is fed straight to `SetupGetInfDriverStoreLocationW` |
| `0x180011360` | `GUID_DISPLAY_DEVICE_ARRIVAL` `{1CA05180-A699-450A-9A0C-DE4FBE3DDD89}` | 16-byte GUID passed to `SetupDiGetClassDevsW` with `DIGCF_DEVICEINTERFACE` while looking for `VEN_10DE` |

Two further judgement calls worth reviewing:

* **`PathIsAbsolute`** is implemented as "drive letter + `:` + separator" only. UNC paths
  are rejected, which is the security-relevant reading (`LoadLibraryFromTrustedPath` must
  never reach a remote share). If the original also accepted a leading `\`, add it.
* **`SetEventMarker` argument validation** — the null checks on the handle and the marker
  pointer are certain; whether the original also rejected `markerSize == 0` could not be
  established from the listing, and the reconstruction allows it (rejecting it would break
  pointer-style markers if the original allowed them).
* **`GFSDK_Aftermath_PageFaultInformation`'s layout** is *not* recoverable from this
  binary — the pointer is forwarded to NVAPI untouched. The definition in
  `include/GFSDK_Aftermath.h` is the published SDK one.

## 8. Verification status

| Check | Status |
|---|---|
| All 8 translation units compile against the **real** Windows headers (`windows.h`, `setupapi.h`, `devpropdef.h`, `unknwn.h`, `winsvc.h`, `winreg.h`) for an `x86_64-windows` target, `-Wall -Wextra`, zero warnings | done — `tests/build_mingw.sh` |
| Links into a real x64 PE (`PE32+`, machine `0x8664`) | done |
| Export directory: nine undecorated names on ordinals 1–9, ordinal base 1, module name `GFSDK_Aftermath_Lib.x64.dll` | done — `tests/dump_exports.py` |
| Exported functions emitted in the same relative order as the original binary | done — `@9, @5, @6, @7, @4, @2, @3, @1, @8`, i.e. `0x4CC0 < 0x4E40 < 0x51D0 < 0x53D0 < 0x5550 < 0x5570 < 0x5580 < 0x5590 < 0x55A0` |
| Parses with no toolchain at all (stub headers, `-Wall -Wextra`) | done — `tests/syntax_check.sh` |
| Public header valid C89 and self contained | done — same script |
| `sizeof(ContextHandleImpl) == 0x18` | done — `static_assert` |
| MSVC v143 x64 build | **not run here** — no Windows SDK in this environment; use `GFSDK_Aftermath_Lib.sln` or the CMake build |
| Behaviour against a real NVIDIA driver | needs hardware |

Two independent checks are provided because they answer different questions:

* `tests/build_mingw.sh` needs a cross compiler (`x86_64-w64-mingw32-g++`, or set
  `CXX="zig c++ -target x86_64-windows-gnu"`). It compiles against the genuine Win32
  headers, links, and verifies the produced export table — so it catches wrong argument
  types, wrong constants, missing `_WIN32_WINNT` gates and export-table mistakes. It does
  **not** prove the MSVC build: the CRT and a handful of header details differ.
* `tests/syntax_check.sh` needs nothing but a host C++ compiler and type checks the
  sources against the stub headers in `tests/win32-shim/`.

Neither is a substitute for building the solution on Windows, which is the one remaining
step.

### Export-table evidence

```
ordinal  name                                          rva
1        GFSDK_Aftermath_DX11_CreateContextHandle      0x00002960
2        GFSDK_Aftermath_DX11_Initialize               0x000028E0
3        GFSDK_Aftermath_DX12_CreateContextHandle      0x00002930
4        GFSDK_Aftermath_DX12_Initialize               0x00002890
5        GFSDK_Aftermath_GetData                       0x00001C00
6        GFSDK_Aftermath_GetDeviceStatus               0x00002490
7        GFSDK_Aftermath_GetPageFaultInformation       0x00002770
8        GFSDK_Aftermath_ReleaseContextHandle          0x00002990
9        GFSDK_Aftermath_SetEventMarker                0x00001960
```

Sorted by address that is `@9, @5, @6, @7, @4, @2, @3, @1, @8` — the same sequence as
`0x180004CC0 … 0x1800055A0` in the original. The five wrapper thunks are larger here than
the 0x10-byte ones in the shipping DLL simply because this verification build is not
compiled with `/O2` tail-call merging.
