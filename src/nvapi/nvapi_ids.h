// =============================================================================
//  nvapi_ids.h
// -----------------------------------------------------------------------------
//  NVAPI interface identifiers used by GFSDK_Aftermath_Lib.
//
//  NVAPI does not export these functions by name.  The library obtains each one
//  by calling
//
//      void* nvapi_QueryInterface(uint32_t id);
//
//  once and caching the returned pointer.  Every id below is now RECOVERED
//  EXACTLY from the listing rather than inferred -- each one appears as an
//  immediate in the corresponding thunk body, and the thunk's own call site in
//  the Aftermath API pins down what it does.
//
//  Derivation for the whole table (all [D]):
//
//      id            thunk           called from                     with
//      ------------  --------------  ------------------------------  ---------------------
//      0x2926AAAD    sub_180001200   Initialize                      (u32* version, void* info)
//      0xC663BA92    sub_1800012D0   SetEventMarker, API == D3D11    (drv, data, size)
//      0x96161ACB    sub_1800013B0   GetData,         API == D3D11    (drv, pData, pSize, pStatus)
//      0x1DE221DD    sub_1800014A0   GetDeviceStatus (tried first)   (drv, u32* status)
//      0x8C68F0F1    sub_180001570   SetEventMarker, API == D3D12    (drv, data, size)
//      0xB2E3E2A2    sub_180001650   GetData,         API == D3D12    (drv, pData, pSize, pStatus)
//      0x633D88E1    sub_180001740   GetDeviceStatus (fallback)      (drv, u32* status)
//      0xC99F4A67    sub_180001810   Initialize + CreateContextHandle, API == D3D11
//      0xF1EA1980    sub_1800018E0   Initialize + CreateContextHandle, API == D3D12
//      0xCBA3F913    sub_1800019B0   Initialize (after attach), D3D11 (drv, flags)
//      0xDBE53CB2    sub_180001A70   Initialize (after attach), D3D12 (drv, flags)
//      0x0BBA25D7    sub_180001B30   GetPageFaultInformation (first) (drv, pInfo)
//      0x6446BEB8    sub_180001C00   GetPageFaultInformation (fallback)
//      0x33C7358C    tracing begin   every thunk                     (id, void** token)
//      0x593E8644    tracing end     every thunk                     (id, token, result)
//
//  The DX11/DX12 split is not guessed: sub_180004690 and sub_180004AC0 branch on
//  the API selector and call 0xC99F4A67/0xF1EA1980 respectively, and
//  SetEventMarker/GetData branch on the word at handle+4 the same way.
// =============================================================================

#ifndef AFTERMATH_NVAPI_IDS_H
#define AFTERMATH_NVAPI_IDS_H

#include <stdint.h>

#include "GFSDK_Aftermath_Defines.h"

namespace aftermath
{
namespace nvapi
{
    // -------------------------------------------------------------------------
    // The recovered ids.  These are 32-bit literals spread across the whole
    // range, so they must never be used to size or subscript an array; the
    // loader keys a small lookup table on them instead.
    // -------------------------------------------------------------------------
    enum InterfaceId : uint32_t
    {
        // Driver interface version + info block.  Returns a driver status and
        // writes the interface version (compared against 0x9784) plus a 0x40
        // byte info block.
        NVAPI_ID_VERSION_INFO            = 0x2926AAAD,

        // Event markers, one entry point per API flavour.
        NVAPI_ID_D3D11_SET_EVENT_MARKER  = 0xC663BA92,
        NVAPI_ID_D3D12_SET_EVENT_MARKER  = 0x8C68F0F1,

        // Marker read-back, one entry point per API flavour.
        NVAPI_ID_D3D11_GET_DATA          = 0x96161ACB,
        NVAPI_ID_D3D12_GET_DATA          = 0xB2E3E2A2,

        // Device status.  GetDeviceStatus() calls the D3D11 one first and falls
        // back to the D3D12 one, which is why they are not selected by an API
        // flag.
        NVAPI_ID_D3D11_DEVICE_STATUS     = 0x1DE221DD,
        NVAPI_ID_D3D12_DEVICE_STATUS     = 0x633D88E1,

        // "Attach to this D3D object and give me a driver handle."
        // Called with the device in Initialize() and with the command
        // list/device context in CreateContextHandle(); the second argument is
        // the address of the slot the driver handle is written to.
        NVAPI_ID_D3D11_ATTACH            = 0xC99F4A67,
        NVAPI_ID_D3D12_ATTACH            = 0xF1EA1980,

        // "Enable these Aftermath features on that handle."  Second argument is
        // the GFSDK_Aftermath_FeatureFlags mask.
        NVAPI_ID_D3D11_ENABLE_FEATURES   = 0xCBA3F913,
        NVAPI_ID_D3D12_ENABLE_FEATURES   = 0xDBE53CB2,

        // Page fault information.  Same first/fallback arrangement as the
        // device status pair.
        NVAPI_ID_PAGE_FAULT_PRIMARY      = 0x0BBA25D7,
        NVAPI_ID_PAGE_FAULT_FALLBACK     = 0x6446BEB8,

        // Nsight tracing bracket.  One pair serves every thunk; the id of the
        // interface being called is passed as the first argument.
        NVAPI_ID_TRACE_BEGIN             = 0x33C7358C,
        NVAPI_ID_TRACE_END               = 0x593E8644
    };

    // -------------------------------------------------------------------------
    // Minimum accepted driver interface version.
    //
    //   sub_180004690 reads a 32-bit value through NVAPI_ID_VERSION_INFO and
    //   rejects it below this threshold with FAIL_DriverVersionNotSupported:
    //
    //       if ( driverInterfaceVersion < 0x9784 )
    //           return 0xBAD0000C;
    // -------------------------------------------------------------------------
    enum : uint32_t
    {
        NVAPI_MIN_INTERFACE_VERSION = 0x9784
    };

    // -------------------------------------------------------------------------
    // Driver-side status codes.
    //
    //   Every driver entry point returns one of these rather than a public
    //   GFSDK_Aftermath_Result.  MapDriverStatus() in aftermath_core.cpp turns
    //   them into public codes.  The values are the immediates that appear in
    //   the switch tables.
    // -------------------------------------------------------------------------
    enum DriverStatus : int32_t
    {
        DriverStatus_Ok               = 0,        // success
        DriverStatus_Fail             = -1,       // generic failure
        DriverStatus_Unavailable      = -3,       // interface could not be resolved
        DriverStatus_InvalidParameter = -5,
        DriverStatus_OutOfMemory      = -0x82,    // -130
        DriverStatus_NewCommandList   = -0x84,    // -132
        DriverStatus_FailA            = -0x83,    // -131
        DriverStatus_FailB            = -0x85,    // -133
        DriverStatus_NvApiA           = -0x68,    // -104
        DriverStatus_NvApiB           = -0x86     // -134
    };

    // -------------------------------------------------------------------------
    // Tracing pair, resolved alongside every other interface.
    // -------------------------------------------------------------------------
    typedef void (AFTERMATH_CDECL * TraceBeginFn)(uint32_t id, void** ppToken);
    typedef void (AFTERMATH_CDECL * TraceEndFn)(uint32_t id, void* token,
                                                int32_t driverStatus);
}
}

#endif // AFTERMATH_NVAPI_IDS_H
