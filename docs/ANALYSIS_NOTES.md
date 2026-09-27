# Analysis notes

Reconstruction of `GFSDK_Aftermath_Lib.x64.dll` from decompiler output.

This document records **where every non-obvious value in the reconstruction came
from**, and **what is still unresolved**. It exists so that the source can be
audited against the original listing without re-deriving anything.

Provenance tags used in the source and below:

| Tag | Meaning |
|-----|---------|
| `[D]` | Recovered — an address, an immediate, or a control-flow structure read out of the listing. |
| `[H]` | Taken from the published `GFSDK_Aftermath` header. The binary's behaviour is consistent with it. |
| `[I]` | **Inferred.** Required for the code to work, but not confirmed from the listing. Every `[I]` is listed under [Open questions](#open-questions). |

> **Status.** The full decompiled listing has been reconciled against this tree.
> Every `[I]` in the first pass that the listing could settle is now `[D]`, and
> several shipped choices were corrected as a result — see
> [§7](#7-corrections-the-listing-forced). What remains genuinely unknown is
> itemised in [Open questions](#open-questions); the list is now short and each
> entry is a single constant or a single call site.

---

## 1. What this DLL is

The binary is an early Nsight Aftermath revision (library revision 1.3), not a
current one. The evidence:

* `GFSDK_Aftermath_DX11_Initialize()` and `GFSDK_Aftermath_DX12_Initialize()`
  compare the caller's version argument against the immediate `19` (`0x13`) and
  return `FAIL_VersionMismatch` otherwise. `[D]` So `GFSDK_Aftermath_Version_API`
  is `0x13`, not the `0x217` (535) of the current SDK.
* It exports **nine** functions, confirmed by the export-table indices that
  appear next to five of them in the listing — see §2. `[D]`
* The device-status enumeration stops at `PageFault` plus a "no information"
  value; there is no `Stopped`, `Reset`, `DmaFault` or
  `DeviceRemovedNoGpuFault`. `[D]`
* Nothing resembling `GFSDK_Aftermath_EnableGpuCrashDumps`, the
  shader-debug-info callbacks, or the description-callback API appears anywhere
  in the image. `[D]` Those are later additions.

This matters because most publicly available Aftermath headers are for much newer
revisions. The newer enumerations (`Unknown = 6`, …) do **not** apply here; see §3.

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

### How the list is known to be complete

Five of the recovered functions carry an adjacent constant in the listing — `1`
for `DX11_CreateContextHandle`, `2` for `DX11_Initialize`, `3` for
`DX12_CreateContextHandle`, `4` for `DX12_Initialize` and `8` for
`ReleaseContextHandle`. Those are not API ordinals. They are indices into the
DLL's **sorted export name table**, which is exactly what the linker emits for a
`.def` file. Sorting the nine names above reproduces all five indices:

| Index | Name |
|-------|------|
| 1 | `GFSDK_Aftermath_DX11_CreateContextHandle` |
| 2 | `GFSDK_Aftermath_DX11_Initialize` |
| 3 | `GFSDK_Aftermath_DX12_CreateContextHandle` |
| 4 | `GFSDK_Aftermath_DX12_Initialize` |
| 5 | `GFSDK_Aftermath_GetData` |
| 6 | `GFSDK_Aftermath_GetDeviceStatus` |
| 7 | `GFSDK_Aftermath_GetPageFaultInformation` |
| 8 | `GFSDK_Aftermath_ReleaseContextHandle` |
| 9 | `GFSDK_Aftermath_SetEventMarker` |

Five independent indices landing on five distinct names, in order, with the
remaining four filled by names that were independently recovered as exports, is
about as strong as export-list evidence gets. `tests/check_exports.sh` re-runs
this check on every `make test`.

### The `_0` suffixes are not real

The first listing presents several functions as `GFSDK_Aftermath_GetData_0`,
`GFSDK_Aftermath_GetDeviceStatus_0` and so on. The trailing `_0` is an artifact
of the analysis database: when a symbol name that is about to be created already
exists, IDA disambiguates by appending an ordinal. The third listing, produced
independently, shows the plain names. Both are the same functions, and the
export table has the plain names. `[D]`

---

## 3. Result codes, and the one place the public header misleads

`GFSDK_Aftermath_Result` has base `0xBAD00000`, with `Success = 1` and
`NotAvailable = 2` outside that range. The `0xBAD00000 + n` values, `n = 0x01`
upward, were all recovered as immediates and cross-checked against the published
header:

| Value | Name |
|-------|------|
| `0xBAD00000` | `Fail` |
| `0xBAD00001` | `FAIL_VersionMismatch` |
| `0xBAD00002` | `FAIL_NotInitialized` |
| `0xBAD00004` | `FAIL_InvalidParameter` |
| `0xBAD00005` | `FAIL_Unknown` |
| `0xBAD00006` | `FAIL_ApiError` |
| `0xBAD00007` | `FAIL_NvApiIncompatible` |
| `0xBAD00008` | `FAIL_GettingContextDataWithNewCommandList` |
| `0xBAD0000A` | `FAIL_D3DDebugLayerNotCompatible` |
| `0xBAD0000B` | `FAIL_DriverInitFailed` |
| `0xBAD0000C` | `FAIL_DriverVersionNotSupported` |
| `0xBAD0000D` | `FAIL_OutOfMemory` |
| `0xBAD0000E` | `FAIL_GetDataOnBundle` |
| `0xBAD0000F` | `FAIL_GetDataOnDeferredContext` |
| `0xBAD00010` | `FAIL_FeatureNotEnabled` |

Two facts about this table are worth stating explicitly, because both were got
wrong on the first pass:

* **A wrong version returns `FAIL_VersionMismatch` (`0xBAD00001`), not
  `FAIL_ApiError`.** `FAIL_ApiError` (`0xBAD00006`) is used in exactly one
  situation: an API selector word that is neither 0 nor 1, which the exports can
  never produce. `[D]`
* **`FAIL_ApiError` is therefore "unknown API index"**, not "something went
  wrong at the API level". Any documentation that describes it otherwise is
  describing a later revision.

`GFSDK_Aftermath_Version_API == 0x13` is `[D]`: it is the immediate `19`
compared in `Initialize()`.

### The three per-context sentinels

`GetData` writes a result code *into an entry's `markerData` pointer slot* when
that entry cannot be serviced. The disassembly shows the arithmetic base
(`0xBAD00000`) plus three offsets, which decode to: `[D]`

```
-0x452FFFFC  =  0xBAD00004  FAIL_InvalidParameter           (null context handle)
-0x452FFFF1  =  0xBAD0000F  FAIL_GetDataOnDeferredContext   (D3D11 deferred)
-0x452FFFF2  =  0xBAD0000E  FAIL_GetDataOnBundle            (D3D12 bundle)
```

All three are hard-coded sentinels, not driver statuses: the driver is never
called on those paths.

### `GFSDK_Aftermath_Device_Status` — do not copy the modern header

The recovered mapping is 0-based, not the 1-based ordering found in current
headers: `[D]`

| Driver value | Status |
|---|---|
| 1 | `Active` (0) |
| 2, 3, 4 | `Timeout` (1) |
| 5 | `OutOfMemory` (2) |
| 6, 7 | `PageFault` (3) |
| anything else | leaves the pre-set value (`Unknown`, 4) |

The sentinel is 4 and the output word is *pre-set* to it before the driver is
consulted, so a driver failure leaves `Unknown` in the caller's variable. `[D]`

---

## 4. State layout

The image keeps everything in `.data`: `[D]`

| Address | Name in this tree | Purpose |
|---------|-------------------|---------|
| `0x18001B9D8` | `aftermath::g_driverRefCount` | driver-call refcount, `_InterlockedAdd ±1` around every thunk |
| `0x18002029C` | `aftermath::g_initGuard` | `Initialize()` compare-exchange target — see below |
| `0x1800202A0` | `aftermath::g_deviceDriverHandle` | **driver handle** for the device, not the device pointer |
| `0x1800202A8` | `aftermath::g_featureFlags` | `GFSDK_Aftermath_FeatureFlags` |
| `0x1800202AC` | `aftermath::g_initialized` | "a successful `Initialize()` happened" |

### The `Initialize()` latch

`Initialize()` opens with

```c
if ( _InterlockedCompareExchange(&dword_18002029C, 1, 0) == 0
     || byte_1800202AC != 0 )
{
    /* the real work */
}
return 1;                       /* GFSDK_Aftermath_Result_Success */
```

and the `1` is **never** written back to 0. `[D]` Two consequences, both
reproduced deliberately:

1. After a **failed** first `Initialize()`, `byte_1800202AC` stays clear, so
   every later call skips the body and falls through to the unconditional
   `return 1` — reporting success without touching the driver at all.
2. A second `Initialize()` after a *successful* one re-runs the body and
   overwrites the flags. There is no `FAIL_AlreadyInitialized` path in this
   revision.

`tests/host_smoke_test.cpp` asserts both. `aftermath::TestResetInitializeGuard()`
exists so the test can clear the latch; the shipping build has no such function
and `Shutdown()` deliberately does not reset it.

### Context handle layout

`GFSDK_Aftermath_DX11/DX12_CreateContextHandle()` heap-allocates `0x18` bytes: `[D]`

```
+0x00  uint32_t  zeroed, never read by any entry point
+0x04  uint32_t  API selector: 0 = D3D11, 1 = D3D12
+0x08  void*     the caller's ID3D11DeviceContext* / ID3D12CommandList*
+0x10  void*     DRIVER HANDLE, written by the attach call
```

`+0x00` and `+0x04` are one zeroed 64-bit slot whose upper half is then stamped
with the API selector; the original stores the qword as zero and then overwrites
half of it. `[D]`

**The context is registered with the driver at creation time**, by the same
attach call `Initialize()` uses — that is why there is no registration step
anywhere else, and why `ReleaseContextHandle()` needs no driver call: the driver
handle at `+0x10` belongs to the driver. `[D]`

The handle is published to the caller **before** the driver can reject it, and a
failed attach does not free it. The original leaks 0x18 bytes on that path; the
reconstruction reproduces the leak rather than "fixing" it, because the caller
sees the handle either way.

---

## 5. The NVAPI layer

The library does not link `nvapi64.lib`. It locates `nvapi64.dll` at run time and
resolves one exported query function, then uses it to obtain private interfaces
by 32-bit id. `[D]`

Query function spellings probed: `nvapi_QueryInterface` for `nvapi64.dll`,
`nvapi_pepQueryInterface` for `nvpowerapi.dll`. `[D]`

### The id table is exact

Every id below was read as an immediate from the corresponding thunk, and each
thunk's caller pins down what it does: `[D]`

| Id | Called from | With |
|---|---|---|
| `0x2926AAAD` | `Initialize()` | `(uint32_t* version, void* info64)` |
| `0xC663BA92` | `SetEventMarker`, API 0 | `(driverHandle, data, size)` |
| `0x8C68F0F1` | `SetEventMarker`, API 1 | `(driverHandle, data, size)` |
| `0x96161ACB` | `GetData`, API 0 | `(driverHandle, &data, &size, &status)` |
| `0xB2E3E2A2` | `GetData`, API 1 | `(driverHandle, &data, &size, &status)` |
| `0x1DE221DD` | `GetDeviceStatus`, tried first | `(driverHandle, &status)` |
| `0x633D88E1` | `GetDeviceStatus`, fallback | `(driverHandle, &status)` |
| `0xC99F4A67` | `Initialize()` + `CreateContextHandle()`, API 0 | `(pD3DObject, &driverHandle)` |
| `0xF1EA1980` | `Initialize()` + `CreateContextHandle()`, API 1 | `(pD3DObject, &driverHandle)` |
| `0xCBA3F913` | `Initialize()`, API 0, after attach | `(driverHandle, flags)` |
| `0xDBE53CB2` | `Initialize()`, API 1, after attach | `(driverHandle, flags)` |
| `0x0BBA25D7` | `GetPageFaultInformation`, tried first | `(driverHandle, pInfo)` |
| `0x6446BEB8` | `GetPageFaultInformation`, fallback | `(driverHandle, pInfo)` |
| `0x33C7358C` | tracing begin | `(id, &token)` |
| `0x593E8644` | tracing end | `(id, token, status)` |

The whole table lives in `src/nvapi/nvapi_ids.h`.

**Create/register is separate from enable-features.** The first thunk allocates
the driver-side object and returns its handle; the second turns the feature flags
into driver-side tracking, and it takes the *handle*, not the D3D object. An
earlier reconstruction had these merged. `[D]`

### One thunk per (feature, API), each with its own cache

There is no shared dispatcher and no `SelectId(api, id11, id12)` helper: the
image contains thirteen near-identical thunks, each with its own cached
function-pointer global and its own id. The common shape is `[D]`

```
++dword_18001B9D8;
status = EnsureLoaded(0);
if ( status == 0 )
{
    fn = cached ?: nvapi_QueryInterface(id);
    if ( fn == 0 ) status = -3;                       /* DriverStatus_Unavailable */
    else { traceBegin(id, &token); status = fn(...); traceEnd(id, token, status); }
}
--dword_18001B9D8;
return status;
```

`src/nvapi/aftermath_driver.cpp` keeps one C++ wrapper per thunk so the
per-thunk identity (and therefore the per-thunk cache) is preserved, while the
repetition is expressed once in a helper.

### Interface version gate

`Initialize()` reads a 32-bit value through id `0x2926AAAD` and rejects anything
below `0x9784` with `FAIL_DriverVersionNotSupported`. `[D]` Both the id and the
threshold are exact.

### Tracing

The trace pair is resolved at load time, and every thunk brackets its driver call
with it. The listing's test for the pair reads

```c
if ( traceBegin == 0 || traceEnd != 0 ) { traceBegin = 0; traceEnd = 0; }
```

which, taken literally with a short-circuiting `||`, keeps the pair only when
`traceBegin` resolves **and** `traceEnd` does not — i.e. it disables tracing
unconditionally. That is self-contradictory and is almost certainly a rendering
of an inverted test. It is reproduced literally in the Windows loader path and
flagged in open question 3; it is harmless either way, because every call site
skips a null trace function, and the host test injects a working pair through
`SetQueryInterfaceForTesting()` so the bracketing itself is covered.

---

## 6. Driver result translation

The driver answers with two different kinds of code, and they must not be
confused:

* **a `GFSDK_Aftermath_Result` value** — anything in the `0xBAD00000` range,
  handed through untouched (this is what the `GetData` sentinels use);
* **a small negative driver-internal status**, translated by `MapDriverStatus()`.

The table is identical in every thunk and in every caller: `[D]`

```
-0x86, -0x68, -3   ->  0xBAD00007  FAIL_NvApiIncompatible
-0x85, -0x83, -1   ->  0xBAD00000  Fail
-0x84              ->  0xBAD00008  FAIL_GettingContextDataWithNewCommandList
-0x82              ->  0xBAD0000D  FAIL_OutOfMemory
-5                 ->  0xBAD00004  FAIL_InvalidParameter
 0                 ->  Success
default            ->  0xBAD00005  FAIL_Unknown
```

`-0x85` and `-0x83` sharing a case body with `-1` while `-0x84` sits between them
is characteristic of a generated dispatch table over a driver status enumeration
rather than hand-written code — corroborating evidence that the transcription is
faithful.

---

## 7. Corrections the listing forced

The first reconstruction was built from the export table and the constant pool
alone, and had to infer the rest. The listing settled these, and the code was
changed:

| # | First pass | Listing |
|---|---|---|
| 1 | wrong version → `FAIL_ApiError` | → `FAIL_VersionMismatch` (`0xBAD00001`) `[D]` |
| 2 | no DX12 debug-layer probe | `Initialize(DX12)` QueryInterfaces the device; **success** → `FAIL_D3DDebugLayerNotCompatible`, non-null result released via vtable `+0x10` `[D]` |
| 3 | one attach/register thunk, flags folded in | two thunks per API; flags failure → `FAIL_DriverInitFailed` (`0xBAD0000B`), a fixed code rather than a mapped status `[D]` |
| 4 | `g_activeApi` selected the device-level entry points | no such global: `GetDeviceStatus`/`GetPageFaultInformation` try the D3D11 thunk first and fall back to the D3D12 one `[D]` |
| 5 | ids paired by inference | exact per-(feature, API) table, §5 `[D]` |
| 6 | no refcount, no tracing | `0x18001B9D8` in-flight counter plus a trace bracket around every driver call `[D]` |
| 7 | `DllMain` did housekeeping and teardown | the recovered `DllMain` is literally `return TRUE;` — no `DisableThreadLibraryCalls`, no teardown, and no shutdown API anywhere `[D]` |
| 8 | export list "probably nine" | proven nine by export-table index, §2 `[D]` |
| 9 | deferred/bundle detection assumed to be driver-reported | detected through the D3D COM vtable: D3D11 slot 112 (`+0x380`, `GetType == 1`), D3D12 slot 8 (`+0x40`, `GetType == 1`) `[D]` |
| 10 | marker data validated | `SetEventMarker` has **no** null test and **no** size limit `[D]` |
| 11 | `CreateContextHandle` registered on first use | registers at creation, and publishes the handle before the driver can reject it `[D]` |

Also confirmed unchanged: the handle layout, the driver-status table, the
`ContextData` encoding (stride 16, per-entry `status = 3` plus the result code in
`markerData`), the feature gates (bit 0 on `SetEventMarker`/`GetData`, bit 1 on
`GetPageFaultInformation`, nothing on `GetDeviceStatus`), `Version_API = 0x13`,
and the `_0` suffix explanation.

---

## Open questions

Each remaining item is isolated so that correcting it is a one-line change.

1. **The debug-layer IID.** `Initialize(DX12)` passes the address of a 16-byte
   GUID at `0x1800189F0`. The bytes were not part of the listing; the
   reconstruction uses `IDXGIDebug`'s, which the call shape implies but does not
   prove. **The one constant to change if you can read the image:**
   `kXgiDebugIid` in `src/aftermath_core.cpp`. `[I]`

2. **The NVAPI usability probe.** The loader resolves id `0x150E828`, rejects a
   null result, then calls it and rejects a non-zero return. Neither the id's
   meaning nor the callee's return type is knowable from this side; the
   reconstruction models it as `int32_t(void)`. `[I]`

3. **The inverted-looking trace-pair test.** See §5. Reproduced literally. `[I]`

4. **`GetData`'s result accumulator.** The closing guard is an unsigned "was it
   positive" test, so the seed must be 0 or `-1`; the reconstruction seeds 0, so
   a call whose contexts are all rejected by the sentinel paths returns success.
   If `-1` is right, that case returns `Fail` instead. One constant, one comment.
   `[I]`

5. **Parameter meaning of each NVAPI interface.** Arity and argument *types* were
   recovered from the call sites; the driver-side meaning of each parameter is
   not knowable from this binary. `[I]`

6. **The `+0x00` word of the context handle.** Never read. Carried to preserve
   the 0x18-byte size and the offsets. `[I]`

7. **`GFSDK_Aftermath_PageFaultInformation`.** Forwarded to the driver
   untouched and never dereferenced by this library, so its layout cannot be
   recovered here; the header declares it as an opaque struct. `[H]`

8. **No shutdown path exists.** There is no driver teardown call and no teardown
   API in the image; `aftermath::Shutdown()` in this tree is a reconstruction
   artefact used by the module-detach path and the test, and is the only place
   that diverges from the original by *adding* behaviour. `[I]`

9. **The per-flavour "loading" byte** (`0x18001B9D0`) is polled with a one-second
   timeout but never set anywhere in this image — it is a handshake with a loader
   thread that is not part of this binary. The wait loop is reproduced; the other
   half of the handshake is unobservable. `[I]`

10. **The DLL locator.** The real loader is a large NVIDIA driver locator
    (SetupAPI `VEN_10DE`, driver store, service configuration, Khronos registry
    keys). The reconstruction uses a `LoadLibrary` of the two names the listing
    actually references, which reaches the same interfaces on any machine with a
    driver installed but is not a faithful reproduction of the search. `[I]`

---

## Verification

`make -f Makefile.host test` runs four gates, all of which must pass:

1. **161 behavioural checks** (`tests/host_smoke_test.cpp`), compiled with
   `-Wall -Wextra -Werror`, against stubbed driver entry points and fake COM
   objects. Coverage: the `Initialize()` latch, the version gate, the interface
   version gate, the debug-layer probe (present, absent, and the release path),
   the attach/enable-features sequence, the handle layout, both feature gates,
   `GetData`'s per-context encoding including the bundle and deferred sentinels,
   the full device-status table, the primary/fallback paths of both device-level
   queries, the trace bracket and the refcount, and every result-code constant
   with the sentinel decodings.
2. **`c-compat`** — the public headers compile as C99 as well as C++17.
3. **`wincheck`** — the `#ifndef AFTERMATH_TEST_BUILD` branches (the loader and
   `DllMain`, i.e. the code MSVC actually compiles) are type-checked against a
   minimal `windows.h` stand-in, so a typo there fails here rather than in Visual
   Studio.
4. **`artifacts` / `exports`** — no decompiler identifier survives outside a
   comment, `defs.h` stays opt-in, and `exports.def` matches the source exactly
   with the nine recovered names in the recovered order.

The only thing the test build changes is one preprocessor define:
`AFTERMATH_TEST_BUILD` replaces the `windows.h` dependency in the loader and in
`DllMain` with a hook the test drives. **No library logic is compiled out** —
`wincheck` is what guarantees the other side of that define still builds.

What none of this can settle is the short list of `[I]` items above: they need
the raw bytes of the image or a live comparison against the shipped DLL.
