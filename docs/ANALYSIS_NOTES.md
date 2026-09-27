# Analysis notes

Reconstruction of `GFSDK_Aftermath_Lib.x64.dll` from decompiler output.

This document records **where every non-obvious value in the reconstruction came
from**, and **what could not be recovered**. It exists so that the source can be
audited against the original listing without re-deriving anything.

Provenance tags used in the source and below:

| Tag | Meaning |
|-----|---------|
| `[D]` | Recovered from the disassembly — an address, an immediate, or a control-flow structure that was actually read out of the binary. |
| `[H]` | Taken from the published `GFSDK_Aftermath` header. The binary's behaviour is consistent with it. |
| `[I]` | **Inferred.** Required for the code to work, but not confirmed from the listing. Every `[I]` is listed under [Open questions](#open-questions). |

---

## 1. What this DLL is

The binary is an early Nsight Aftermath revision (library revision 1.3), not a
current one. The evidence:

* `GFSDK_Aftermath_DX11_Initialize()` and `GFSDK_Aftermath_DX12_Initialize()`
  compare the caller's version argument against the immediate `19` (`0x13`) and
  return `FAIL_ApiError` otherwise. `[D]` So `GFSDK_Aftermath_Version_API` is
  `0x13`, not the `0x217` (535) of the current SDK.
* It exports **nine** functions. The device-status enumeration stops at
  `PageFault` plus a "no information" value; there is no `Stopped`, `Reset`,
  `DmaFault` or `DeviceRemovedNoGpuFault`. `[D]`
* Nothing resembling `GFSDK_Aftermath_EnableGpuCrashDumps`, the shader-debug-info
  callbacks, or the description-callback API appears anywhere in the image. `[D]`
  Those are later additions.

This matters because most publicly available Aftermath headers are for much newer
revisions. The newer enumerations (`GFSDK_Aftermath_Device_Status_Active = 0`,
`Unknown = 6`, …) do **not** apply here; see §3.

---

## 2. The nine exports

Emitted in `src/exports.def`:

```
GFSDK_Aftermath_DX11_Initialize
GFSDK_Aftermath_DX12_Initialize
GFSDK_Aftermath_DX11_CreateContextHandle
GFSDK_Aftermath_DX12_CreateContextHandle
GFSDK_Aftermath_ReleaseContextHandle
GFSDK_Aftermath_SetEventMarker
GFSDK_Aftermath_GetData
GFSDK_Aftermath_GetDeviceStatus
GFSDK_Aftermath_GetPageFaultInformation
```

x64 has a single calling convention, so these are undecorated: no leading
underscore, no `@n` suffix. `[D]`

### The `_0` suffixes are not real

The decompiler presents several of these as `GFSDK_Aftermath_GetData_0`,
`GFSDK_Aftermath_GetDeviceStatus_0` and so on. That suffix is analysis-database
bookkeeping — the disassembler appends an ordinal when the name it wants to
assign is already taken in the database. It is not part of the export table. The
names above are the real ones; the suffix is stripped everywhere in this tree.

> **Unresolved discrepancy.** One planning note from the recovery pass refers to
> "the 15 `GFSDK_Aftermath_*` exports" while the recovered function list contains
> exactly nine distinct export names (the extras being `_0`-suffixed duplicates
> of the same functions). The nine above are what the export-name list supports.
> If the original truly exports fifteen distinct names, six are missing from the
> recovery and **`src/exports.def` will need to grow**. This is the single
> highest-value thing to re-check against the binary.

---

## 3. Result codes, and the one place the public header misleads

All `GFSDK_Aftermath_Result` values are exact. `[H]` `[D]`

```
Success                                       0x00000001
NotAvailable                                  0x00000002
Fail                                          0xBAD00000
FAIL_VersionMismatch                          0xBAD00001
FAIL_NotInitialized                           0xBAD00002
FAIL_InvalidAdapter                           0xBAD00003
FAIL_InvalidParameter                         0xBAD00004
FAIL_Unknown                                  0xBAD00005
FAIL_ApiError                                 0xBAD00006
FAIL_NvApiIncompatible                        0xBAD00007
FAIL_GettingContextDataWithNewCommandList     0xBAD00008
FAIL_AlreadyInitialized                       0xBAD00009
FAIL_D3DDebugLayerNotCompatible               0xBAD0000A
FAIL_DriverInitFailed                         0xBAD0000B
FAIL_DriverVersionNotSupported                0xBAD0000C
FAIL_OutOfMemory                              0xBAD0000D
FAIL_GetDataOnBundle                          0xBAD0000E
FAIL_GetDataOnDeferredContext                 0xBAD0000F
FAIL_FeatureNotEnabled                        0xBAD00010
FAIL_NoResourcesRegistered                    0xBAD00011
FAIL_ThisResourceNeverRegistered              0xBAD00012
FAIL_NotSupportedInUWP                        0xBAD00013
FAIL_D3dDllNotSupported                       0xBAD00014
FAIL_D3dDllInterceptionNotSupported           0xBAD00015
FAIL_Disabled                                 0xBAD00016
FAIL_NotSupportedOnContext                    0xBAD00017
```

Independent confirmation from the binary itself: `FAIL_FeatureNotEnabled`
(`0xBAD00010`) is exactly what the recovered feature-flag guards return, and
`FAIL_NotInitialized` (`0xBAD00002`) is what the `g_initialized` byte test
returns. `[D]`

### The three per-context sentinels

`GFSDK_Aftermath_GetData()` stores a failure code *in the entry* rather than
failing the whole call. The listing shows three negative immediates here. Widened
to 32 bits they decode to:

| Listing | 32-bit | Enumerator |
|---------|--------|------------|
| `-0x452FFFF1` | `0xBAD0000F` | `FAIL_GetDataOnDeferredContext` |
| `-0x452FFFF2` | `0xBAD0000E` | `FAIL_GetDataOnBundle` |
| `-0x452FFFFC` | `0xBAD00004` | `FAIL_InvalidParameter` (null handle) |

That these three literals land on exactly the three documented per-context
failure modes is what identifies the function. `[D]`

### `GFSDK_Aftermath_Device_Status` — do not copy the modern header

The translation in `GFSDK_Aftermath_GetDeviceStatus()` is fully recovered: `[D]`

| driver status | value written | meaning |
|---------------|---------------|---------|
| 1 | 0 | Active |
| 2, 3, 4 | 1 | Timeout |
| 5 | 2 | OutOfMemory |
| 6, 7 | 3 | PageFault |
| anything else | 4 | "no information" |

Two details fix the numbering: the output slot is **pre-set to 4** before the
driver is called (so a driver failure leaves 4 behind), and driver status 1 means
"running normally", which must map onto the first enumerator.

So this revision is **0-based and defines only 0..3**. The current SDK header
gives `Active = 0, Timeout = 1, OutOfMemory = 2, PageFault = 3, Stopped = 4,
Reset = 5, Unknown = 6` — the first four agree, but `4` here is *not* `Stopped`.
Transcribing the modern enumeration would be a bug. `include/GFSDK_Aftermath_Defines.h`
uses the recovered numbering.

---

## 4. State layout

The image keeps everything in `.data`. `[D]`

| Address | Name in this tree | Purpose |
|---------|-------------------|---------|
| `0x18002029C` | `aftermath::g_oneTimeInitState` | one-time init interlock (0 / 1 / 2) |
| `0x1800202A0` | `aftermath::g_pDevice` | device passed to `Initialize` |
| `0x1800202A8` | `aftermath::g_featureFlags` | `GFSDK_Aftermath_FeatureFlags` |
| `0x1800202AC` | `aftermath::g_initialized` | "a successful Initialize happened" |

Feature-flag gates, both confirmed by their immediates: `[D]`

* bit 0 (`EnableMarkers`) → `SetEventMarker`, `GetData`
* bit 1 (`EnableResourceTracking`) → `GetPageFaultInformation`

No other bit is ever inspected. Later SDK revisions define bits 3, 4 and 30
(`GenerateShaderDebugInfo`, `EnableShaderErrorReporting`, `CallStackCapturing`);
they are declared in the header for source compatibility but this build ignores
them.

### Context handle layout

`GFSDK_Aftermath_DX11/DX12_CreateContextHandle()` heap-allocates `0x18` bytes: `[D]`

```
+0x00  uint32_t  never read by any recovered entry point
+0x04  uint32_t  API selector: 0 = D3D11, 1 = D3D12
+0x08  void*     the caller's ID3D11DeviceContext* / ID3D12CommandList*
+0x10  void*     driver-side state, set to null on creation
```

`SetEventMarker` dispatches on the word at `+0x04`; `ReleaseContextHandle` frees
the object after checking `g_initialized` and rejecting null.

Because `ReleaseContextHandle` makes **no driver call**, anything the driver
stores must be reachable without the handle — which is why `+0x10` is modelled as
driver-owned state that the handle merely carries a pointer to. `[I]`

---

## 5. The NVAPI layer

The library does not link `nvapi64.lib`. It locates `nvapi64.dll` at run time and
resolves one exported query function, then uses it to obtain private interfaces
by 32-bit id. `[D]`

Query function spellings probed, in order: `nvapi_QueryInterface`, then
`nvapi_pepQueryInterface`. `[D]`

### The id set is exact

These thirteen ids were extracted as immediates: `[D]`

```
0x2926AAAD  0xC663BA92  0x96161ACB  0x1DE221DD  0x8C68F0F1
0xB2E3E2A2  0x633D88E1  0xC99F4A67  0xF1EA1980  0xCBA3F913
0xDBE53CB2  0x0BBA25D7  0x6446BEB8
```

plus the tracing pair `0x33C7358C` / `0x593E8644`.

Two of these are pinned to a feature with confidence: `0x0BBA25D7` and
`0x6446BEB8` are the page-fault pair. `[D]`

### Which id belongs to which feature is inferred

The remaining assignment follows the layout order of the thunks in the image and
the shape of their call sites (argument counts and types were readable even where
the id was not). It is `[I]`.

**The entire pairing lives in `src/nvapi/nvapi_ids.h`.** Nothing else depends on
it being right — every call site goes through a named constant. If you have the
listing, that is the one file to correct.

The reconstruction assumes one id pair per feature that has both a D3D11 and a
D3D12 flavour (initialize, set event marker, get data, get device status,
get page fault information = ten ids), with the remaining ids belonging to
plumbing that could not be tied to a public entry point.

### Interface version gate

`Initialize()` reads a 32-bit value through NVAPI and rejects it when below
`0x9784`, returning `FAIL_DriverVersionNotSupported`. `[D]` The threshold is
exact. *Which* id carries that value is `[I]` — see open question 2.

---

## 6. Driver result translation

The driver answers with two different kinds of code, and they must not be
confused:

* **A `GFSDK_Aftermath_Result` value** — anything in the `0xBAD00000` range.
  Handed through untouched. This is what produces the `GetData` per-context
  sentinels above.
* **A small negative driver-internal status** with no meaning outside the driver.
  Translated by the table in `MapDriverStatus()`. `[D]`

The recovered table: `[D]`

```
-0x86, -0x68, -3   ->  0xBAD00007  FAIL_NvApiIncompatible
-0x85, -0x83, -1   ->  0xBAD00000  Fail
-0x84              ->  0xBAD00008  FAIL_GettingContextDataWithNewCommandList
-0x82              ->  0xBAD0000D  FAIL_OutOfMemory
-5                 ->  0xBAD00004  FAIL_InvalidParameter
default            ->  0xBAD00005  FAIL_Unknown
```

Note that `-0x85` and `-0x83` share a case body with `-1` while `-0x84` sits
between them — characteristic of a generated dispatch table over a driver status
enumeration rather than hand-written code, which is corroborating evidence that
the transcription is faithful.

Distinguishing the two cases on the `0xBAD0` prefix is `[I]`; the *table* is
`[D]`, but the original may test for it differently.

---

## Open questions

Everything below is something a complete recovery would still need to settle.
Each is isolated in the source so that fixing it is a local change.

1. **Nine or fifteen exports?** See §2. If the binary exports more than the nine
   recovered names, they are missing here. *Highest priority.*

2. **Which id returns the interface version?** The `0x9784` threshold is exact,
   but the id it is read through is inferred (`NVAPI_ID_INTERFACE_VERSION` in
   `src/nvapi/nvapi_ids.h`). Wrong guess → `Initialize()` reports
   `FAIL_DriverVersionNotSupported` on a healthy driver.

3. **Where does the D3D11/D3D12 selector for the device-level queries come from?**
   `GetDeviceStatus()` and `GetPageFaultInformation()` take no context handle and
   so have no per-call selector, yet must choose between the two driver
   interfaces. Modelled as `aftermath::g_activeApi`, a variable that is set on
   `Initialize()`. The original reads this from somewhere in `.data` that was not
   pinned down; it may well be a second field of the same block as `g_pDevice`.

4. **Where exactly is `MapDriverStatus()` applied?** The table is recovered; its
   call sites are inferred (any driver result that needs translating).

5. **Feature → id pairing.** See §5.

6. **Signature of each NVAPI interface.** NVAPI returns an untyped `void*` and the
   original casts inline at each call site. The typedefs in
   `src/nvapi/aftermath_driver.cpp` state the contract the *call sites* imply
   (arity and argument types recovered from the thunks); the parameter names and
   exact driver-side meaning are not knowable from this side.

7. **The `+0x00` word of the context handle.** Never read by any recovered entry
   point. Carried to preserve the 0x18-byte size and the offsets.

8. **The marker payload size limit.** `SetEventMarker` rejects payloads above
   `GFSDK_AFTERMATH_MARKER_DATA_SIZE` (6 KiB, `[H]`). The bound is documented for
   the SDK but was *not* confirmed as an immediate in this revision.

9. **Null-pointer checks.** The recovered listing confirms the pointer tests in
   `SetEventMarker`, `GetData`, `ReleaseContextHandle` and both
   `CreateContextHandle` flavours. The null-*device* test in `Initialize()` and
   the null-`pStatus`/null-`pPageFaultInfo` tests are `[I]` — consistent with the
   rest of the image, but not individually confirmed.

10. **Gracious shutdown.** No driver teardown entry point was identified.
    `aftermath::Shutdown()` resets the library state and drops the cached
    interfaces; `DriverReleaseDevice()` is a deliberate no-op with a single place
    to grow into if the real teardown call is found.

---

## Verification

`make -f Makefile.host test` compiles all six translation units with
`-Wall -Wextra -Werror` and runs an 87-check behavioural test against stubbed
driver entry points. It covers the version gate, both feature-flag gates, the
handle layout, the `GetData` per-context encoding (including both the
public-result and driver-internal-status paths), the full device-status
translation table, the shutdown/re-initialise cycle, and every result-code
constant including the three sentinel decodings.

The only thing the test build changes is one preprocessor define:
`AFTERMATH_TEST_BUILD` replaces the `windows.h` dependency in the loader and in
`DllMain` with a hook the test drives. **No library logic is compiled out.**

This test cannot validate the `[I]` items above — those need the original
listings or a live comparison against the shipped DLL.
