/*
 * GFSDK_Aftermath.h
 *
 * Reconstructed public interface of NVIDIA Nsight Aftermath (GFSDK_Aftermath_Lib.x64.dll).
 *
 * This header was rebuilt from the decompiled binary (Hex-Rays / Ghidra / Binary Ninja).
 * Everything in here that is *observable from the binary* is marked as such; the few
 * declarations that are part of the published SDK contract but are only ever passed
 * through the DLL as opaque pointers are marked "not recoverable from the binary".
 *
 * Evidence for the numeric values:
 *   - Version_API == 19 (0x13)        : sub_180004690 rejects any other value with
 *                                       0xBAD00001 (FAIL_VersionMismatch).
 *   - Result codes                    : every 0xBAD000xx constant below appears verbatim
 *                                       in the decompiled control flow.
 *   - Device_Status values 0..4       : sub_1800051D0 maps the NVAPI device state
 *                                       (1..7) onto exactly these five values.
 *   - Context_Status_Invalid == 3     : sub_180004E40 stores 3 in ContextData::status
 *                                       on every failure path.
 *   - sizeof(GFSDK_Aftermath_ContextData) == 16 : sub_180004E40 indexes the output
 *                                       array with a stride of 16 and writes
 *                                       +0 (8 bytes), +8 (4 bytes), +12 (4 bytes).
 */

#ifndef GFSDK_AFTERMATH_H
#define GFSDK_AFTERMATH_H

#ifdef __cplusplus
#   define GFSDK_AFTERMATH_EXTERN_C extern "C"
#else
#   define GFSDK_AFTERMATH_EXTERN_C
#endif

/*
 * Linkage.
 *
 * The shipping DLL exports the nine entry points *by name and by ordinal* through a
 * module definition file (see exports.def).  When building the DLL we therefore do NOT
 * add __declspec(dllexport): doing both makes the linker emit LNK4197 and lets the
 * ordinals drift away from the ones in the original binary.  Clients get the plain
 * declaration (an import library generated from the .def is used for linking).
 */
#if defined(GFSDK_AFTERMATH_BUILD_DLL)
#   define GFSDK_AFTERMATH_DECLSPEC
#elif defined(GFSDK_AFTERMATH_STATIC)
#   define GFSDK_AFTERMATH_DECLSPEC
#else
#   define GFSDK_AFTERMATH_DECLSPEC
#endif

/* x64 has a single calling convention; __cdecl is kept for source compatibility with
 * the 32-bit flavour of the SDK header and is a no-op here. */
#if defined(_MSC_VER)
#   define GFSDK_AFTERMATH_CALL __cdecl
#else
#   define GFSDK_AFTERMATH_CALL
#endif

#define GFSDK_Aftermath_API \
    GFSDK_AFTERMATH_EXTERN_C GFSDK_AFTERMATH_DECLSPEC GFSDK_Aftermath_Result GFSDK_AFTERMATH_CALL

#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef unsigned __int32 GFSDK_Aftermath_uint32;
typedef unsigned __int64 GFSDK_Aftermath_uint64;
#else
#   include <stdint.h>
typedef uint32_t GFSDK_Aftermath_uint32;
typedef uint64_t GFSDK_Aftermath_uint64;
#endif

/* Forward declarations so that this header can be included without the D3D headers.
 * In C++ these name the very same types that d3d11.h / d3d12.h declare. */
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D12Device;
struct ID3D12CommandList;

/*---------------------------------------------------------------------------------------
 * Version
 *-------------------------------------------------------------------------------------*/

/* GFSDK_Aftermath_Version_API is the only value accepted by the two Initialize entry
 * points; 0x0000013 == 19 decimal, which is the literal compared in sub_180004690. */
typedef enum GFSDK_Aftermath_Version
{
    GFSDK_Aftermath_Version_API = 0x0000013
} GFSDK_Aftermath_Version;

/*---------------------------------------------------------------------------------------
 * Results
 *-------------------------------------------------------------------------------------*/

typedef enum GFSDK_Aftermath_Result
{
    GFSDK_Aftermath_Result_Success      = 0x1,
    GFSDK_Aftermath_Result_NotAvailable = 0x2,
    GFSDK_Aftermath_Result_Fail         = 0xBAD00000,

    /* The API version the caller asked for is not the one implemented by this DLL. */
    GFSDK_Aftermath_Result_FAIL_VersionMismatch                       = GFSDK_Aftermath_Result_Fail | 1,
    /* An entry point was called before GFSDK_Aftermath_DX1x_Initialize succeeded. */
    GFSDK_Aftermath_Result_FAIL_NotInitialized                        = GFSDK_Aftermath_Result_Fail | 2,
    GFSDK_Aftermath_Result_FAIL_InvalidAdapter                        = GFSDK_Aftermath_Result_Fail | 3,
    GFSDK_Aftermath_Result_FAIL_InvalidParameter                      = GFSDK_Aftermath_Result_Fail | 4,
    GFSDK_Aftermath_Result_FAIL_Unknown                               = GFSDK_Aftermath_Result_Fail | 5,
    GFSDK_Aftermath_Result_FAIL_ApiError                              = GFSDK_Aftermath_Result_Fail | 6,
    GFSDK_Aftermath_Result_FAIL_NvApiIncompatible                     = GFSDK_Aftermath_Result_Fail | 7,
    GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList  = GFSDK_Aftermath_Result_Fail | 8,
    GFSDK_Aftermath_Result_FAIL_AlreadyInitialized                    = GFSDK_Aftermath_Result_Fail | 9,
    GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible            = GFSDK_Aftermath_Result_Fail | 10,
    GFSDK_Aftermath_Result_FAIL_DriverInitFailed                      = GFSDK_Aftermath_Result_Fail | 11,
    GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported             = GFSDK_Aftermath_Result_Fail | 12,
    GFSDK_Aftermath_Result_FAIL_OutOfMemory                           = GFSDK_Aftermath_Result_Fail | 13,
    GFSDK_Aftermath_Result_FAIL_GetDataOnBundle                       = GFSDK_Aftermath_Result_Fail | 14,
    GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext              = GFSDK_Aftermath_Result_Fail | 15,
    GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled                     = GFSDK_Aftermath_Result_Fail | 16
} GFSDK_Aftermath_Result;

#define GFSDK_Aftermath_SUCCEED(value) (((value) & 0xFFF00000) != GFSDK_Aftermath_Result_Fail)

/*---------------------------------------------------------------------------------------
 * Feature flags
 *
 * Bit 0 gates SetEventMarker/GetData (checked in sub_180004CC0 and sub_180004E40),
 * bit 1 gates GetPageFaultInformation (checked in sub_1800053D0).  The value is stored
 * verbatim at initialisation time and forwarded to NVAPI.
 *-------------------------------------------------------------------------------------*/

typedef enum GFSDK_Aftermath_FeatureFlags
{
    GFSDK_Aftermath_FeatureFlags_Minimum                 = 0x00000000,
    GFSDK_Aftermath_FeatureFlags_EnableMarkers           = 0x00000001,
    GFSDK_Aftermath_FeatureFlags_EnableResourceTracking  = 0x00000002,
    GFSDK_Aftermath_FeatureFlags_Maximum                 = GFSDK_Aftermath_FeatureFlags_EnableMarkers |
                                                           GFSDK_Aftermath_FeatureFlags_EnableResourceTracking
} GFSDK_Aftermath_FeatureFlags;

/*---------------------------------------------------------------------------------------
 * Context handles and context data
 *-------------------------------------------------------------------------------------*/

typedef struct GFSDK_Aftermath_ContextHandle__ { int unused; } *GFSDK_Aftermath_ContextHandle;

typedef enum GFSDK_Aftermath_Context_Status
{
    GFSDK_Aftermath_Context_Status_NotStarted = 0,
    GFSDK_Aftermath_Context_Status_Executing  = 1,
    GFSDK_Aftermath_Context_Status_Finished   = 2,
    GFSDK_Aftermath_Context_Status_Invalid    = 3
} GFSDK_Aftermath_Context_Status;

typedef enum GFSDK_Aftermath_Device_Status
{
    GFSDK_Aftermath_Device_Status_Active      = 0,
    GFSDK_Aftermath_Device_Status_Timeout     = 1,
    GFSDK_Aftermath_Device_Status_OutOfMemory = 2,
    GFSDK_Aftermath_Device_Status_PageFault   = 3,
    GFSDK_Aftermath_Device_Status_Unknown     = 4
} GFSDK_Aftermath_Device_Status;

/*
 * Filled in by GFSDK_Aftermath_GetData.  Layout is recoverable from the binary: the
 * output array is indexed with a 16 byte stride and the three fields are written at
 * +0 (pointer), +8 (uint32) and +12 (uint32).
 *
 * NOTE (observed behaviour): when the library cannot query a context it sets
 *      status = GFSDK_Aftermath_Context_Status_Invalid and stores the
 *      GFSDK_Aftermath_Result that explains why into the markerData slot
 *      (sign extended from 32 bits).  markerSize is left untouched in that case.
 */
typedef struct GFSDK_Aftermath_ContextData
{
    void*                          markerData;
    GFSDK_Aftermath_uint32         markerSize;
    GFSDK_Aftermath_Context_Status status;
} GFSDK_Aftermath_ContextData;

/*
 * Page fault description.
 *
 * NOT RECOVERABLE FROM THE BINARY: GFSDK_Aftermath_GetPageFaultInformation only forwards
 * the caller's pointer to NVAPI, it never dereferences it.  The layout below is the one
 * published by the Aftermath 1.x SDK and is what the driver expects; treat it as part of
 * the ABI contract with the driver rather than as decompiler output.
 */
typedef struct GFSDK_Aftermath_ResourceDescriptor
{
    GFSDK_Aftermath_uint64 size;
    GFSDK_Aftermath_uint32 width;
    GFSDK_Aftermath_uint32 height;
    GFSDK_Aftermath_uint32 depth;
    GFSDK_Aftermath_uint32 mipLevels;
    GFSDK_Aftermath_uint32 format;                          /* DXGI_FORMAT */
    GFSDK_Aftermath_uint32 bIsBufferHeap;
    GFSDK_Aftermath_uint32 bIsStaticTextureHeap;
    GFSDK_Aftermath_uint32 bIsRenderTargetOrDepthStencilViewHeap;
    GFSDK_Aftermath_uint32 bPlacedResource;
    GFSDK_Aftermath_uint32 bWasDestroyed;
} GFSDK_Aftermath_ResourceDescriptor;

typedef struct GFSDK_Aftermath_PageFaultInformation
{
    GFSDK_Aftermath_uint64             faultingGpuVA;
    GFSDK_Aftermath_ResourceDescriptor resourceDesc;
    GFSDK_Aftermath_uint32             bHasPageFaultOccured;
} GFSDK_Aftermath_PageFaultInformation;

/*---------------------------------------------------------------------------------------
 * Entry points (ordinals as exported by the original GFSDK_Aftermath_Lib.x64.dll)
 *-------------------------------------------------------------------------------------*/

/* @1 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX11_CreateContextHandle(
    struct ID3D11DeviceContext* const pDx11DeviceContext,
    GFSDK_Aftermath_ContextHandle* pOutContextHandle);

/* @2 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX11_Initialize(
    GFSDK_Aftermath_Version version,
    GFSDK_Aftermath_uint32 flags,
    struct ID3D11Device* const pDx11Device);

/* @3 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_CreateContextHandle(
    struct ID3D12CommandList* const pDx12CommandList,
    GFSDK_Aftermath_ContextHandle* pOutContextHandle);

/* @4 */
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_Initialize(
    GFSDK_Aftermath_Version version,
    GFSDK_Aftermath_uint32 flags,
    struct ID3D12Device* const pDx12Device);

/* @5 */
GFSDK_Aftermath_API GFSDK_Aftermath_GetData(
    const GFSDK_Aftermath_uint32 numContexts,
    const GFSDK_Aftermath_ContextHandle* pContextHandles,
    GFSDK_Aftermath_ContextData* pOutContextData);

/* @6 */
GFSDK_Aftermath_API GFSDK_Aftermath_GetDeviceStatus(
    GFSDK_Aftermath_Device_Status* pOutStatus);

/* @7 */
GFSDK_Aftermath_API GFSDK_Aftermath_GetPageFaultInformation(
    GFSDK_Aftermath_PageFaultInformation* pOutPageFaultInformation);

/* @8 */
GFSDK_Aftermath_API GFSDK_Aftermath_ReleaseContextHandle(
    const GFSDK_Aftermath_ContextHandle contextHandle);

/* @9 */
GFSDK_Aftermath_API GFSDK_Aftermath_SetEventMarker(
    const GFSDK_Aftermath_ContextHandle contextHandle,
    const void* markerData,
    const GFSDK_Aftermath_uint32 markerSize);

#endif /* GFSDK_AFTERMATH_H */
