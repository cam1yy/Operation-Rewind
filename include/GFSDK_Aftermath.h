// =============================================================================
//  GFSDK_Aftermath.h
// -----------------------------------------------------------------------------
//  Public interface of GFSDK_Aftermath_Lib.x64.dll.
//
//  Every function below corresponds to one exported symbol of the original
//  binary.  The mapping between public name and internal implementation is
//  documented in src/exports.def and in each implementation file.
//
//  Note on the export names: the disassembler appended a "_0" suffix to several
//  of the decompiled functions (GFSDK_Aftermath_GetData_0 and friends).  That
//  suffix is an artifact of the analysis database -- it disambiguates symbols
//  that collide with a name already present in the IDB -- and is NOT part of
//  the real export table.  The exports are emitted without it.
// =============================================================================

#ifndef GFSDK_AFTERMATH_H
#define GFSDK_AFTERMATH_H

#include "GFSDK_Aftermath_Defines.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Initialization
//
//   GFSDK_Aftermath_DX12_Initialize(version, flags, device)
//   GFSDK_Aftermath_DX11_Initialize(version, flags, device)
//
//   Both forward to the same implementation with the API selector 1 (DX12) or
//   0 (DX11) -- see src/aftermath_core.cpp.
//
//   Returns:
//     GFSDK_Aftermath_Result_Success                          on success
//     GFSDK_Aftermath_Result_FAIL_ApiError                    version != 0x13
//     GFSDK_Aftermath_Result_FAIL_InvalidParameter            null device
//     GFSDK_Aftermath_Result_FAIL_NotInitialized              driver interface missing
//     GFSDK_Aftermath_Result_FAIL_NvApiIncompatible           driver refused the interface
//     GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported   driver too old
//     GFSDK_Aftermath_Result_FAIL_DriverInitFailed            device init failed
// -----------------------------------------------------------------------------
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX12_Initialize(
    GFSDK_Aftermath_Version      version,
    GFSDK_Aftermath_FeatureFlags flags,
    const void*                  pDevice);   // ID3D12Device*

GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX11_Initialize(
    GFSDK_Aftermath_Version      version,
    GFSDK_Aftermath_FeatureFlags flags,
    const void*                  pDevice);   // ID3D11Device*

// -----------------------------------------------------------------------------
// Contexts
// -----------------------------------------------------------------------------

// Creates a handle that identifies pContext (an ID3D12CommandList* /
// ID3D11DeviceContext*) to the Aftermath API.
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX12_CreateContextHandle(
    const void*                       pContext,
    GFSDK_Aftermath_ContextHandle*    pHandleOut);

GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX11_CreateContextHandle(
    const void*                       pContext,
    GFSDK_Aftermath_ContextHandle*    pHandleOut);

GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_ReleaseContextHandle(
    GFSDK_Aftermath_ContextHandle     handle);

// -----------------------------------------------------------------------------
// Marker data
//
//   Associates 'markerData' (an application defined blob, at most
//   GFSDK_AFTERMATH_MARKER_DATA_SIZE bytes) with the given context.
//
//   Returns:
//     GFSDK_Aftermath_Result_Success                on success
//     GFSDK_Aftermath_Result_FAIL_NotInitialized    no successful Initialize()
//     GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled EnableMarkers was not requested
//     GFSDK_Aftermath_Result_FAIL_InvalidParameter  null context handle
// -----------------------------------------------------------------------------
#define GFSDK_AFTERMATH_MARKER_DATA_SIZE (6u * 1024u)

GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_SetEventMarker(
    GFSDK_Aftermath_ContextHandle handle,
    const void*                   markerData,
    uint32_t                      markerDataSize);

// -----------------------------------------------------------------------------
// Reading back the marker data of finished contexts.
//
//   numContexts entries of ppContextHandles are queried; the corresponding
//   result is written to each GFSDK_Aftermath_ContextData of pContextDataOut.
//
//   Returns:
//     GFSDK_Aftermath_Result_Success               on success
//     GFSDK_Aftermath_Result_FAIL_NotInitialized   no successful Initialize()
//     GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled
//     GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList
//                                                  the command list was never
//                                                  submitted
//     GFSDK_Aftermath_Result_FAIL_GetDataOnBundle  DX12 bundle
//     GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext
//                                                  DX11 deferred context
// -----------------------------------------------------------------------------
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_GetData(
    uint32_t                    numContexts,
    const GFSDK_Aftermath_ContextHandle* pContextHandles,
    GFSDK_Aftermath_ContextData* pContextDataOut);

// -----------------------------------------------------------------------------
// Device status / page fault information.
//
//   GFSDK_Aftermath_GetDeviceStatus()  requires EnableMarkers or
//                                      EnableResourceTracking to be active.
//   GFSDK_Aftermath_GetPageFaultInformation() requires
//                                      EnableResourceTracking.
// -----------------------------------------------------------------------------
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_GetDeviceStatus(
    GFSDK_Aftermath_Device_Status* pStatus);

GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_GetPageFaultInformation(
    GFSDK_Aftermath_PageFaultInformation* pPageFaultInfo);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GFSDK_AFTERMATH_H
