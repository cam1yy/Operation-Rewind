/*
 * aftermath_api.cpp -- the nine exported entry points and the two shared workers.
 *
 *      sub_180004690   Initialize                      (GFSDK_Aftermath_DX1x_Initialize)
 *      sub_180004AC0   CreateContextHandle             (GFSDK_Aftermath_DX1x_CreateContextHandle)
 *      sub_180004CC0   GFSDK_Aftermath_SetEventMarker
 *      sub_180004E40   GFSDK_Aftermath_GetData
 *      sub_1800051D0   GFSDK_Aftermath_GetDeviceStatus
 *      sub_1800053D0   GFSDK_Aftermath_GetPageFaultInformation
 *      sub_180005550   GFSDK_Aftermath_DX12_Initialize          -> Initialize(kApiTypeDX12, ...)
 *      sub_180005570   GFSDK_Aftermath_DX11_Initialize          -> Initialize(kApiTypeDX11, ...)
 *      sub_180005580   GFSDK_Aftermath_DX12_CreateContextHandle -> CreateContextHandle(kApiTypeDX12, ...)
 *      sub_180005590   GFSDK_Aftermath_DX11_CreateContextHandle -> CreateContextHandle(kApiTypeDX11, ...)
 *      sub_1800055A0   GFSDK_Aftermath_ReleaseContextHandle
 */

#include "aftermath_internal.h"

#include <new>

#include "compat/com_min.h"
#include "nvapi/nvapi_bridge.h"

namespace aftermath {
namespace {

/* ------------------------------------------------------------------------------------ *
 * Library state.  Four globals, all in the 0x180202xx block.
 * ------------------------------------------------------------------------------------ */

/* 0x18002029C -- claimed with InterlockedCompareExchange so that only the first caller
 * runs the initialisation sequence. */
volatile LONG g_initializeGuard = 0;

/* 0x1800202A0 -- driver side handle for the device, used by every device wide query. */
NvAftermathContextHandle g_deviceHandle = NULL;

/* 0x1800202A8 -- GFSDK_Aftermath_FeatureFlags as passed to Initialize. */
unsigned int g_featureFlags = 0;

/* 0x1800202AC -- set once initialisation completed successfully. */
bool g_initialized = false;

/*
 * 0x1800189F0 -- the interface the DX12 path asks the device for before touching NVAPI.
 * A successful QueryInterface means the D3D12 debug layer is active, and Aftermath
 * refuses to run alongside it.  The GUID bytes live in .rdata, which was not part of the
 * supplied listings; IID_ID3D12DebugDevice is the interface that carries that meaning.
 */
const IID kIID_ID3D12DebugDevice =
    { 0x3febd6dd, 0x4973, 0x4787, { 0x81, 0x94, 0xe4, 0x5f, 0x9e, 0x28, 0x92, 0x3e } };

/*
 * GFSDK_Aftermath_GetData reports per-context failures by parking the result code in the
 * markerData slot.  The binary writes the full 64 bit field with a sign extended 32 bit
 * immediate, which this helper reproduces exactly.
 */
void StoreResultInMarkerData(GFSDK_Aftermath_ContextData* pContextData, GFSDK_Aftermath_Result result)
{
    const INT_PTR signExtended = static_cast<INT_PTR>(static_cast<INT32>(result));
    pContextData->markerData = reinterpret_cast<void*>(signExtended);
    pContextData->status = GFSDK_Aftermath_Context_Status_Invalid;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------------------------ *
 * NVAPI status -> Aftermath result.  Inlined at every call site in the binary; the
 * mapping is identical everywhere.
 * ------------------------------------------------------------------------------------ */
GFSDK_Aftermath_Result TranslateNvApiStatus(NvAPI_Status status)
{
    switch (status)
    {
    case NVAPI_OK:
        return GFSDK_Aftermath_Result_Success;

    case NVAPI_INVALID_CALL:            /* -134 */
    case NVAPI_NOT_SUPPORTED:           /* -104 */
    case NVAPI_NO_IMPLEMENTATION:       /*   -3 */
        return GFSDK_Aftermath_Result_FAIL_NvApiIncompatible;

    case NVAPI_TOO_MANY_UNIQUE_STATE_OBJECTS: /* -133 */
    case NVAPI_WAS_STILL_DRAWING:             /* -131 */
    case NVAPI_ERROR:                         /*   -1 */
        return GFSDK_Aftermath_Result_Fail;

    case NVAPI_FILE_NOT_FOUND:          /* -132 */
        return GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList;

    case NVAPI_OUT_OF_MEMORY:           /* -130 */
        return GFSDK_Aftermath_Result_FAIL_OutOfMemory;

    case NVAPI_INVALID_ARGUMENT:        /*   -5 */
        return GFSDK_Aftermath_Result_FAIL_InvalidParameter;

    default:
        return GFSDK_Aftermath_Result_FAIL_Unknown;
    }
}

/* ------------------------------------------------------------------------------------ *
 * sub_180004690
 * ------------------------------------------------------------------------------------ */
GFSDK_Aftermath_Result Initialize(ApiType apiType,
                                  GFSDK_Aftermath_Version version,
                                  unsigned int flags,
                                  IUnknown* pDevice)
{
    /*
     * Verbatim from the binary: the first caller claims the guard and runs the body;
     * any caller that loses the race before initialisation completed is told "Success"
     * and does nothing.  Note that the guard is never released, so a *failed*
     * initialisation cannot be retried -- subsequent calls return Success while
     * g_initialized stays false and every other entry point keeps reporting
     * FAIL_NotInitialized.  See docs/RECONSTRUCTION.md.
     */
    if (::InterlockedCompareExchange(&g_initializeGuard, 1, 0) != 0 && !g_initialized)
    {
        return GFSDK_Aftermath_Result_Success;
    }

    if (version != GFSDK_Aftermath_Version_API)
    {
        return GFSDK_Aftermath_Result_FAIL_VersionMismatch;
    }

    if (pDevice == NULL)
    {
        return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
    }

    NvU32 driverVersion = 0;
    NvAPI_ShortString buildBranch;
    ::ZeroMemory(buildBranch, sizeof(buildBranch));

    NvAPI_Status status = nvapi::SYS_GetDriverAndBranchVersion(&driverVersion, buildBranch);
    if (status != NVAPI_OK)
    {
        return TranslateNvApiStatus(status);
    }

    if (driverVersion < kMinimumDriverVersion)
    {
        return GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported;
    }

    if (apiType == kApiTypeDX11)
    {
        status = nvapi::D3D11_AftermathCreateHandle(pDevice, &g_deviceHandle);
        if (status != NVAPI_OK)
        {
            return TranslateNvApiStatus(status);
        }

        if (nvapi::D3D11_AftermathEnableFeatures(g_deviceHandle, flags) != NVAPI_OK)
        {
            return GFSDK_Aftermath_Result_FAIL_DriverInitFailed;
        }
    }
    else if (apiType == kApiTypeDX12)
    {
        IUnknown* pDebugDevice = NULL;
        if (SUCCEEDED(pDevice->QueryInterface(kIID_ID3D12DebugDevice, reinterpret_cast<void**>(&pDebugDevice))))
        {
            /* The debug layer is enabled -- Aftermath and the debug layer are mutually
             * exclusive.  (The original leaks the reference here; it only releases it on
             * the failure path below.) */
            return GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible;
        }

        /* QueryInterface failed, so pDebugDevice must be NULL -- the binary still checks. */
        if (pDebugDevice != NULL)
        {
            pDebugDevice->Release();
        }

        status = nvapi::D3D12_AftermathCreateHandle(pDevice, &g_deviceHandle);
        if (status != NVAPI_OK)
        {
            return TranslateNvApiStatus(status);
        }

        if (nvapi::D3D12_AftermathEnableFeatures(g_deviceHandle, flags) != NVAPI_OK)
        {
            return GFSDK_Aftermath_Result_FAIL_DriverInitFailed;
        }
    }
    else
    {
        return GFSDK_Aftermath_Result_FAIL_ApiError;
    }

    g_featureFlags = flags;
    g_initialized = true;
    return GFSDK_Aftermath_Result_Success;
}

/* ------------------------------------------------------------------------------------ *
 * sub_180004AC0
 * ------------------------------------------------------------------------------------ */
GFSDK_Aftermath_Result CreateContextHandle(ApiType apiType,
                                           void* pD3DObject,
                                           GFSDK_Aftermath_ContextHandle* pOutContextHandle)
{
    if (!g_initialized)
    {
        return GFSDK_Aftermath_Result_FAIL_NotInitialized;
    }

    /* Note: the binary checks pD3DObject but not pOutContextHandle. */
    if (pD3DObject == NULL)
    {
        return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
    }

    ContextHandleImpl* const handle = new (std::nothrow) ContextHandleImpl;
    if (handle == NULL)
    {
        return GFSDK_Aftermath_Result_FAIL_OutOfMemory;
    }

    handle->reserved   = 0;
    handle->apiType    = static_cast<unsigned int>(apiType);
    handle->pD3DObject = pD3DObject;
    handle->nvHandle   = NULL;

    *pOutContextHandle = reinterpret_cast<GFSDK_Aftermath_ContextHandle>(handle);

    NvAPI_Status status;
    if (apiType == kApiTypeDX11)
    {
        status = nvapi::D3D11_AftermathCreateHandle(pD3DObject, &handle->nvHandle);
    }
    else if (apiType == kApiTypeDX12)
    {
        status = nvapi::D3D12_AftermathCreateHandle(pD3DObject, &handle->nvHandle);
    }
    else
    {
        /* Matches the binary, which leaks the allocation on this path. */
        return GFSDK_Aftermath_Result_FAIL_ApiError;
    }

    return TranslateNvApiStatus(status);
}

} /* namespace aftermath */

/* ====================================================================================== *
 * Exported entry points, in the order they appear in the original binary (MSVC emits
 * functions in source order, so keeping the order makes a future diff trivial).
 * ====================================================================================== */

/* sub_180004CC0 -- ordinal 9 */
GFSDK_Aftermath_API GFSDK_Aftermath_SetEventMarker(const GFSDK_Aftermath_ContextHandle contextHandle,
                                                   const void* markerData,
                                                   const GFSDK_Aftermath_uint32 markerSize)
{
    using namespace aftermath;

    if (!g_initialized)
    {
        return GFSDK_Aftermath_Result_FAIL_NotInitialized;
    }

    if ((g_featureFlags & GFSDK_Aftermath_FeatureFlags_EnableMarkers) == 0)
    {
        return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
    }

    if (contextHandle == NULL || markerData == NULL)
    {
        return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
    }

    const ContextHandleImpl* const handle = reinterpret_cast<const ContextHandleImpl*>(contextHandle);
    const ApiType apiType = static_cast<ApiType>(handle->apiType);

    NvAPI_Status status;
    if (apiType == kApiTypeDX11)
    {
        status = nvapi::D3D11_AftermathSetEventMarker(handle->nvHandle, markerData, markerSize);
    }
    else if (apiType == kApiTypeDX12)
    {
        status = nvapi::D3D12_AftermathSetEventMarker(handle->nvHandle, markerData, markerSize);
    }
    else
    {
        return GFSDK_Aftermath_Result_FAIL_ApiError;
    }

    return TranslateNvApiStatus(status);
}

/* sub_180004E40 -- ordinal 5 */
GFSDK_Aftermath_API GFSDK_Aftermath_GetData(const GFSDK_Aftermath_uint32 numContexts,
                                            const GFSDK_Aftermath_ContextHandle* pContextHandles,
                                            GFSDK_Aftermath_ContextData* pOutContextData)
{
    using namespace aftermath;

    if (!g_initialized)
    {
        return GFSDK_Aftermath_Result_FAIL_NotInitialized;
    }

    if ((g_featureFlags & GFSDK_Aftermath_FeatureFlags_EnableMarkers) == 0)
    {
        return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
    }

    if (numContexts == 0 || pContextHandles == NULL || pOutContextData == NULL)
    {
        return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
    }

    /* Seeded with NVAPI_ERROR so that "not a single context could be queried" still maps
     * to GFSDK_Aftermath_Result_Fail, exactly like the binary. */
    NvAPI_Status status = NVAPI_ERROR;

    for (GFSDK_Aftermath_uint32 i = 0; i < numContexts; ++i)
    {
        GFSDK_Aftermath_ContextData* const contextData = &pOutContextData[i];
        const ContextHandleImpl* const handle =
            reinterpret_cast<const ContextHandleImpl*>(pContextHandles[i]);

        if (handle == NULL)
        {
            StoreResultInMarkerData(contextData, GFSDK_Aftermath_Result_FAIL_InvalidParameter);
            continue;
        }

        const ApiType apiType = static_cast<ApiType>(handle->apiType);

        if (apiType == kApiTypeDX11)
        {
            /* Deferred contexts have no execution status of their own. */
            if (GetD3D11DeviceContextType(handle->pD3DObject) == kD3D11DeviceContextDeferred)
            {
                StoreResultInMarkerData(contextData, GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext);
                continue;
            }

            /* NvU32 is "unsigned long" in NVAPI's headers, so the 32 bit fields of
             * GFSDK_Aftermath_ContextData need an explicit (same size) cast. */
            status = nvapi::D3D11_AftermathGetData(handle->nvHandle,
                                                   &contextData->markerData,
                                                   reinterpret_cast<NvU32*>(&contextData->markerSize),
                                                   reinterpret_cast<NvU32*>(&contextData->status));
        }
        else if (apiType == kApiTypeDX12)
        {
            /* Bundles are recorded into a direct command list; query that instead. */
            if (GetD3D12CommandListType(handle->pD3DObject) == kD3D12CommandListTypeBundle)
            {
                StoreResultInMarkerData(contextData, GFSDK_Aftermath_Result_FAIL_GetDataOnBundle);
                continue;
            }

            status = nvapi::D3D12_AftermathGetData(handle->nvHandle,
                                                   &contextData->markerData,
                                                   reinterpret_cast<NvU32*>(&contextData->markerSize),
                                                   reinterpret_cast<NvU32*>(&contextData->status));
        }
        else
        {
            return GFSDK_Aftermath_Result_FAIL_ApiError;
        }

        if (status != NVAPI_OK)
        {
            StoreResultInMarkerData(contextData, TranslateNvApiStatus(status));
        }
    }

    return TranslateNvApiStatus(status);
}

/* sub_1800051D0 -- ordinal 6 */
GFSDK_Aftermath_API GFSDK_Aftermath_GetDeviceStatus(GFSDK_Aftermath_Device_Status* pOutStatus)
{
    using namespace aftermath;

    if (!g_initialized)
    {
        return GFSDK_Aftermath_Result_FAIL_NotInitialized;
    }

    if (pOutStatus == NULL)
    {
        return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
    }

    *pOutStatus = GFSDK_Aftermath_Device_Status_Unknown;

    NvU32 deviceState = 0;
    NvAPI_Status status = nvapi::AftermathGetDeviceState(g_deviceHandle, &deviceState);
    if (status != NVAPI_OK)
    {
        status = nvapi::AftermathGetDeviceState2(g_deviceHandle, &deviceState);
    }

    if (status == NVAPI_OK)
    {
        switch (deviceState)
        {
        case kNvAftermathDeviceState_Active:
            *pOutStatus = GFSDK_Aftermath_Device_Status_Active;
            break;

        case kNvAftermathDeviceState_Timeout:
        case kNvAftermathDeviceState_TimeoutAlt1:
        case kNvAftermathDeviceState_TimeoutAlt2:
            *pOutStatus = GFSDK_Aftermath_Device_Status_Timeout;
            break;

        case kNvAftermathDeviceState_OutOfMemory:
            *pOutStatus = GFSDK_Aftermath_Device_Status_OutOfMemory;
            break;

        case kNvAftermathDeviceState_PageFault:
        case kNvAftermathDeviceState_PageFaultAlt:
            *pOutStatus = GFSDK_Aftermath_Device_Status_PageFault;
            break;

        default:
            /* Leaves GFSDK_Aftermath_Device_Status_Unknown in place. */
            break;
        }
    }

    return TranslateNvApiStatus(status);
}

/* sub_1800053D0 -- ordinal 7 */
GFSDK_Aftermath_API GFSDK_Aftermath_GetPageFaultInformation(GFSDK_Aftermath_PageFaultInformation* pOutPageFaultInformation)
{
    using namespace aftermath;

    if (!g_initialized)
    {
        return GFSDK_Aftermath_Result_FAIL_NotInitialized;
    }

    if ((g_featureFlags & GFSDK_Aftermath_FeatureFlags_EnableResourceTracking) == 0)
    {
        return GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled;
    }

    if (pOutPageFaultInformation == NULL)
    {
        return GFSDK_Aftermath_Result_FAIL_InvalidParameter;
    }

    NvAPI_Status status = nvapi::AftermathGetPageFaultInformation(g_deviceHandle, pOutPageFaultInformation);
    if (status != NVAPI_OK)
    {
        status = nvapi::AftermathGetPageFaultInformation2(g_deviceHandle, pOutPageFaultInformation);
    }

    return TranslateNvApiStatus(status);
}

/* sub_180005550 -- ordinal 4 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version version,
                                                    GFSDK_Aftermath_uint32 flags,
                                                    struct ID3D12Device* const pDx12Device)
{
    return aftermath::Initialize(aftermath::kApiTypeDX12, version, flags,
                                 reinterpret_cast<IUnknown*>(pDx12Device));
}

/* sub_180005570 -- ordinal 2 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX11_Initialize(GFSDK_Aftermath_Version version,
                                                    GFSDK_Aftermath_uint32 flags,
                                                    struct ID3D11Device* const pDx11Device)
{
    return aftermath::Initialize(aftermath::kApiTypeDX11, version, flags,
                                 reinterpret_cast<IUnknown*>(pDx11Device));
}

/* sub_180005580 -- ordinal 3 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_CreateContextHandle(struct ID3D12CommandList* const pDx12CommandList,
                                                             GFSDK_Aftermath_ContextHandle* pOutContextHandle)
{
    return aftermath::CreateContextHandle(aftermath::kApiTypeDX12, pDx12CommandList, pOutContextHandle);
}

/* sub_180005590 -- ordinal 1 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX11_CreateContextHandle(struct ID3D11DeviceContext* const pDx11DeviceContext,
                                                             GFSDK_Aftermath_ContextHandle* pOutContextHandle)
{
    return aftermath::CreateContextHandle(aftermath::kApiTypeDX11, pDx11DeviceContext, pOutContextHandle);
}

/* sub_1800055A0 -- ordinal 8
 *
 * The binary allocates with operator new and releases with free(); both end up in the
 * same CRT heap, but the reconstruction uses a matching new/delete pair. */
GFSDK_Aftermath_API GFSDK_Aftermath_ReleaseContextHandle(const GFSDK_Aftermath_ContextHandle contextHandle)
{
    delete reinterpret_cast<aftermath::ContextHandleImpl*>(contextHandle);
    return GFSDK_Aftermath_Result_Success;
}
