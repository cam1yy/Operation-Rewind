/*
 * nvapi_types.h -- the subset of NVAPI this DLL talks to.
 *
 * GFSDK_Aftermath_Lib.x64.dll never links against nvapi64.lib.  It loads nvapi64.dll at
 * run time, resolves the single exported entry point "nvapi_QueryInterface", and then
 * asks it for individual functions by 32 bit interface id.  Everything below is derived
 * from the constants that appear in the 13 thunks at 0x180001200 .. 0x180001C00.
 */

#ifndef GFSDK_AFTERMATH_NVAPI_TYPES_H
#define GFSDK_AFTERMATH_NVAPI_TYPES_H

#include <windows.h>

typedef unsigned long NvU32;
typedef signed   long NvS32;

/* NvAPI_ShortString is char[64]; the driver/branch string buffer in sub_180004690 is a
 * 64 byte stack buffer that is zeroed before the call. */
#define NVAPI_SHORT_STRING_MAX 64
typedef char NvAPI_ShortString[NVAPI_SHORT_STRING_MAX];

/* Opaque Aftermath handles handed out by the driver. */
typedef void* NvAftermathContextHandle;

/*
 * NvAPI_Status -- only the codes this DLL reacts to are listed; the numeric values are
 * the official ones (see NVAPI nvapi_lite_common.h).
 */
typedef enum NvAPI_Status
{
    NVAPI_OK                        = 0,
    NVAPI_ERROR                     = -1,
    NVAPI_LIBRARY_NOT_FOUND         = -2,
    NVAPI_NO_IMPLEMENTATION         = -3,
    NVAPI_API_NOT_INITIALIZED       = -4,
    NVAPI_INVALID_ARGUMENT          = -5,
    NVAPI_NOT_SUPPORTED             = -104,
    NVAPI_OUT_OF_MEMORY             = -130,
    NVAPI_WAS_STILL_DRAWING         = -131,
    NVAPI_FILE_NOT_FOUND            = -132,
    NVAPI_TOO_MANY_UNIQUE_STATE_OBJECTS = -133,
    NVAPI_INVALID_CALL              = -134
} NvAPI_Status;

/*
 * Interface ids passed to nvapi_QueryInterface().
 *
 * The three infrastructure ids are the standard NVAPI ones; the ten Aftermath ids are
 * private to the driver and are named after the way this DLL uses them.
 */
enum NvApiInterfaceId
{
    /* Infrastructure (resolved once, in sub_180001000). */
    kNvApiId_Initialize                        = 0x0150E828u, /* NvAPI_Initialize            */
    kNvApiId_EnterEntryPoint                   = 0x33C7358Cu, /* internal call-trace: enter  */
    kNvApiId_LeaveEntryPoint                   = 0x593E8644u, /* internal call-trace: leave  */

    /* Generic. */
    kNvApiId_SYS_GetDriverAndBranchVersion     = 0x2926AAADu, /* sub_180001200 */

    /* Aftermath, D3D11 flavour. */
    kNvApiId_D3D11_AftermathSetEventMarker     = 0xC663BA92u, /* sub_1800012D0 */
    kNvApiId_D3D11_AftermathGetData            = 0x96161ACBu, /* sub_1800013B0 */
    kNvApiId_D3D11_AftermathCreateHandle       = 0xC99F4A67u, /* sub_180001810 */
    kNvApiId_D3D11_AftermathEnableFeatures     = 0xCBA3F913u, /* sub_1800019B0 */

    /* Aftermath, D3D12 flavour. */
    kNvApiId_D3D12_AftermathSetEventMarker     = 0x8C68F0F1u, /* sub_180001570 */
    kNvApiId_D3D12_AftermathGetData            = 0xB2E3E2A2u, /* sub_180001650 */
    kNvApiId_D3D12_AftermathCreateHandle       = 0xF1EA1980u, /* sub_1800018E0 */
    kNvApiId_D3D12_AftermathEnableFeatures     = 0xDBE53CB2u, /* sub_180001A70 */

    /* Aftermath, device wide queries (primary id + fallback id). */
    kNvApiId_AftermathGetDeviceState           = 0x1DE221DDu, /* sub_1800014A0 */
    kNvApiId_AftermathGetDeviceState2          = 0x633D88E1u, /* sub_180001740 */
    kNvApiId_AftermathGetPageFaultInformation  = 0x0BBA25D7u, /* sub_180001B30 */
    kNvApiId_AftermathGetPageFaultInformation2 = 0x6446BEB8u  /* sub_180001C00 */
};

/*
 * Device state reported by the two AftermathGetDeviceState entry points.  The values are
 * inferred from the switch in sub_1800051D0 that maps them onto
 * GFSDK_Aftermath_Device_Status.
 */
enum NvApiAftermathDeviceState
{
    kNvAftermathDeviceState_Active         = 1,
    kNvAftermathDeviceState_Timeout        = 2,
    kNvAftermathDeviceState_TimeoutAlt1    = 3,
    kNvAftermathDeviceState_TimeoutAlt2    = 4,
    kNvAftermathDeviceState_OutOfMemory    = 5,
    kNvAftermathDeviceState_PageFault      = 6,
    kNvAftermathDeviceState_PageFaultAlt   = 7
};

/* ------------------------------------------------------------------------------------ *
 * Function pointer types, in the order in which the thunks appear in the binary.
 * ------------------------------------------------------------------------------------ */

typedef void* (__cdecl* PfnNvApiQueryInterface)(unsigned int interfaceId);
typedef NvAPI_Status(__cdecl* PfnNvApiInitialize)(void);

/* Internal NVAPI call tracing hooks (no-ops unless an Nsight style tool installs them). */
typedef void (__cdecl* PfnNvApiEnterEntryPoint)(unsigned int interfaceId, ULONG_PTR* pOutToken);
typedef void (__cdecl* PfnNvApiLeaveEntryPoint)(unsigned int interfaceId, ULONG_PTR token, NvAPI_Status status);

typedef NvAPI_Status(__cdecl* PfnNvApiSysGetDriverAndBranchVersion)(NvU32* pDriverVersion, char* szBuildBranchString);
typedef NvAPI_Status(__cdecl* PfnNvApiAftermathCreateHandle)(void* pD3DObject, NvAftermathContextHandle* pOutHandle);
typedef NvAPI_Status(__cdecl* PfnNvApiAftermathEnableFeatures)(NvAftermathContextHandle handle, NvU32 featureFlags);
typedef NvAPI_Status(__cdecl* PfnNvApiAftermathSetEventMarker)(NvAftermathContextHandle handle, const void* pMarkerData, NvU32 markerSize);
typedef NvAPI_Status(__cdecl* PfnNvApiAftermathGetData)(NvAftermathContextHandle handle, void** ppOutMarkerData, NvU32* pOutMarkerSize, NvU32* pOutStatus);
typedef NvAPI_Status(__cdecl* PfnNvApiAftermathGetDeviceState)(NvAftermathContextHandle handle, NvU32* pOutState);
typedef NvAPI_Status(__cdecl* PfnNvApiAftermathGetPageFaultInformation)(NvAftermathContextHandle handle, void* pOutPageFaultInformation);

#endif /* GFSDK_AFTERMATH_NVAPI_TYPES_H */
