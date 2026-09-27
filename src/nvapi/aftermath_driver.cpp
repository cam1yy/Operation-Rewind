// =============================================================================
//  aftermath_driver.cpp
// -----------------------------------------------------------------------------
//  Implementation of the NVAPI Aftermath thunks.
//
//  Each function below is a direct translation of one recovered thunk.  The
//  pattern is always the same three steps:
//
//      1. obtain the driver entry point through nvapi::QueryInterface(id)
//      2. if it is null, report GFSDK_Aftermath_Result_FAIL_NotInitialized
//      3. otherwise cast it to the observed signature and call it
//
//  The cast is the [I] part; see the header.  When a call site in the original
//  listing shows a different argument list, only the corresponding typedef here
//  needs to change.
// =============================================================================

#include "aftermath_driver.h"
#include "nvapi_ids.h"
#include "nvapi_loader.h"

namespace aftermath
{
    // -------------------------------------------------------------------------
    // Function pointer types.
    //
    //   The driver interfaces all follow the C convention (__cdecl): nvapi64.dll
    //   is a plain C DLL and its private entry points do not use __stdcall on
    //   x64 (where the distinction is moot anyway).  [I]
    // -------------------------------------------------------------------------
    typedef GFSDK_Aftermath_Result(AFTERMATH_CDECL * DriverInitializeFn)(
        void* pDevice, uint32_t version, uint32_t flags);

    typedef GFSDK_Aftermath_Result(AFTERMATH_CDECL * DriverSetEventMarkerFn)(
        void* pContext, void* pDriverState, const void* pMarkerData,
        uint32_t markerDataSize);

    typedef GFSDK_Aftermath_Result(AFTERMATH_CDECL * DriverGetDataFn)(
        void* pContext, void* pDriverState, const void** ppMarkerData,
        uint32_t* pMarkerSize, uint32_t* pContextStatus);

    typedef GFSDK_Aftermath_Result(AFTERMATH_CDECL * DriverGetDeviceStatusFn)(
        void* pDevice, uint32_t* pDeviceStatus);

    typedef GFSDK_Aftermath_Result(AFTERMATH_CDECL * DriverGetPageFaultInfoFn)(
        void* pDevice, void* pPageFaultInfo);

    // -------------------------------------------------------------------------
    // Feature -> id selection.
    //
    //   This is the one place where the D3D11 / D3D12 split is resolved.  Every
    //   public entry point reaches the driver through here, which is why the
    //   per-feature id pairs in nvapi_ids.h can be corrected in isolation.
    // -------------------------------------------------------------------------
    static uint32_t SelectId(Api api, uint32_t idD3D11, uint32_t idD3D12)
    {
        return (api == Api_D3D12) ? idD3D12 : idD3D11;
    }

    // -------------------------------------------------------------------------
    // DriverInitialize
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverInitialize(
        Api      api,
        void*    pDevice,
        uint32_t version,
        uint32_t flags)
    {
        const uint32_t id = SelectId(api,
                                     nvapi::NVAPI_ID_D3D11_INITIALIZE,
                                     nvapi::NVAPI_ID_D3D12_INITIALIZE);

        DriverInitializeFn fn =
            reinterpret_cast<DriverInitializeFn>(nvapi::QueryInterface(id));

        if (fn == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        return fn(pDevice, version, flags);
    }

    // -------------------------------------------------------------------------
    // DriverSetEventMarker
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverSetEventMarker(
        Api         api,
        void*       pContext,
        void*       pDriverState,
        const void* pMarkerData,
        uint32_t    markerDataSize)
    {
        const uint32_t id = SelectId(api,
                                     nvapi::NVAPI_ID_D3D11_SET_EVENT_MARKER,
                                     nvapi::NVAPI_ID_D3D12_SET_EVENT_MARKER);

        DriverSetEventMarkerFn fn =
            reinterpret_cast<DriverSetEventMarkerFn>(nvapi::QueryInterface(id));

        if (fn == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        return fn(pContext, pDriverState, pMarkerData, markerDataSize);
    }

    // -------------------------------------------------------------------------
    // DriverGetData
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverGetData(
        Api          api,
        void*        pContext,
        void*        pDriverState,
        const void** ppMarkerData,
        uint32_t*    pMarkerSize,
        uint32_t*    pContextStatus)
    {
        const uint32_t id = SelectId(api,
                                     nvapi::NVAPI_ID_D3D11_GET_DATA,
                                     nvapi::NVAPI_ID_D3D12_GET_DATA);

        DriverGetDataFn fn =
            reinterpret_cast<DriverGetDataFn>(nvapi::QueryInterface(id));

        if (fn == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        return fn(pContext, pDriverState, ppMarkerData, pMarkerSize,
                  pContextStatus);
    }

    // -------------------------------------------------------------------------
    // DriverGetDeviceStatus
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverGetDeviceStatus(
        Api       api,
        void*     pDevice,
        uint32_t* pDeviceStatus)
    {
        const uint32_t id = SelectId(api,
                                     nvapi::NVAPI_ID_D3D11_GET_DEVICE_STATUS,
                                     nvapi::NVAPI_ID_D3D12_GET_DEVICE_STATUS);

        DriverGetDeviceStatusFn fn =
            reinterpret_cast<DriverGetDeviceStatusFn>(nvapi::QueryInterface(id));

        if (fn == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        return fn(pDevice, pDeviceStatus);
    }

    // -------------------------------------------------------------------------
    // DriverGetPageFaultInformation
    //
    //   Note that this feature is gated on the EnableResourceTracking flag by
    //   the caller, and that the two ids here are the ones the recovery pass
    //   tied specifically to the page-fault path.  [D]
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result DriverGetPageFaultInformation(
        Api   api,
        void* pDevice,
        void* pPageFaultInfo)
    {
        const uint32_t id = SelectId(api,
                                     nvapi::NVAPI_ID_D3D11_GET_PAGE_FAULT_INFO,
                                     nvapi::NVAPI_ID_D3D12_GET_PAGE_FAULT_INFO);

        DriverGetPageFaultInfoFn fn =
            reinterpret_cast<DriverGetPageFaultInfoFn>(nvapi::QueryInterface(id));

        if (fn == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        return fn(pDevice, pPageFaultInfo);
    }

    // -------------------------------------------------------------------------
    // DriverReleaseContext / DriverReleaseDevice
    //
    //   No driver entry point for teardown was identified in the recovery pass.
    //   The handle object owns nothing but its own storage (see
    //   GFSDK_Aftermath_ContextHandleImpl), so releasing is a no-op here and
    //   these functions exist only so the shutdown path has a single place to
    //   grow into if the real teardown call is identified later.  [I]
    // -------------------------------------------------------------------------
    void DriverReleaseContext(Api /*api*/, void* /*pContext*/,
                              void* /*pDriverState*/)
    {
    }

    void DriverReleaseDevice(Api /*api*/, void* /*pDevice*/)
    {
    }
}
