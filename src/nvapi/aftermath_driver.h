// =============================================================================
//  aftermath_driver.h
// -----------------------------------------------------------------------------
//  Typed wrappers over the NVAPI Aftermath interfaces.
//
//  Every public entry point of the DLL funnels into one of these.  They are the
//  functions that the recovery pass identified as the pair of thunks per
//  feature:
//
//      [D] set event marker   : D3D11 thunk, D3D12 thunk
//      [D] get data           : D3D11 thunk, D3D12 thunk
//      [D] get device status  : D3D11 thunk, D3D12 thunk
//      [D] page fault info    : ids 0x0BBA25D7 / 0x6446BEB8
//
//  The argument lists below are the ones visible at the call sites -- the thunk
//  bodies simply forward their parameters, so the arity and the types are
//  recovered even though the driver side contract itself is private.  The
//  FUNCTION POINTER TYPES are [I]: NVAPI returns an untyped void*, and the
//  original casts it inline at each call site.  Declaring the cast explicitly
//  here is what makes the reconstruction readable, and it is the only place
//  where a wrong guess would have to be corrected.
//
//  Return values are GFSDK_Aftermath_Result codes as reported by the driver.
//  The public layer maps them to the documented failure set (see
//  aftermath_api.cpp); the driver is free to return codes this build does not
//  know, which become GFSDK_Aftermath_Result_FAIL_Unknown.
// =============================================================================

#ifndef AFTERMATH_DRIVER_H
#define AFTERMATH_DRIVER_H

#include <stdint.h>
#include "../../include/GFSDK_Aftermath_Defines.h"
#include "../aftermath_internal.h"

namespace aftermath
{
    // -------------------------------------------------------------------------
    // DriverInitialize
    //
    //   Called once, after the feature flags have been recorded.  The driver
    //   attaches to the device and prepares its tracking resources.
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverInitialize(
        Api             api,
        void*           pDevice,
        uint32_t        version,
        uint32_t        flags);

    // -------------------------------------------------------------------------
    // DriverSetEventMarker
    //
    //   pContext is a raw D3D11 device context or D3D12 command list -- NOT a
    //   GFSDK_Aftermath_ContextHandle.  The handle's pDriverState field is
    //   passed alongside so the driver can keep its own per-context bookkeeping
    //   owned by the caller rather than by the driver.  [I]
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverSetEventMarker(
        Api             api,
        void*           pContext,
        void*           pDriverState,
        const void*     pMarkerData,
        uint32_t        markerDataSize);

    // -------------------------------------------------------------------------
    // DriverGetData
    //
    //   Reads back the marker data of one context.  On success the driver
    //   supplies the marker blob and its size; the caller (GetData) owns the
    //   result and fills in the status word.
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverGetData(
        Api             api,
        void*           pContext,
        void*           pDriverState,
        const void**    ppMarkerData,
        uint32_t*       pMarkerSize,
        uint32_t*       pContextStatus);

    // -------------------------------------------------------------------------
    // DriverGetDeviceStatus
    //
    //   pDeviceStatus receives the raw driver status, which
    //   GFSDK_Aftermath_GetDeviceStatus() then maps onto the public
    //   GFSDK_Aftermath_Device_Status enumeration.
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverGetDeviceStatus(
        Api             api,
        void*           pDevice,
        uint32_t*       pDeviceStatus);

    // -------------------------------------------------------------------------
    // DriverGetPageFaultInformation
    //
    //   pPageFaultInfo is the caller's GFSDK_Aftermath_PageFaultInformation*,
    //   handed to the driver untouched -- the library never dereferences it and
    //   therefore cannot know its layout.
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverGetPageFaultInformation(
        Api             api,
        void*           pDevice,
        void*           pPageFaultInfo);

    // -------------------------------------------------------------------------
    // DriverReleaseContext / DriverReleaseDevice
    //
    //   Release any per-context or per-device state the driver holds.  [I] the
    //   recovered GFSDK_Aftermath_ReleaseContextHandle() only frees the handle
    //   object itself, which implies the driver does not require an explicit
    //   per-context teardown call in this revision; these are provided for the
    //   shutdown path discovered in DllMain's counterpart.
    // -------------------------------------------------------------------------
    void DriverReleaseContext(Api api, void* pContext, void* pDriverState);
    void DriverReleaseDevice(Api api, void* pDevice);
}

#endif // AFTERMATH_DRIVER_H
