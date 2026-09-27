# Symbol map

Every address is the RVA-with-preferred-base shown by the decompilers
(`0x180000000` image base). "File" is where the reconstruction lives.

## Code

### NVAPI bridge — `src/nvapi/nvapi_bridge.cpp`

| Address | NVAPI interface id | Reconstructed name | Notes |
|---|---|---|---|
| `0x180001000` | — | `BindModuleEntryPoints` | resolves `nvapi_QueryInterface` / `nvapi_pepQueryInterface`, calls `NvAPI_Initialize` (`0x0150E828`), installs the trace hooks (`0x33C7358C`, `0x593E8644`), clears the entry point cache |
| `0x1800010E0` | — | `LoadModule` | `nvapi64.dll` (id 0) or `nvpowerapi.dll` (id 1) through `NvLoadLibrary` |
| `0x180001180` | — | `EnsureModuleLoaded` | spins up to 10 × 100 ms while the module is being unloaded |
| `0x180001200` | `0x2926AAAD` | `SYS_GetDriverAndBranchVersion` | `NvAPI_SYS_GetDriverAndBranchVersion` |
| `0x1800012D0` | `0xC663BA92` | `D3D11_AftermathSetEventMarker` | |
| `0x1800013B0` | `0x96161ACB` | `D3D11_AftermathGetData` | |
| `0x1800014A0` | `0x1DE221DD` | `AftermathGetDeviceState` | primary |
| `0x180001570` | `0x8C68F0F1` | `D3D12_AftermathSetEventMarker` | |
| `0x180001650` | `0xB2E3E2A2` | `D3D12_AftermathGetData` | |
| `0x180001740` | `0x633D88E1` | `AftermathGetDeviceState2` | fallback for `0x1DE221DD` |
| `0x180001810` | `0xC99F4A67` | `D3D11_AftermathCreateHandle` | used for both the device and each context |
| `0x1800018E0` | `0xF1EA1980` | `D3D12_AftermathCreateHandle` | used for both the device and each command list |
| `0x1800019B0` | `0xCBA3F913` | `D3D11_AftermathEnableFeatures` | |
| `0x180001A70` | `0xDBE53CB2` | `D3D12_AftermathEnableFeatures` | |
| `0x180001B30` | `0x0BBA25D7` | `AftermathGetPageFaultInformation` | primary |
| `0x180001C00` | `0x6446BEB8` | `AftermathGetPageFaultInformation2` | fallback |

### StrSafe workers — `src/loader/nv_strsafe.cpp`

| Address | Reconstructed name |
|---|---|
| `0x180002070` | `StringCchCopyW` |
| `0x1800020F0` | `StringCbCopyW` |
| `0x180002180` | `StringCchCopyNW` |
| `0x180002210` | `StringCchCatW` |
| `0x1800022E0` | `StringCbCatW` |

### Path / registry helpers — `src/loader/nv_path_utils.cpp`

| Address | Reconstructed name |
|---|---|
| `0x180001CD0` | `BuildSystemDirectoryPath` |
| `0x180001DD0` | `PathContainsSeparator` |
| `0x180001E30` | `PathIsAbsolute` |
| `0x180001E90` | `DuplicateSystemDirectory` |
| `0x180001F00` | `FileNameMatchesModule` |
| `0x1800023B0` | `LoadLibraryFromSystemDirectory` |
| `0x180002420` | `BuildKnownFolderPath` |
| `0x1800025B0` | `IsPathInTrustedLocation` |
| `0x180002770` | `DuplicateString` |
| `0x180002800` | `DuplicateStringN` |
| `0x1800028A0` | `RegQueryValueAlloc` |
| `0x180002A10` | `RegQueryValueAllocEx` |
| `0x180002B70` | `IsWindowsBuildOrGreater` |
| `0x180002F80` | `EnsureDllExtension` |
| `0x1800039C0` | `LoadLibraryFromTrustedPath` |
| `0x180003D90` | `JoinPath` |

### Driver module discovery — `src/loader/nv_driver_store.cpp`

| Address | Reconstructed name |
|---|---|
| `0x180002C60` | `FindNvidiaDisplayDevice` |
| `0x1800030B0` | `FindServiceBinaryPath` |
| `0x1800032E0` | `FindDriverModuleViaService` |
| `0x1800035A0` | `FindDriverModuleInRegistryValues` |
| `0x1800037B0` | `FindDriverModuleByEnumeratingValues` |
| `0x180003A60` | `GetDriverStoreDirectory` |
| `0x180003EA0` | `GetDisplayDeviceDriverKeyPath` |
| `0x1800040D0` | `ResolveDriverModuleName` |
| `0x180004350` | `ResolveDriverModulePath` |

### Hardened loader entry — `src/loader/nv_module_loader.cpp`

| Address | Reconstructed name |
|---|---|
| `0x180004580` | `NvLoadLibrary` |

### Aftermath API — `src/aftermath_api.cpp`

| Address | Ordinal | Reconstructed name |
|---|---|---|
| `0x180004690` | — | `aftermath::Initialize` (shared worker) |
| `0x180004AC0` | — | `aftermath::CreateContextHandle` (shared worker) |
| `0x180004CC0` | 9 | `GFSDK_Aftermath_SetEventMarker` |
| `0x180004E40` | 5 | `GFSDK_Aftermath_GetData` |
| `0x1800051D0` | 6 | `GFSDK_Aftermath_GetDeviceStatus` |
| `0x1800053D0` | 7 | `GFSDK_Aftermath_GetPageFaultInformation` |
| `0x180005550` | 4 | `GFSDK_Aftermath_DX12_Initialize` |
| `0x180005570` | 2 | `GFSDK_Aftermath_DX11_Initialize` |
| `0x180005580` | 3 | `GFSDK_Aftermath_DX12_CreateContextHandle` |
| `0x180005590` | 1 | `GFSDK_Aftermath_DX11_CreateContextHandle` |
| `0x1800055A0` | 8 | `GFSDK_Aftermath_ReleaseContextHandle` |

### Entry point — `src/dllmain.cpp`

| Address | Reconstructed name |
|---|---|
| `0x18000639C` | `DllMain` (returns `TRUE`) |

### Not reconstructed (compiler supplied)

Everything else in the `.text` section belongs to the statically linked MSVC runtime and
comes back automatically when the project is linked:

`_DllMainCRTStartup`, `dllmain_dispatch`, `dllmain_raw`, `dllmain_crt_dispatch`,
`__scrt_common_main_seh`, `__scrt_initialize_crt`, `__scrt_acquire_startup_lock`,
`__scrt_dllmain_*`, `_initterm` / `_initterm_e`, `__acrt_*`, `__vcrt_*`, the heap and
locale glue, `memcpy` / `memset`, the `wcs*` family, `operator new`, `free`, and the
`/GS` cookie setup (`__security_init_cookie`, `__security_check_cookie`).

## Data

| Address | Reconstructed name | Notes |
|---|---|---|
| `0x180011348` | `kDevPkeyDeviceDriverInfPath` | 20 byte `DEVPROPKEY`; inferred to be `DEVPKEY_Device_DriverInfPath` from the `SetupGetInfDriverStoreLocationW` call that consumes it |
| `0x180011360` | `kDisplayDeviceInterfaceGuid` | 16 byte GUID; inferred to be `GUID_DISPLAY_DEVICE_ARRIVAL` |
| `0x180011370` | `L".dll"` | |
| `0x18001137C` | `L""` | compared one `WCHAR` at a time in `FileNameMatchesModule` |
| `0x180011380` | `L"\\"` | separator used by `JoinPath` / `BuildKnownFolderPath` |
| `0x1800118B4` | `L"nv"` | prefix test in `NvLoadLibrary` |
| `0x1800189F0` | `kIID_ID3D12DebugDevice` | 16 byte IID; inferred (see `docs/RECONSTRUCTION.md`) |
| `0x18001B000` | `g_isWindows7OrGreater` | the binary stores the inverse |
| `0x18001B998` | `g_queryInterface` | `nvapi64.dll!nvapi_QueryInterface` |
| `0x18001B9A0` | `g_onEnterEntryPoint` | NVAPI call trace hook |
| `0x18001B9A8` | `g_onLeaveEntryPoint` | NVAPI call trace hook |
| `0x18001B9B0` | `g_moduleHandles[5]` | |
| `0x18001B9D0` | `g_moduleUnloading[8]` | |
| `0x18001B9D8` | `g_activeCallCount` | `lock xadd` target |
| `0x18001B9E8` | `g_moduleLoaded[5]` | |
| `0x18001BA00` | `g_entryPointCache[1689]` | `0x34C8` bytes, cleared with one `memset` |
| `0x18001F10C` | `g_windowsBuildNumber` | parsed from `CurrentBuildNumber` |
| `0x18002029C` | `g_initializeGuard` | one-shot `InterlockedCompareExchange` |
| `0x1800202A0` | `g_deviceHandle` | NVAPI Aftermath handle for the device |
| `0x1800202A8` | `g_featureFlags` | |
| `0x1800202AC` | `g_initialized` | |
