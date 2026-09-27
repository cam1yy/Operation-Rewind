// =============================================================================
//  aftermath_driver.h
// -----------------------------------------------------------------------------
//  Typed wrappers over the thirteen recovered NVAPI thunks.
//
//  Every public entry point of the DLL funnels into one of these.  The
//  signatures below are no longer guesses: each one comes from the call site
//  that the corresponding thunk is reached from, and the id each thunk passes
//  to nvapi_QueryInterface is listed in nvapi_ids.h.
//
//  Return values are DRIVER statuses (see aftermath::nvapi::DriverStatus), not
//  public GFSDK_Aftermath_Result codes.  The caller maps them with
//  MapDriverStatus(); a thunk returns DriverStatus_Unavailable (-3) when the
//  interface could not be resolved at all.
// =============================================================================

#ifndef AFTERMATH_DRIVER_H
#define AFTERMATH_DRIVER_H

#include <stdint.h>

#include "../../include/GFSDK_Aftermath_Defines.h"
#include "../aftermath_internal.h"

namespace aftermath
{
    // -------------------------------------------------------------------------
    // Attach
    //
    //   NVAPI_ID_D3D11_ATTACH / NVAPI_ID_D3D12_ATTACH.
    //
    //   Tells the driver about a D3D object and asks it for a handle:
    //
    //       status = Attach(api, pD3DObject, &driverHandle);
    //
    //   Called twice per object in the recovered code:
    //     * once from Initialize() with the device;
    //     * once from CreateContextHandle() with the command list / device
    //       context, which is the call that actually registers the context.
    //
    //   `pDriverHandleOut` is written on success.
    // -------------------------------------------------------------------------
    int32_t Attach(Api api, void* pD3DObject, void** pDriverHandleOut);

    // -------------------------------------------------------------------------
    // EnableFeatures
    //
    //   NVAPI_ID_D3D11_ENABLE_FEATURES / NVAPI_ID_D3D12_ENABLE_FEATURES.
    //   Initialize() only: turns the GFSDK_Aftermath_FeatureFlags mask into
    //   driver-side tracking.
    // -------------------------------------------------------------------------
    int32_t EnableFeatures(Api api, void* driverHandle, uint32_t featureFlags);

    // -------------------------------------------------------------------------
    // SetEventMarker
    //
    //   NVAPI_ID_*_SET_EVENT_MARKER.  Note the first argument: the DRIVER
    //   HANDLE taken from the context handle, not the D3D context pointer.
    //   There is no size limit and no null-marker test in the recovered code.
    // -------------------------------------------------------------------------
    int32_t SetEventMarker(Api      api,
                           void*    driverHandle,
                           const void* pMarkerData,
                           uint32_t markerDataSize);

    // -------------------------------------------------------------------------
    // GetData
    //
    //   NVAPI_ID_*_GET_DATA.  The driver writes the marker blob pointer, its
    //   size and a GFSDK_Aftermath_Context_Status word directly into the
    //   caller's context-data entry:
    //
    //       status = GetData(api, driverHandle,
    //                        &entry.markerData, &entry.markerSize, &entry.status);
    // -------------------------------------------------------------------------
    int32_t GetData(Api      api,
                    void*    driverHandle,
                    void**   ppMarkerData,
                    uint32_t* pMarkerSize,
                    uint32_t* pContextStatus);

    // -------------------------------------------------------------------------
    // GetDeviceStatus
    //
    //   Tries NVAPI_ID_D3D11_DEVICE_STATUS first and falls back to
    //   NVAPI_ID_D3D12_DEVICE_STATUS.  Returns whichever status the driver
    //   produced; see GetDeviceStatus() in aftermath_core.cpp for how the two
    //   are combined.
    // -------------------------------------------------------------------------
    int32_t GetDeviceStatusPrimary(void* driverHandle, uint32_t* pStatusOut);
    int32_t GetDeviceStatusFallback(void* driverHandle, uint32_t* pStatusOut);

    // -------------------------------------------------------------------------
    // GetPageFaultInformation
    //
    //   Same primary/fallback arrangement with
    //   NVAPI_ID_PAGE_FAULT_PRIMARY / NVAPI_ID_PAGE_FAULT_FALLBACK.  The second
    //   call is only made when the first reports a failure.
    // -------------------------------------------------------------------------
    int32_t GetPageFaultInformationPrimary(void* driverHandle, void* pInfoOut);
    int32_t GetPageFaultInformationFallback(void* driverHandle, void* pInfoOut);
}

#endif // AFTERMATH_DRIVER_H
