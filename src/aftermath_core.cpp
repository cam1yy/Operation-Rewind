// =============================================================================
//  aftermath_core.cpp
// -----------------------------------------------------------------------------
//  The real implementation behind the exported entry points.
//
//  Every public function in aftermath_exports.cpp is a two line forwarder that
//  pins the API selector; all of the logic lives here.
//
//      sub_180004690   Initialize                 (DX11 and DX12)
//      sub_180004AC0   CreateContextHandle        (DX11 and DX12)
//      sub_180004CC0   SetEventMarker
//      sub_180004E40   GetData
//      sub_1800051D0   GetDeviceStatus
//      sub_1800053D0   GetPageFaultInformation
//
//  The bodies below follow the listing statement by statement.  Where the
//  original does something surprising the surprise is kept and explained in a
//  comment rather than tidied away, because the exported behaviour depends on
//  it.
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
    // [D] This single table is used everywhere a driver status has to be turned
    //     into a public code, both for function return values and for the
    //     per-context encoding inside GetData.  It is byte for byte the switch
    //     that appears in sub_180004690, sub_180004AC0, sub_180004CC0,
    //     sub_180004E40, sub_1800051D0 and sub_1800053D0.
    //
    //     The case labels are quoted as the recovery listed them.  Note -0x85
    //     and -0x83 sharing a body with -1 while -0x84 sits between them: that
    //     interleaving is characteristic of a generated dispatch table over a
    //     driver status enumeration rather than hand written code, which is
    //     good evidence the transcription is faithful.
    // =========================================================================
    static GFSDK_Aftermath_Result MapDriverStatus(int32_t driverStatus)
    {
        switch (driverStatus)
        {
        case 0:
            return GFSDK_Aftermath_Result_Success;

        case -0x86:     // -134
        case -0x68:     // -104
        case -3:
            return GFSDK_Aftermath_Result_FAIL_NvApiIncompatible;

        case -0x85:     // -133
        case -0x83:     // -131
        case -1:
            return GFSDK_Aftermath_Result_Fail;

        case -0x84:     // -132
            return GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList;

        case -0x82:     // -130
            return GFSDK_Aftermath_Result_FAIL_OutOfMemory;

        case -5:
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;

        default:
            return GFSDK_Aftermath_Result_FAIL_Unknown;
        }
    }

    // =========================================================================
    // The Initialize() guard
    // =========================================================================
    //
    // [D] sub_180004690 opens with:
    //
    //       v7 = 1;                                    // return value seed
    //       if ( _InterlockedCompareExchange(&dword_18002029C, 1, 0) == 0
    //            || byte_1800202AC != 0 )
    //       {
    //           ... the real work ...
    //       }
    //       return v7;                                 // == 1 if skipped
    //
    //     Two things follow, and both are reproduced:
    //
    //       * the flag is set to 1 and never cleared, so once the FIRST call
    //         has taken the compare-exchange, later calls only enter the body
    //         when g_initialized is already set;
    //       * if the first call FAILED, g_initialized stays clear and every
    //         later call falls through to the leaf return -- reporting
    //         GFSDK_Aftermath_Result_Success (1) without doing any work at all.
    //
    //     That second point is almost certainly not what the author intended,
    //     but it is what the binary does, so it is what this builds.
    // =========================================================================
    static bool EnterInitialize()
    {
        const bool firstEver = (AFTERMATH_INTERLOCKED_CAS(&g_initGuard, 1u, 0u) == 0);

        return firstEver || g_initialized;
    }

    // =========================================================================
    // The D3D12 debug layer probe
    // =========================================================================
    //
    // [D] The DX12 arm of Initialize() does, before it touches the driver:
    //
    //       if ( (**a4)(a4, &unk_1800189F0, &v12) == 0 )   // S_OK == debug layer present
    //           return 0xBAD0000A;                        // FAIL_D3DDebugLayerNotCompatible
    //       if ( v12 != 0 )
    //           (*(void (**)(__int64))(*(_QWORD *)v12 + 16))(v12);   // release
    //
    //     i.e. it calls QueryInterface on the D3D12 device, and an interface
    //     that is PRESENT is the failure condition.  This is the standard
    //     IDXGIDebug probe: the debug layer exposes it, and Aftermath cannot
    //     share the device with it.
    //
    // [I] The 16 bytes of the queried IID live at 0x1800189F0 and were not part
    //     of the recovered listing.  The IID below is IDXGIDebug, which the call
    //     shape strongly implies.  If it turns out different, this is the one
    //     constant to change; it has no other effect on the reconstruction.
    // =========================================================================
    struct AftermathGuid
    {
        uint32_t data1;
        uint16_t data2;
        uint16_t data3;
        uint8_t  data4[8];
    };

    static const AftermathGuid kXgiDebugIid =
    {
        0x119e7452u, 0xde9eu, 0x40feu,
        { 0x88, 0x06, 0x88, 0xf9, 0x0c, 0x12, 0xb4, 0x41 }
    };

    // Slot 2 of the returned interface's vtable (offset +0x10) is its release
    // method.  [D] the recovered call is `(*(...)(*v12 + 16))(v12)`.
    static bool D3D12DebugLayerIsPresent(const void* pDevice)
    {
        // The device is a COM object: its vtable is the first word, and
        // QueryInterface is slot 1 (offset 0).
        void** vtable = *reinterpret_cast<void***>(const_cast<void*>(pDevice));

        typedef int32_t(AFTERMATH_CDECL * QueryInterfaceFn)(void* pSelf,
                                                            const void* pIid,
                                                            void** ppOut);

        QueryInterfaceFn queryInterface =
            reinterpret_cast<QueryInterfaceFn>(vtable[0]);

        void* pInterface = nullptr;
        const int32_t hr = queryInterface(const_cast<void*>(pDevice),
                                          &kXgiDebugIid,
                                          &pInterface);

        // HRESULT 0 == S_OK == "the interface exists" == the debug layer is on.
        if (hr == 0)
        {
            // Release it before reporting the failure; the original does this
            // after the test, on the non-failing path only, so on failure the
            // interface is deliberately leaked along with the call.
            return true;
        }

        if (pInterface != nullptr)
        {
            void** ifaceVtable = *reinterpret_cast<void***>(pInterface);
            typedef uint32_t(AFTERMATH_CDECL * ReleaseFn)(void* pSelf);
            ReleaseFn release = reinterpret_cast<ReleaseFn>(ifaceVtable[2]);
            release(pInterface);
        }

        return false;
    }

    // =========================================================================
    // Initialize     -- sub_180004690
    // =========================================================================
    GFSDK_Aftermath_Result Initialize(
        Api                          api,
        GFSDK_Aftermath_Version      version,
        GFSDK_Aftermath_FeatureFlags flags,
        const void*                  pDevice)
    {
        // [D] the guard.  Falling through returns Success without doing work.
        if (!EnterInitialize())
        {
            return GFSDK_Aftermath_Result_Success;
        }

        // [D] `if ( a2 != 19 ) return 0xBAD00001;`
        //     0xBAD00001 is FAIL_VersionMismatch, NOT FAIL_ApiError.
        if (version != GFSDK_Aftermath_Version_API)
        {
            return GFSDK_Aftermath_Result_FAIL_VersionMismatch;
        }

        // [D] `if ( a4 == nullptr ) return 0xBAD00004;`
        if (pDevice == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // [D] the version/info query, with the 0x40 byte info block zeroed.
        uint32_t interfaceVersion = 0;
        uint8_t  interfaceInfo[0x40];
        ::memset(interfaceInfo, 0, sizeof(interfaceInfo));

        int32_t driverStatus =
            nvapi::ReadInterfaceInfo(&interfaceVersion, interfaceInfo);

        if (driverStatus != 0)
        {
            return MapDriverStatus(driverStatus);
        }

        // [D] `if ( v13 < 0x9784 ) return 0xBAD0000C;`
        if (interfaceVersion < nvapi::NVAPI_MIN_INTERFACE_VERSION)
        {
            return GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported;
        }

        int32_t attachStatus;

        if (api == Api_D3D12)
        {
            // [D] the debug layer probe.  It runs before anything is handed to
            //     the driver -- see the note on D3D12DebugLayerIsPresent(); its
            //     position relative to the version gates above is not pinned
            //     down by the listing, and only differs in outcome when a
            //     debug-layer device is combined with a driver that would have
            //     failed the version query anyway.
            if (D3D12DebugLayerIsPresent(pDevice))
            {
                return GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible;
            }

            attachStatus = Attach(Api_D3D12, const_cast<void*>(pDevice),
                                  &g_deviceDriverHandle);
        }
        else
        {
            if (api != Api_D3D11)
            {
                // [D] unreachable from the exports, which only pass 0 and 1.
                return GFSDK_Aftermath_Result_FAIL_ApiError;
            }

            attachStatus = Attach(Api_D3D11, const_cast<void*>(pDevice),
                                  &g_deviceDriverHandle);
        }

        if (attachStatus != 0)
        {
            return MapDriverStatus(attachStatus);
        }

        // [D] features are enabled on the DRIVER HANDLE, not on the device.
        driverStatus = EnableFeatures(api, g_deviceDriverHandle,
                                      static_cast<uint32_t>(flags));

        if (driverStatus != 0)
        {
            // [D] this one is a fixed code, not a mapped driver status.
            return GFSDK_Aftermath_Result_FAIL_DriverInitFailed;
        }

        // [D] publish the state.  A repeated successful Initialize() overwrites
        //     both values rather than failing; there is no
        //     FAIL_AlreadyInitialized path in this revision.
        g_featureFlags = static_cast<uint32_t>(flags);
        g_initialized  = true;

        return GFSDK_Aftermath_Result_Success;
    }

    // =========================================================================
    // CreateContextHandle        -- sub_180004AC0
    // =========================================================================
    //
    // [D] the recovered order of operations, which is not the obvious one:
    //
    //       1. allocate and fully populate the handle
    //       2. store it in *pHandleOut               <-- BEFORE the driver call
    //       3. Attach(api, pContext, &handle->pDriverHandle)
    //       4. map the Attach status and return
    //
    //     Two consequences that are reproduced deliberately:
    //
    //       * the context IS registered with the driver here, at creation
    //         time.  This is why GFSDK_Aftermath_GetData() has no registration
    //         step of its own.
    //       * on failure the handle has already escaped to the caller and is
    //         not freed, so a failed Attach leaks 0x18 bytes.  The original
    //         does this; "fixing" it would change observable behaviour.
    // =========================================================================
    GFSDK_Aftermath_Result CreateContextHandle(
        Api                             api,
        const void*                     pContext,
        GFSDK_Aftermath_ContextHandle*  pHandleOut)
    {
        // [D] `if ( byte_1800202AC == 0 ) return 0xBAD00002;`
        if (!g_initialized)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        // [D] `if ( a2 == 0 ) return 0xBAD00004;`
        //     Note there is NO test on pHandleOut.
        if (pContext == nullptr)
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

        // [D] the original zeroes the first qword and the third, then stamps the
        //     API word over the upper half of the first.  See the note on
        //     GFSDK_Aftermath_ContextHandleImpl for why the layout is what it is.
        handle->reserved0     = 0;
        handle->pDriverHandle = nullptr;
        handle->api           = static_cast<uint32_t>(api);
        handle->pD3DObject    = const_cast<void*>(pContext);
        handle->pDriverHandle = nullptr;

        // [D] the handle escapes before the driver can reject it.
        *pHandleOut = handle;

        int32_t driverStatus;

        if (api == Api_D3D11)
        {
            driverStatus = Attach(Api_D3D11, const_cast<void*>(pContext),
                                  &handle->pDriverHandle);
        }
        else
        {
            if (api != Api_D3D12)
            {
                return GFSDK_Aftermath_Result_FAIL_ApiError;
            }

            driverStatus = Attach(Api_D3D12, const_cast<void*>(pContext),
                                  &handle->pDriverHandle);
        }

        return MapDriverStatus(driverStatus);
    }

    // =========================================================================
    // ReleaseContextHandle       -- GFSDK_Aftermath_ReleaseContextHandle
    // =========================================================================
    //
    // [D] `if ( !initialized ) return 0xBAD00002;`
    //     `if ( handle == 0 )  return 0xBAD00004;`
    //     `_free_base(handle); return 1;`
    //
    //     No driver call at all -- which is only sound because the driver handle
    //     at +16 is owned by the driver, not by this object.
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
    // SetEventMarker             -- sub_180004CC0
    // =========================================================================
    //
    // [D] the whole recovered body:
    //
    //       if ( !initialized )        return 0xBAD00002;
    //       if ( (flags & 1) == 0 )    return 0xBAD00010;   // EnableMarkers
    //       if ( handle == 0 )         return 0xBAD00004;
    //
    //       switch ( *(int*)(handle + 4) )                  // the API word
    //       {
    //       case 0: status = SetEventMarker_D3D11(*(void**)(handle + 16), data, size); break;
    //       case 1: status = SetEventMarker_D3D12(*(void**)(handle + 16), data, size); break;
    //       default: return 0xBAD00006;                     // FAIL_ApiError
    //       }
    //       return MapDriverStatus(status);
    //
    //     Two absences worth noting, because an earlier reconstruction assumed
    //     them present and they are not: there is no null test on markerData and
    //     no upper bound on markerDataSize.
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

        // [D] EnableMarkers gate
        if ((g_featureFlags & GFSDK_Aftermath_FeatureFlags_EnableMarkers) == 0)
        {
            return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
        }

        if (handle == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        int32_t driverStatus;

        if (handle->api == Api_D3D11)
        {
            driverStatus = SetEventMarker(Api_D3D11, handle->pDriverHandle,
                                          markerData, markerDataSize);
        }
        else
        {
            if (handle->api != Api_D3D12)
            {
                return GFSDK_Aftermath_Result_FAIL_ApiError;
            }

            driverStatus = SetEventMarker(Api_D3D12, handle->pDriverHandle,
                                          markerData, markerDataSize);
        }

        return MapDriverStatus(driverStatus);
    }

    // =========================================================================
    // GetData                    -- sub_180004E40
    // =========================================================================
    //
    // [D] The call is an array query: numContexts handles in, numContexts
    //     GFSDK_Aftermath_ContextData out, 16 bytes apart, each dispatched on
    //     its own handle.
    //
    // [D] A per-context failure does NOT fail the call.  Instead the entry is
    //     marked with status = 3 (Context_Status_Invalid) and the RESULT CODE
    //     WIDENED INTO THE markerData POINTER SLOT.  That is what the negative
    //     immediates in the listing decode to:
    //
    //         -0x452FFFF1 = 0xBAD0000F  FAIL_GetDataOnDeferredContext
    //         -0x452FFFF2 = 0xBAD0000E  FAIL_GetDataOnBundle
    //         -0x452FFFFC = 0xBAD00004  FAIL_InvalidParameter   (null handle)
    //
    //     Those three are not driver statuses: they are hard-coded sentinels.
    // =========================================================================
    //
    // [D] The deferred-context / bundle detection is done by calling through the
    //     D3D object's own vtable, not by asking the driver:
    //
    //         API == D3D11:  (*(*(pContext + 8) + 0x380))() == 1  ->  deferred
    //         API == D3D12:  (*(*(pContext + 8) + 0x40))()  == 1  ->  bundle
    //
    //     0x380/8 = vtable slot 112 of ID3D11DeviceContext, whose GetType()
    //     returns D3D11_DEVICE_CONTEXT_TYPE with DEFERRED == 1.
    //     0x40/8  = vtable slot 8 of ID3D12CommandList, whose GetType() returns
    //     D3D12_COMMAND_LIST_TYPE with BUNDLE == 1.
    //     Both comparisons are `== 1`, so both are a "type is the second
    //     enumerator" test, which is exactly the deferred and bundle cases.
    // =========================================================================
    static uint32_t CallD3DObjectGetType(const void* pD3DObject, size_t slotOffset)
    {
        void** vtable = *reinterpret_cast<void***>(const_cast<void*>(pD3DObject));
        typedef uint32_t(AFTERMATH_CDECL * GetTypeFn)(void* pSelf);
        GetTypeFn getType =
            reinterpret_cast<GetTypeFn>(vtable[slotOffset / sizeof(void*)]);
        return getType(const_cast<void*>(pD3DObject));
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

        // [D] EnableMarkers gate -- the same bit as SetEventMarker.
        if ((g_featureFlags & GFSDK_Aftermath_FeatureFlags_EnableMarkers) == 0)
        {
            return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
        }

        if (pContextHandles == nullptr || pContextDataOut == nullptr ||
            numContexts == 0)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // [D] The accumulator behind the return value.  Only a real driver
        //     call writes it, so a call whose contexts are all rejected by the
        //     sentinel paths below returns Success.  The closing guard
        //     `if ( lastDriverStatus + 0x86 > 0x86U ) return 0xBAD00005;` is an
        //     unsigned "was it positive" test, which is why the seed must be a
        //     value the guard passes: only 0 and -1 qualify, and the emitted
        //     constant list shows no separate negative seed.  See
        //     docs/ANALYSIS_NOTES.md, open question 6.
        int32_t lastDriverStatus = 0;

        for (uint32_t i = 0; i < numContexts; ++i)
        {
            GFSDK_Aftermath_ContextData& entry = pContextDataOut[i];
            const GFSDK_Aftermath_ContextHandle handle = pContextHandles[i];

            // [D] null handle -> FAIL_InvalidParameter encoded in the entry.
            if (handle == nullptr)
            {
                entry.status     = GFSDK_Aftermath_Context_Status_Invalid;
                entry.markerData = reinterpret_cast<void*>(
                    static_cast<intptr_t>(
                        static_cast<int32_t>(GFSDK_Aftermath_Result_FAIL_InvalidParameter)));
                continue;
            }

            int32_t driverStatus;

            if (handle->api == Api_D3D11)
            {
                // [D] deferred context -> FAIL_GetDataOnDeferredContext
                if (CallD3DObjectGetType(handle->pD3DObject, 0x380) == 1)
                {
                    entry.status     = GFSDK_Aftermath_Context_Status_Invalid;
                    entry.markerData = reinterpret_cast<void*>(
                        static_cast<intptr_t>(
                            static_cast<int32_t>(
                                GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext)));
                    continue;
                }

                driverStatus = GetData(Api_D3D11,
                                       handle->pDriverHandle,
                                       &entry.markerData,
                                       &entry.markerSize,
                                       reinterpret_cast<uint32_t*>(&entry.status));
            }
            else
            {
                if (handle->api != Api_D3D12)
                {
                    return GFSDK_Aftermath_Result_FAIL_ApiError;
                }

                // [D] bundle -> FAIL_GetDataOnBundle
                if (CallD3DObjectGetType(handle->pD3DObject, 0x40) == 1)
                {
                    entry.status     = GFSDK_Aftermath_Context_Status_Invalid;
                    entry.markerData = reinterpret_cast<void*>(
                        static_cast<intptr_t>(
                            static_cast<int32_t>(
                                GFSDK_Aftermath_Result_FAIL_GetDataOnBundle)));
                    continue;
                }

                driverStatus = GetData(Api_D3D12,
                                       handle->pDriverHandle,
                                       &entry.markerData,
                                       &entry.markerSize,
                                       reinterpret_cast<uint32_t*>(&entry.status));
            }

            lastDriverStatus = driverStatus;

            if (driverStatus != 0)
            {
                entry.status     = GFSDK_Aftermath_Context_Status_Invalid;
                entry.markerData = reinterpret_cast<void*>(
                    static_cast<intptr_t>(
                        static_cast<int32_t>(MapDriverStatus(driverStatus))));
            }
        }

        // [D] `if ( lastDriverStatus + 0x86 > 0x86U ) return 0xBAD00005;`
        //     which is an unsigned test for "the status was positive".
        if (static_cast<uint32_t>(lastDriverStatus) + 0x86u > 0x86u)
        {
            return GFSDK_Aftermath_Result_FAIL_Unknown;
        }

        return MapDriverStatus(lastDriverStatus);
    }

    // =========================================================================
    // GetDeviceStatus            -- sub_1800051D0
    // =========================================================================
    //
    // [D] Be aware of three things that a first reading gets wrong:
    //
    //     1. The driver handle at 0x1800202A0 is passed, not a device pointer.
    //     2. The status word is pre-set to 4 and the driver is asked through
    //        the D3D11 entry point first, falling back to the D3D12 one.  There
    //        is no stored "which API is active" flag anywhere in this image.
    //     3. The RESULT comes from the second call's status variable, which is
    //        left at 0 whenever the first call succeeded -- so the function
    //        reports Success even if it was the fallback that produced the
    //        status word.
    //
    // [D] there is NO feature flag gate on this entry point.
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
            // Written value stays at the pre-set 4 ("no information").
            return GFSDK_Aftermath_Device_Status_Unknown;
        }
    }

    GFSDK_Aftermath_Result GetDeviceStatus(GFSDK_Aftermath_Device_Status* pStatus)
    {
        // [D] `if ( byte_1800202AC == 0 ) return 0xBAD00002;`
        if (!g_initialized)
        {
            return GFSDK_Aftermath_Result_FAIL_NotInitialized;
        }

        // [D] `if ( param_1 == 0 ) return 0xBAD00004;`
        if (pStatus == nullptr)
        {
            return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
        }

        // [D] pre-set before the driver is consulted.
        *pStatus = GFSDK_Aftermath_Device_Status_Unknown;

        uint32_t driverStatusWord = 0;

        const int32_t primary = GetDeviceStatusPrimary(g_deviceDriverHandle,
                                                       &driverStatusWord);

        int32_t secondStatus = 0;

        if (primary == 0)
        {
            // Primary answered: translate what it produced.
            *pStatus = TranslateDeviceStatus(driverStatusWord);
        }
        else
        {
            secondStatus = GetDeviceStatusFallback(g_deviceDriverHandle,
                                                   &driverStatusWord);

            if (secondStatus == 0)
            {
                *pStatus = TranslateDeviceStatus(driverStatusWord);
            }
        }

        // [D] the returned code is derived from `secondStatus`, which is 0 when
        //     the primary succeeded.
        if (static_cast<uint32_t>(secondStatus) + 0x86u > 0x86u)
        {
            return GFSDK_Aftermath_Result_FAIL_Unknown;
        }

        return MapDriverStatus(secondStatus);
    }

    // =========================================================================
    // GetPageFaultInformation    -- sub_1800053D0
    // =========================================================================
    //
    // [D] `if ( (flags & 2) == 0 ) return 0xBAD00010;` -- the
    //     EnableResourceTracking bit, and only that bit.
    //
    // [D] The caller's structure pointer is forwarded to the driver untouched;
    //     the library never dereferences it, which is why its layout cannot be
    //     recovered from this image.
    //
    // [D] same primary/fallback arrangement as the device status, but here the
    //     two calls share one status variable, so a failing primary followed by
    //     a successful fallback still reports Success.
    // =========================================================================
    GFSDK_Aftermath_Result GetPageFaultInformation(
        GFSDK_Aftermath_PageFaultInformation* pPageFaultInfo)
    {
        if (!g_initialized)
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

        int32_t driverStatus = GetPageFaultInformationPrimary(g_deviceDriverHandle,
                                                              pPageFaultInfo);

        if (driverStatus != 0)
        {
            driverStatus = GetPageFaultInformationFallback(g_deviceDriverHandle,
                                                           pPageFaultInfo);
        }

        if (static_cast<uint32_t>(driverStatus) + 0x86u > 0x86u)
        {
            return GFSDK_Aftermath_Result_FAIL_Unknown;
        }

        return MapDriverStatus(driverStatus);
    }

    // =========================================================================
    // Shutdown
    // =========================================================================
    //
    // [D] No teardown routine was found in the image.  The library's own
    //     DllMain is a bare `return true;` and the CRT scaffolding that
    //     surrounds it does not touch any Aftermath state, so nothing in the
    //     original ever clears these globals.
    //
    //     This function exists for the reconstruction's own benefit -- it lets
    //     the host test drive a full init/teardown cycle -- and is called from
    //     the module detach path here.  It has no counterpart in the binary.
    // =========================================================================
    void Shutdown()
    {
        g_deviceDriverHandle = nullptr;
        g_featureFlags       = 0;
        g_initialized        = false;

        // NOTE: g_initGuard is deliberately NOT reset, mirroring the original
        // where the compare-exchange value is never cleared.  See the note on
        // EnterInitialize().
        // g_initGuard = 0;

        nvapi::Release();
    }

    // -------------------------------------------------------------------------
    // Test-build helper: reset the guard so the host test can run more than one
    // Initialize() scenario.  Compiled out of the shipping build.
    // -------------------------------------------------------------------------
#ifdef AFTERMATH_TEST_BUILD
    void TestResetInitializeGuard()
    {
        g_initGuard = 0;
    }
#endif
}
