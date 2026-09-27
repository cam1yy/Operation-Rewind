// =============================================================================
//  aftermath_core.cpp
// -----------------------------------------------------------------------------
//  The real implementation behind the exported entry points.
//
//  Every public function in aftermath_exports.cpp is a two line forwarder that
//  pins the API selector; all of the logic lives here.
//
//  The five functions below correspond to the recovered implementations:
//
//      Initialize            -- DX11/DX12 shared device initialisation
//      CreateContextHandle   -- DX11/DX12 shared handle allocation
//      SetEventMarker        -- dispatch on handle->api
//      GetData               -- per-context dispatch on handle->api
//      GetDeviceStatus       -- driver status translation
//      GetPageFaultInformation
// =============================================================================

#include "aftermath_internal.h"
#include "nvapi/aftermath_driver.h"
#include "nvapi/nvapi_ids.h"
#include "nvapi/nvapi_loader.h"

#include <stdlib.h>     // malloc / free
#include <string.h>     // memset

namespace aftermath
{
    // =========================================================================
    // Driver status -> public result code
    // =========================================================================
    //
    // [D] Reproduced verbatim from the recovery pass.  The switch was seen on a
    //     small negative driver-side status value and produced these pairs:
    //
    //         -0x86, -0x68,  -3   ->  0xBAD00007  FAIL_NvApiIncompatible
    //         -0x85, -0x83,  -1   ->  0xBAD00000  Fail
    //         -0x84               ->  0xBAD00008  FAIL_GettingContextDataWithNewCommandList
    //         -0x82               ->  0xBAD0000D  FAIL_OutOfMemory
    //         -5                  ->  0xBAD00004  FAIL_InvalidParameter
    //         default             ->  0xBAD00005  FAIL_Unknown
    //
    //     The literal case values are quoted as they were recovered.  Note that
    //     -0x85 (-133) and -0x83 (-131) share a case body with -1 while
    //     -0x84 (-132) sits between them, which is characteristic of a
    //     generated dispatch table over a driver status enumeration rather than
    //     of hand written code.
    //
    // Where this mapping is applied is [I]: it is the translation used whenever
    // a driver call has to be turned into a public result.  See
    // docs/ANALYSIS_NOTES.md, "Open questions", item 4.
    // =========================================================================
    static GFSDK_Aftermath_Result MapDriverStatus(int32_t driverStatus)
    {
        switch (driverStatus)
        {
        case -0x86:
        case -0x68:
        case -3:
            return GFSDK_Aftermath_Result_FAIL_NvApiIncompatible;

        case -0x85:
        case -0x83:
        case -1:
            return GFSDK_Aftermath_Result_Fail;

        case -0x84:
            return GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList;

        case -0x82:
            return GFSDK_Aftermath_Result_FAIL_OutOfMemory;

        case -5:
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;

        default:
            return GFSDK_Aftermath_Result_FAIL_Unknown;
        }
    }

    // =========================================================================
    // Driver result -> public result code
    // =========================================================================
    //
    // The driver answers with one of two different kinds of code, and the two
    // must not be confused:
    //
    //   * a GFSDK_Aftermath_Result value, i.e. anything in the 0xBAD00000 range.
    //     These are handed through untouched.  This is the case that produces
    //     the per-context sentinels observed in GetData (0xBAD0000F, 0xBAD0000E,
    //     0xBAD00004): the driver reports the condition and the library stores
    //     the code verbatim.
    //
    //   * a small negative driver-internal status, which has no meaning outside
    //     the driver and is translated by MapDriverStatus().
    //
    // Distinguishing on the 0xBAD0 prefix is what lets both cases coexist.  [I]
    // =========================================================================
    static GFSDK_Aftermath_Result TranslateDriverResult(uint32_t driverResult)
    {
        if ((driverResult & 0xFFFF0000u) == 0xBAD00000u)
        {
            return static_cast<GFSDK_Aftermath_Result>(driverResult);
        }

        return MapDriverStatus(static_cast<int32_t>(driverResult));
    }

    // =========================================================================
    // Initialize     [D] the DX11 and DX12 exports both forward here
    // =========================================================================
    //
    // Order of checks recovered from the two export bodies:
    //
    //   1. version must be exactly GFSDK_Aftermath_Version_API (0x13),
    //      otherwise FAIL_ApiError;
    //   2. NVAPI must be present, otherwise FAIL_NotInitialized;
    //   3. the driver interface version must be >= NVAPI_MIN_INTERFACE_VERSION,
    //      otherwise FAIL_DriverVersionNotSupported;
    //   4. the driver is asked to attach to the device; a failure is mapped
    //      through MapDriverStatus().
    //
    // The null-device check is [I]: every other entry point in the image tests
    // its pointer arguments before use, so this one is expected to as well.
    // =========================================================================
    GFSDK_Aftermath_Result Initialize(
        Api                          api,
        GFSDK_Aftermath_Version      version,
        GFSDK_Aftermath_FeatureFlags flags,
        const void*                  pDevice)
    {
        // (1) [D] the immediate 19 / 0x13 is compared against the version
        //     argument, and the failure code produced is FAIL_ApiError.
        if (version != GFSDK_Aftermath_Version_API)
        {
            return GFSDK_Aftermath_Result_FAIL_ApiError;
        }

        // [I] pointer validation
        if (pDevice == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // (2) [D] resolving NVAPI is a one-time operation guarded by the
        //     interlock at 0x18002029C.
        if (!nvapi::EnsureLoaded())
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        // (3) [D] the interface version is read through NVAPI and compared
        //     against 0x9784; below that the driver is too old.
        if (nvapi::GetInterfaceVersion() < nvapi::NVAPI_MIN_INTERFACE_VERSION)
        {
            return GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported;
        }

        // (4) [D] hand the device to the driver.
        const GFSDK_Aftermath_Result driverResult =
            DriverInitialize(api, const_cast<void*>(pDevice),
                             static_cast<uint32_t>(version),
                             static_cast<uint32_t>(flags));

        if (!AftermathSucceeded(driverResult))
        {
            return TranslateDriverResult(static_cast<uint32_t>(driverResult));
        }

        // [D] publish the state.  Note that a repeated successful Initialize()
        //     overwrites the previous device rather than failing; the recovered
        //     code has no FAIL_AlreadyInitialized path.
        g_pDevice      = const_cast<void*>(pDevice);
        g_featureFlags = static_cast<uint32_t>(flags);
        g_activeApi    = api;
        g_initialized  = true;

        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // CreateContextHandle
    // =========================================================================
    //
    // [D] the allocation and the field stores were recovered exactly:
    //
    //       handle = operator new(0x18);
    //       *(uint32_t*)(handle + 4)  = api;
    //       *(void**)(handle + 8)     = pContext;
    //       *(void**)(handle + 16)    = nullptr;
    //
    // The handle is deliberately not registered with the driver at this point:
    // the driver side state at +16 stays null until the first call that needs
    // it.  [I]
    // =========================================================================
    GFSDK_Aftermath_Result CreateContextHandle(
        Api                             api,
        const void*                     pContext,
        GFSDK_Aftermath_ContextHandle*  pHandleOut)
    {
        if (!g_initialized)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        if (pContext == nullptr || pHandleOut == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        GFSDK_Aftermath_ContextHandleImpl* handle =
            static_cast<GFSDK_Aftermath_ContextHandleImpl*>(
                ::malloc(sizeof(GFSDK_Aftermath_ContextHandleImpl)));

        if (handle == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_OutOfMemory;
        }

        // [D] the first word is never initialised by the original either: the
        //     allocation comes from a non-zeroing allocator and only +4, +8 and
        //     +16 are written.  memset() here keeps the reconstruction
        //     deterministic without changing the observable layout.
        ::memset(handle, 0, sizeof(*handle));

        handle->api         = static_cast<uint32_t>(api);
        handle->pContext    = const_cast<void*>(pContext);
        handle->pDriverState = nullptr;

        *pHandleOut = handle;

        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // ReleaseContextHandle
    // =========================================================================
    //
    // [D] the exported wrapper checks the initialized flag and the null handle,
    //     then frees the object.  No driver call is made, which is why the
    //     driver must not own anything reachable only through the handle.
    // =========================================================================
    GFSDK_Aftermath_Result ReleaseContextHandle(GFSDK_Aftermath_ContextHandle handle)
    {
        if (!g_initialized)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        if (handle == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        ::free(handle);

        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // SetEventMarker
    // =========================================================================
    //
    // [D] the feature gate is bit 0 of the stored flags:
    //
    //       if ( (dword_1800202A8 & 1) == 0 )
    //           return 0xBAD00010;      // FAIL_FeatureNotEnabled
    //
    // [D] the dispatch is on the API word inside the handle, at offset +4:
    //       0 -> the D3D11 driver entry point
    //       1 -> the D3D12 driver entry point
    // =========================================================================
    GFSDK_Aftermath_Result SetEventMarker(
        GFSDK_Aftermath_ContextHandle handle,
        const void*                   markerData,
        uint32_t                      markerDataSize)
    {
        if (!g_initialized)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        if (handle == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // [D] EnableMarkers gate
        if ((g_featureFlags & GFSDK_Aftermath_FeatureFlags_EnableMarkers) == 0)
        {
            return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
        }

        if (markerData == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // [I] The published SDK documents a 6 KiB upper bound on the marker
        //     payload and rejects anything larger with FAIL_InvalidParameter.
        //     The bound itself is not an immediate that the recovery pass could
        //     confirm for this revision.
        if (markerDataSize > GFSDK_AFTERMATH_MARKER_DATA_SIZE)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        const GFSDK_Aftermath_Result driverResult =
            DriverSetEventMarker(static_cast<Api>(handle->api),
                                 handle->pContext,
                                 handle->pDriverState,
                                 markerData,
                                 markerDataSize);

        if (!AftermathSucceeded(driverResult))
        {
            return TranslateDriverResult(static_cast<uint32_t>(driverResult));
        }

        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // GetData
    // =========================================================================
    //
    // [D] The call is an array query: numContexts handles in, numContexts
    //     GFSDK_Aftermath_ContextData out, 16 bytes apart, each dispatched on
    //     its own handle's API word.
    //
    // [D] A per-context failure does NOT fail the call.  Instead the entry is
    //     marked with:
    //
    //         entry.status     = 3          (GFSDK_Aftermath_Context_Status_Invalid)
    //         entry.markerData = <result>   (the 32-bit result code, widened into
    //                                        the pointer sized slot)
    //
    //     which is why the recovered code shows the result codes as the negative
    //     immediates -0x452FFFF1, -0x452FFFF2 and -0x452FFFFC.  Widened to
    //     32 bits those are:
    //
    //         0xBAD0000F  FAIL_GetDataOnDeferredContext   (D3D11 deferred context)
    //         0xBAD0000E  FAIL_GetDataOnBundle            (D3D12 bundle)
    //         0xBAD00004  FAIL_InvalidParameter           (null handle)
    //
    //     That the three literals decode onto exactly the three documented
    //     per-context failure modes is what identifies this function.
    //
    // [D] The feature gate is again bit 0 (EnableMarkers).
    // =========================================================================
    static void SetContextDataFailure(
        GFSDK_Aftermath_ContextData* entry,
        GFSDK_Aftermath_Result       result)
    {
        // [D] the marker pointer slot carries the result code
        entry->markerData = reinterpret_cast<void*>(
            static_cast<intptr_t>(static_cast<int32_t>(result)));
        entry->markerSize = 0;
        entry->status     = GFSDK_Aftermath_Context_Status_Invalid;
    }

    GFSDK_Aftermath_Result GetData(
        uint32_t                             numContexts,
        const GFSDK_Aftermath_ContextHandle* pContextHandles,
        GFSDK_Aftermath_ContextData*         pContextDataOut)
    {
        if (!g_initialized)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        if (pContextHandles == nullptr || pContextDataOut == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // [D] EnableMarkers gate
        if ((g_featureFlags & GFSDK_Aftermath_FeatureFlags_EnableMarkers) == 0)
        {
            return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
        }

        for (uint32_t i = 0; i < numContexts; ++i)
        {
            GFSDK_Aftermath_ContextData& entry = pContextDataOut[i];
            const GFSDK_Aftermath_ContextHandle handle = pContextHandles[i];

            // [D] null handle -> FAIL_InvalidParameter encoded in the entry
            //     (the -0x452FFFFC literal).
            if (handle == nullptr)
            {
                SetContextDataFailure(
                    &entry, GFSDK_Aftermath_Result_FAIL_InvalidParameter);
                continue;
            }

            const void*   pMarkerData     = nullptr;
            uint32_t      markerSize      = 0;
            uint32_t      contextStatus   = static_cast<uint32_t>(
                GFSDK_Aftermath_Context_Status_NotStarted);

            const GFSDK_Aftermath_Result driverResult =
                DriverGetData(static_cast<Api>(handle->api),
                              handle->pContext,
                              handle->pDriverState,
                              &pMarkerData,
                              &markerSize,
                              &contextStatus);

            if (!AftermathSucceeded(driverResult))
            {
                // [D] the driver's verdict, translated.  The deferred-context
                //     (-0x452FFFF1) and bundle (-0x452FFFF2) codes come back
                //     from the driver through this path.
                SetContextDataFailure(&entry, TranslateDriverResult(
                    static_cast<uint32_t>(driverResult)));
                continue;
            }

            entry.markerData = const_cast<void*>(pMarkerData);
            entry.markerSize = markerSize;
            entry.status     = static_cast<GFSDK_Aftermath_Context_Status>(
                                   contextStatus);
        }

        // [D] the array query itself succeeds even when individual entries were
        //     marked invalid.
        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // GetDeviceStatus
    // =========================================================================
    //
    // [D] the output slot is pre-set to 4 and the driver status is translated:
    //
    //         driver 1        -> 0   Active
    //         driver 2, 3, 4  -> 1   Timeout
    //         driver 5        -> 2   OutOfMemory
    //         driver 6, 7     -> 3   PageFault
    //         anything else   -> 4   (left as pre-set)
    //
    //     The translation is deliberately total: an unmapped driver status is
    //     not an error, it just leaves the "no information" value behind.
    //
    //     Note that this entry point is not gated on a feature flag in the
    //     recovered code, and that it answers FAIL_NotInitialized when no
    //     Initialize() has happened.
    // =========================================================================
    static GFSDK_Aftermath_Device_Status TranslateDeviceStatus(uint32_t driverStatus)
    {
        switch (driverStatus)
        {
        case 1:
            return GFSDK_Aftermath_Device_Status_Active;

        case 2:
        case 3:
        case 4:
            return GFSDK_Aftermath_Device_Status_Timeout;

        case 5:
            return GFSDK_Aftermath_Device_Status_OutOfMemory;

        case 6:
        case 7:
            return GFSDK_Aftermath_Device_Status_PageFault;

        default:
            return GFSDK_Aftermath_Device_Status_Unknown;
        }
    }

    GFSDK_Aftermath_Result GetDeviceStatus(GFSDK_Aftermath_Device_Status* pStatus)
    {
        if (!g_initialized || g_pDevice == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        if (pStatus == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // [D] written before the driver is called, so a failed call leaves it.
        *pStatus = GFSDK_Aftermath_Device_Status_Unknown;

        uint32_t driverStatus = 0;

        const GFSDK_Aftermath_Result driverResult =
            DriverGetDeviceStatus(g_activeApi, g_pDevice, &driverStatus);

        if (!AftermathSucceeded(driverResult))
        {
            // The status word keeps its "no information" value; the call itself
            // reports the driver failure.
            return TranslateDriverResult(static_cast<uint32_t>(driverResult));
        }

        *pStatus = TranslateDeviceStatus(driverStatus);

        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // GetPageFaultInformation
    // =========================================================================
    //
    // [D] the feature gate is bit 1 of the stored flags:
    //
    //         if ( (dword_1800202A8 & 2) == 0 )
    //             return 0xBAD00010;      // FAIL_FeatureNotEnabled
    //
    //     i.e. GFSDK_Aftermath_FeatureFlags_EnableResourceTracking must have
    //     been requested, which matches the published documentation for this
    //     entry point.
    //
    // [D] the caller's structure pointer is forwarded to the driver untouched;
    //     the library never dereferences it, which is why its layout cannot be
    //     recovered from this image.
    // =========================================================================
    GFSDK_Aftermath_Result GetPageFaultInformation(
        GFSDK_Aftermath_PageFaultInformation* pPageFaultInfo)
    {
        if (!g_initialized || g_pDevice == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        // [D] EnableResourceTracking gate
        if ((g_featureFlags &
             GFSDK_Aftermath_FeatureFlags_EnableResourceTracking) == 0)
        {
            return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
        }

        if (pPageFaultInfo == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        const GFSDK_Aftermath_Result driverResult =
            DriverGetPageFaultInformation(g_activeApi,
                                          g_pDevice,
                                          pPageFaultInfo);

        if (!AftermathSucceeded(driverResult))
        {
            return TranslateDriverResult(static_cast<uint32_t>(driverResult));
        }

        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // Shutdown
    // =========================================================================
    //
    // [D] the exported library tears its state down on unload.  Resetting the
    //     globals is all that is left for this revision to do: the handle
    //     objects are owned by the application and the driver keeps its own
    //     bookkeeping.
    // =========================================================================
    void Shutdown()
    {
        DriverReleaseDevice(g_activeApi, g_pDevice);

        g_pDevice           = nullptr;
        g_featureFlags      = 0;
        g_initialized       = false;
        g_activeApi         = Api_D3D11;
        g_oneTimeInitState  = OneTimeInit_NotStarted;

        nvapi::Release();
    }
}
