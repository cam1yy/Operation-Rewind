// =============================================================================
//  GFSDK_Aftermath_DX12.h
// -----------------------------------------------------------------------------
//  Direct3D 12 convenience wrappers.
//
//  The real SDK splits the D3D12 specific entry points into this header so that
//  callers do not have to see the D3D12 types in GFSDK_Aftermath.h.  The DLL
//  itself takes untyped pointers (it never includes d3d12.h -- see
//  src/aftermath_core.cpp, where the device pointer is only ever forwarded to
//  the driver), so the inline wrappers below are all that is needed to restore
//  a source-compatible interface without pulling in the D3D headers.
// =============================================================================

#ifndef GFSDK_AFTERMATH_DX12_H
#define GFSDK_AFTERMATH_DX12_H

#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"

#ifdef __cplusplus
extern "C" {
#endif

// GFSDK_Aftermath_DX12_Initialize
//   Thin wrapper around the real entry point that performs the typed cast to
//   the void* the DLL expects.
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX12_Initialize(
    GFSDK_Aftermath_Version      version,
    GFSDK_Aftermath_FeatureFlags flags,
    const void*                  pDevice);

// GFSDK_Aftermath_DX12_CreateContextHandle
//   pContext is an ID3D12GraphicsCommandList* / ID3D12CommandList*.
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX12_CreateContextHandle(
    const void*                       pContext,
    GFSDK_Aftermath_ContextHandle*    pHandleOut);

// -----------------------------------------------------------------------------
// Notes
//
//   The D3D12 entry points are reached with plain pointers because the DLL
//   itself never includes <d3d12.h>: GFSDK_Aftermath_DX12_CreateContextHandle()
//   stores the ID3D12CommandList* verbatim and hands it to the driver.  The
//   typed convenience overloads that the real SDK header provides therefore
//   reduce to the declarations above under C, and to the inline wrappers below
//   under C++ when the D3D headers happen to be present.
//
//   GFSDK_Aftermath_Result_FAIL_GetDataOnBundle is returned by the driver when
//   the tracked D3D12 command list is a bundle; that distinction is made on the
//   driver side, not here.
// -----------------------------------------------------------------------------

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GFSDK_AFTERMATH_DX12_H
