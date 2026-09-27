// =============================================================================
//  GFSDK_Aftermath_DX11.h
// -----------------------------------------------------------------------------
//  Direct3D 11 convenience declarations.
//
//  In the original binary the DX11 and DX12 entry points are the same functions
//  with a different API selector argument:
//
//      GFSDK_Aftermath_DX12_Initialize()            -> Initialize(api = 1, ...)
//      GFSDK_Aftermath_DX11_Initialize()            -> Initialize(api = 0, ...)
//      GFSDK_Aftermath_DX12_CreateContextHandle()   -> CreateContextHandle(api = 1, ...)
//      GFSDK_Aftermath_DX11_CreateContextHandle()   -> CreateContextHandle(api = 0, ...)
//
//  The selector is stamped into every context handle that is handed out, which
//  is how the DLL later knows whether to route a call to the D3D11 or the D3D12
//  driver interface.
//
//  The DX11 flavour is limited to a single device and to the immediate context:
//  the driver interface returns
//  GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext for a deferred context.
// =============================================================================

#ifndef GFSDK_AFTERMATH_DX11_H
#define GFSDK_AFTERMATH_DX11_H

#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"

#ifdef __cplusplus
extern "C" {
#endif

// GFSDK_Aftermath_DX11_Initialize
//   pDevice is an ID3D11Device*.  Only one device may be initialized at a time;
//   a second (successful) initialization replaces the stored device pointer.
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX11_Initialize(
    GFSDK_Aftermath_Version      version,
    GFSDK_Aftermath_FeatureFlags flags,
    const void*                  pDevice);

// GFSDK_Aftermath_DX11_CreateContextHandle
//   pContext is an ID3D11DeviceContext* (the immediate context).
GFSDK_AFTERMATH_API GFSDK_Aftermath_Result GFSDK_Aftermath_DX11_CreateContextHandle(
    const void*                       pContext,
    GFSDK_Aftermath_ContextHandle*    pHandleOut);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GFSDK_AFTERMATH_DX11_H
