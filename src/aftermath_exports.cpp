// =============================================================================
//  aftermath_exports.cpp
// -----------------------------------------------------------------------------
//  The nine exported entry points.
//
//  These are exactly the symbols listed in src/exports.def.  Each one is a
//  forwarder that pins the API selector and hands over to aftermath_core.cpp;
//  keeping them separate makes the export surface auditable at a glance --
//  if it is not in this file, it is not exported.
//
//  ---------------------------------------------------------------------------
//  A note on the function names
//
//  The disassembler presents several of these as GFSDK_Aftermath_GetData_0,
//  GFSDK_Aftermath_GetDeviceStatus_0 and so on.  The trailing "_0" is an
//  artifact of the analysis database: when a symbol name that is about to be
//  created already exists in the database, IDA disambiguates by appending an
//  ordinal.  Those suffixes are NOT part of the export table and are stripped
//  here.  The real names are the ones in exports.def.
//  ---------------------------------------------------------------------------
// =============================================================================

#include "aftermath_internal.h"
#include "../include/GFSDK_Aftermath.h"

// =============================================================================
// Initialization
// =============================================================================

// [D] GFSDK_Aftermath_DX12_Initialize forwards with the API selector 1.
extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_DX12_Initialize(
    GFSDK_Aftermath_Version      version,
    GFSDK_Aftermath_FeatureFlags flags,
    const void*                  pDevice)
{
    return aftermath::Initialize(aftermath::Api_D3D12, version, flags, pDevice);
}

// [D] GFSDK_Aftermath_DX11_Initialize forwards with the API selector 0.
extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_DX11_Initialize(
    GFSDK_Aftermath_Version      version,
    GFSDK_Aftermath_FeatureFlags flags,
    const void*                  pDevice)
{
    return aftermath::Initialize(aftermath::Api_D3D11, version, flags, pDevice);
}

// =============================================================================
// Context handles
// =============================================================================

// [D] API selector 1.
extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_DX12_CreateContextHandle(
    const void*                       pContext,
    GFSDK_Aftermath_ContextHandle*    pHandleOut)
{
    return aftermath::CreateContextHandle(aftermath::Api_D3D12,
                                          pContext, pHandleOut);
}

// [D] API selector 0.
extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_DX11_CreateContextHandle(
    const void*                       pContext,
    GFSDK_Aftermath_ContextHandle*    pHandleOut)
{
    return aftermath::CreateContextHandle(aftermath::Api_D3D11,
                                          pContext, pHandleOut);
}

// [D] No API selector: the handle knows which API it belongs to.
extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_ReleaseContextHandle(GFSDK_Aftermath_ContextHandle handle)
{
    return aftermath::ReleaseContextHandle(handle);
}

// =============================================================================
// Marker data
// =============================================================================

// [D] Dispatches on the API word stored in the handle.
extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_SetEventMarker(
    GFSDK_Aftermath_ContextHandle handle,
    const void*                   markerData,
    uint32_t                      markerDataSize)
{
    return aftermath::SetEventMarker(handle, markerData, markerDataSize);
}

// [D] Per-context dispatch; see aftermath_core.cpp for the entry encoding.
extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_GetData(
    uint32_t                             numContexts,
    const GFSDK_Aftermath_ContextHandle* pContextHandles,
    GFSDK_Aftermath_ContextData*         pContextDataOut)
{
    return aftermath::GetData(numContexts, pContextHandles, pContextDataOut);
}

// =============================================================================
// Device level queries
// =============================================================================

extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_GetDeviceStatus(GFSDK_Aftermath_Device_Status* pStatus)
{
    return aftermath::GetDeviceStatus(pStatus);
}

extern "C" GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL
GFSDK_Aftermath_GetPageFaultInformation(
    GFSDK_Aftermath_PageFaultInformation* pPageFaultInfo)
{
    return aftermath::GetPageFaultInformation(pPageFaultInfo);
}
