// =============================================================================
//  aftermath_driver.cpp
// -----------------------------------------------------------------------------
//  Implementation of the NVAPI Aftermath thunks.
//
//  Every thunk in the original has the same eleven-instruction shape, and the
//  helper below reproduces it exactly:
//
//      ++dword_18001B9D8;                       // driver call refcount
//      status = EnsureLoaded(0);
//      if ( status == 0 )
//      {
//          fn = cached[id];
//          if ( fn == 0 )
//          {
//              fn = queryFunction(id);          // only if the query function exists
//              cached[id] = fn;
//          }
//          if ( fn == 0 ) status = -3;          // DriverStatus_Unavailable
//          else
//          {
//              token = 0;
//              if ( traceBegin ) traceBegin(id, &token);
//              status = fn(...the thunk's own arguments...);
//              if ( traceEnd ) traceEnd(id, token, status);
//          }
//      }
//      --dword_18001B9D8;
//      return status;
//
//  The per-thunk differences are only the id and the argument list, which is
//  why thirteen near-identical bodies is all the image contains.
// =============================================================================

#include "aftermath_driver.h"
#include "nvapi_ids.h"
#include "nvapi_loader.h"

namespace aftermath
{
    // -------------------------------------------------------------------------
    // Thunk epilogue shared by all thirteen entry points.
    // -------------------------------------------------------------------------
    static void EndThunk(uint32_t id, void* token, int32_t status)
    {
        nvapi::TraceEndFn traceEnd = nvapi::GetTraceEnd();
        if (traceEnd != nullptr)
        {
            traceEnd(id, token, status);
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
    }

    // -------------------------------------------------------------------------
    // Attach
    // -------------------------------------------------------------------------
    int32_t Attach(Api api, void* pD3DObject, void** pDriverHandleOut)
    {
        const uint32_t id = (api == Api_D3D12) ? nvapi::NVAPI_ID_D3D12_ATTACH
                                               : nvapi::NVAPI_ID_D3D11_ATTACH;

        typedef int32_t(AFTERMATH_CDECL * AttachFn)(void* pD3DObject,
                                                    void** pDriverHandleOut);

        AFTERMATH_INTERLOCKED_INC(&g_driverRefCount);

        int32_t status = nvapi::EnsureLoaded();

        if (status != 0)
        {
            AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
            return status;
        }

        AttachFn fn = reinterpret_cast<AttachFn>(nvapi::ResolveInterface(id));

        if (fn == nullptr)
        {
            status = nvapi::DriverStatus_Unavailable;
        }
        else
        {
            void* token = nullptr;

            nvapi::TraceBeginFn traceBegin = nvapi::GetTraceBegin();
            if (traceBegin != nullptr)
            {
                traceBegin(id, &token);
            }

            status = fn(pD3DObject, pDriverHandleOut);

            EndThunk(id, token, status);
            return status;
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
        return status;
    }

    // -------------------------------------------------------------------------
    // EnableFeatures
    // -------------------------------------------------------------------------
    int32_t EnableFeatures(Api api, void* driverHandle, uint32_t featureFlags)
    {
        const uint32_t id = (api == Api_D3D12) ? nvapi::NVAPI_ID_D3D12_ENABLE_FEATURES
                                               : nvapi::NVAPI_ID_D3D11_ENABLE_FEATURES;

        typedef int32_t(AFTERMATH_CDECL * EnableFn)(void* driverHandle,
                                                    uint32_t featureFlags);

        AFTERMATH_INTERLOCKED_INC(&g_driverRefCount);

        int32_t status = nvapi::EnsureLoaded();

        if (status != 0)
        {
            AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
            return status;
        }

        EnableFn fn = reinterpret_cast<EnableFn>(nvapi::ResolveInterface(id));

        if (fn == nullptr)
        {
            status = nvapi::DriverStatus_Unavailable;
        }
        else
        {
            void* token = nullptr;

            nvapi::TraceBeginFn traceBegin = nvapi::GetTraceBegin();
            if (traceBegin != nullptr)
            {
                traceBegin(id, &token);
            }

            status = fn(driverHandle, featureFlags);

            EndThunk(id, token, status);
            return status;
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
        return status;
    }

    // -------------------------------------------------------------------------
    // SetEventMarker
    // -------------------------------------------------------------------------
    int32_t SetEventMarker(Api api,
                           void* driverHandle,
                           const void* pMarkerData,
                           uint32_t markerDataSize)
    {
        const uint32_t id = (api == Api_D3D12) ? nvapi::NVAPI_ID_D3D12_SET_EVENT_MARKER
                                               : nvapi::NVAPI_ID_D3D11_SET_EVENT_MARKER;

        typedef int32_t(AFTERMATH_CDECL * SetMarkerFn)(void* driverHandle,
                                                       const void* pMarkerData,
                                                       uint32_t markerDataSize);

        AFTERMATH_INTERLOCKED_INC(&g_driverRefCount);

        int32_t status = nvapi::EnsureLoaded();

        if (status != 0)
        {
            AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
            return status;
        }

        SetMarkerFn fn = reinterpret_cast<SetMarkerFn>(nvapi::ResolveInterface(id));

        if (fn == nullptr)
        {
            status = nvapi::DriverStatus_Unavailable;
        }
        else
        {
            void* token = nullptr;

            nvapi::TraceBeginFn traceBegin = nvapi::GetTraceBegin();
            if (traceBegin != nullptr)
            {
                traceBegin(id, &token);
            }

            status = fn(driverHandle, pMarkerData, markerDataSize);

            EndThunk(id, token, status);
            return status;
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
        return status;
    }

    // -------------------------------------------------------------------------
    // GetData
    // -------------------------------------------------------------------------
    int32_t GetData(Api api,
                    void* driverHandle,
                    void** ppMarkerData,
                    uint32_t* pMarkerSize,
                    uint32_t* pContextStatus)
    {
        const uint32_t id = (api == Api_D3D12) ? nvapi::NVAPI_ID_D3D12_GET_DATA
                                               : nvapi::NVAPI_ID_D3D11_GET_DATA;

        typedef int32_t(AFTERMATH_CDECL * GetDataFn)(void* driverHandle,
                                                     void** ppMarkerData,
                                                     uint32_t* pMarkerSize,
                                                     uint32_t* pContextStatus);

        AFTERMATH_INTERLOCKED_INC(&g_driverRefCount);

        int32_t status = nvapi::EnsureLoaded();

        if (status != 0)
        {
            AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
            return status;
        }

        GetDataFn fn = reinterpret_cast<GetDataFn>(nvapi::ResolveInterface(id));

        if (fn == nullptr)
        {
            status = nvapi::DriverStatus_Unavailable;
        }
        else
        {
            void* token = nullptr;

            nvapi::TraceBeginFn traceBegin = nvapi::GetTraceBegin();
            if (traceBegin != nullptr)
            {
                traceBegin(id, &token);
            }

            status = fn(driverHandle, ppMarkerData, pMarkerSize, pContextStatus);

            EndThunk(id, token, status);
            return status;
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
        return status;
    }

    // -------------------------------------------------------------------------
    // GetDeviceStatus -- primary and fallback entries.
    //
    //   These two share a signature; only the id differs.  GetDeviceStatus() in
    //   aftermath_core.cpp calls the primary one and, if it fails, the fallback.
    // -------------------------------------------------------------------------
    static int32_t CallDeviceStatus(uint32_t id, void* driverHandle, uint32_t* pStatusOut)
    {
        typedef int32_t(AFTERMATH_CDECL * DeviceStatusFn)(void* driverHandle,
                                                          uint32_t* pStatusOut);

        AFTERMATH_INTERLOCKED_INC(&g_driverRefCount);

        int32_t status = nvapi::EnsureLoaded();

        if (status != 0)
        {
            AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
            return status;
        }

        DeviceStatusFn fn = reinterpret_cast<DeviceStatusFn>(nvapi::ResolveInterface(id));

        if (fn == nullptr)
        {
            status = nvapi::DriverStatus_Unavailable;
        }
        else
        {
            void* token = nullptr;

            nvapi::TraceBeginFn traceBegin = nvapi::GetTraceBegin();
            if (traceBegin != nullptr)
            {
                traceBegin(id, &token);
            }

            status = fn(driverHandle, pStatusOut);

            EndThunk(id, token, status);
            return status;
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
        return status;
    }

    int32_t GetDeviceStatusPrimary(void* driverHandle, uint32_t* pStatusOut)
    {
        return CallDeviceStatus(nvapi::NVAPI_ID_D3D11_DEVICE_STATUS,
                                driverHandle, pStatusOut);
    }

    int32_t GetDeviceStatusFallback(void* driverHandle, uint32_t* pStatusOut)
    {
        return CallDeviceStatus(nvapi::NVAPI_ID_D3D12_DEVICE_STATUS,
                                driverHandle, pStatusOut);
    }

    // -------------------------------------------------------------------------
    // GetPageFaultInformation -- primary and fallback entries.
    // -------------------------------------------------------------------------
    static int32_t CallPageFault(uint32_t id, void* driverHandle, void* pInfoOut)
    {
        typedef int32_t(AFTERMATH_CDECL * PageFaultFn)(void* driverHandle,
                                                       void* pInfoOut);

        AFTERMATH_INTERLOCKED_INC(&g_driverRefCount);

        int32_t status = nvapi::EnsureLoaded();

        if (status != 0)
        {
            AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
            return status;
        }

        PageFaultFn fn = reinterpret_cast<PageFaultFn>(nvapi::ResolveInterface(id));

        if (fn == nullptr)
        {
            status = nvapi::DriverStatus_Unavailable;
        }
        else
        {
            void* token = nullptr;

            nvapi::TraceBeginFn traceBegin = nvapi::GetTraceBegin();
            if (traceBegin != nullptr)
            {
                traceBegin(id, &token);
            }

            status = fn(driverHandle, pInfoOut);

            EndThunk(id, token, status);
            return status;
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
        return status;
    }

    int32_t GetPageFaultInformationPrimary(void* driverHandle, void* pInfoOut)
    {
        return CallPageFault(nvapi::NVAPI_ID_PAGE_FAULT_PRIMARY, driverHandle, pInfoOut);
    }

    int32_t GetPageFaultInformationFallback(void* driverHandle, void* pInfoOut)
    {
        return CallPageFault(nvapi::NVAPI_ID_PAGE_FAULT_FALLBACK, driverHandle, pInfoOut);
    }
}
