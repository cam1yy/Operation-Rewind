/*
 * nvapi_bridge.h -- late bound access to the NVIDIA display driver's NVAPI entry points.
 *
 * Reconstructed from sub_180001000 .. sub_180001C00.
 */

#ifndef GFSDK_AFTERMATH_NVAPI_BRIDGE_H
#define GFSDK_AFTERMATH_NVAPI_BRIDGE_H

#include "nvapi/nvapi_types.h"

namespace nvapi {

/*
 * Driver libraries the stub layer knows about.  Only kModuleNvApi64 is ever requested by
 * the Aftermath entry points; the nvpowerapi.dll branch comes from the shared NVAPI stub
 * generator and is kept so the reconstruction matches the binary.
 */
enum ModuleId
{
    kModuleNvApi64    = 0,   /* nvapi64.dll     */
    kModuleNvPowerApi = 1,   /* nvpowerapi.dll  */
    kModuleCount      = 5    /* size of the module tables at 0x18001B9B0 / 0x18001B9E8 */
};

/* sub_180001180 -- waits for a concurrent unload to finish, then loads on demand. */
NvAPI_Status EnsureModuleLoaded(ModuleId moduleId);

/* ---- generic ---------------------------------------------------------------------- */

/* sub_180001200 -- NvAPI_SYS_GetDriverAndBranchVersion (0x2926AAAD). */
NvAPI_Status SYS_GetDriverAndBranchVersion(NvU32* pDriverVersion, char* szBuildBranchString);

/* ---- Aftermath, D3D11 -------------------------------------------------------------- */

/* sub_180001810 (0xC99F4A67) -- creates a driver side handle for an ID3D11Device or an
 * ID3D11DeviceContext. */
NvAPI_Status D3D11_AftermathCreateHandle(void* pD3D11DeviceOrContext, NvAftermathContextHandle* pOutHandle);

/* sub_1800019B0 (0xCBA3F913) */
NvAPI_Status D3D11_AftermathEnableFeatures(NvAftermathContextHandle handle, NvU32 featureFlags);

/* sub_1800012D0 (0xC663BA92) */
NvAPI_Status D3D11_AftermathSetEventMarker(NvAftermathContextHandle handle, const void* pMarkerData, NvU32 markerSize);

/* sub_1800013B0 (0x96161ACB) */
NvAPI_Status D3D11_AftermathGetData(NvAftermathContextHandle handle, void** ppOutMarkerData, NvU32* pOutMarkerSize, NvU32* pOutStatus);

/* ---- Aftermath, D3D12 -------------------------------------------------------------- */

/* sub_1800018E0 (0xF1EA1980) -- ID3D12Device or ID3D12CommandList. */
NvAPI_Status D3D12_AftermathCreateHandle(void* pD3D12DeviceOrCommandList, NvAftermathContextHandle* pOutHandle);

/* sub_180001A70 (0xDBE53CB2) */
NvAPI_Status D3D12_AftermathEnableFeatures(NvAftermathContextHandle handle, NvU32 featureFlags);

/* sub_180001570 (0x8C68F0F1) */
NvAPI_Status D3D12_AftermathSetEventMarker(NvAftermathContextHandle handle, const void* pMarkerData, NvU32 markerSize);

/* sub_180001650 (0xB2E3E2A2) */
NvAPI_Status D3D12_AftermathGetData(NvAftermathContextHandle handle, void** ppOutMarkerData, NvU32* pOutMarkerSize, NvU32* pOutStatus);

/* ---- Aftermath, device wide -------------------------------------------------------- */

/* sub_1800014A0 (0x1DE221DD) and its fallback sub_180001740 (0x633D88E1). */
NvAPI_Status AftermathGetDeviceState(NvAftermathContextHandle handle, NvU32* pOutState);
NvAPI_Status AftermathGetDeviceState2(NvAftermathContextHandle handle, NvU32* pOutState);

/* sub_180001B30 (0x0BBA25D7) and its fallback sub_180001C00 (0x6446BEB8). */
NvAPI_Status AftermathGetPageFaultInformation(NvAftermathContextHandle handle, void* pOutPageFaultInformation);
NvAPI_Status AftermathGetPageFaultInformation2(NvAftermathContextHandle handle, void* pOutPageFaultInformation);

} /* namespace nvapi */

#endif /* GFSDK_AFTERMATH_NVAPI_BRIDGE_H */
