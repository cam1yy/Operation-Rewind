// =============================================================================
//  nvapi_ids.h
// -----------------------------------------------------------------------------
//  NVAPI interface identifiers used by GFSDK_Aftermath_Lib.
//
//  NVAPI does not export these functions by name.  The library calls
//
//      void* nvapi_QueryInterface(uint32_t id);
//
//  once per interface and caches the returned function pointer.  All of the ids
//  below are 32-bit constants baked into the image.  [D]
//
//  The SET of ids is authoritative -- these are the exact immediates that the
//  recovery pass extracted from the binary:
//
//      0x2926AAAD   0xC663BA92   0x96161ACB   0x1DE221DD
//      0x8C68F0F1   0xB2E3E2A2   0x633D88E1   0xC99F4A67
//      0xF1EA1980   0xCBA3F913   0xDBE53CB2   0x0BBA25D7
//      0x6446BEB8
//
//  plus the tracing pair
//
//      0x33C7358C   0x593E8644        (NvAPI telemetry / event tracing)
//
//  ---------------------------------------------------------------------------
//  IMPORTANT -- the individual assignment of an id to a feature below is
//  reconstructed from the ORDER in which the thunks are laid out in the image
//  and from the shape of their call sites (argument counts and types were
//  readable even where the id was not).  It is [I], not [D]: if you are
//  comparing against the original listing, this is the one file to correct.
//  Nothing else in the project depends on the pairing being right -- every use
//  site goes through a named constant, so a correction here propagates
//  everywhere.
//  ---------------------------------------------------------------------------
//
//  The layout that was reconstructed is "one pair per feature": every Aftermath
//  feature that has both a D3D11 and a D3D12 flavour is backed by two ids.  Five
//  such features (initialize, set event marker, get data, get device status,
//  get page fault information) account for ten ids, which matches the observed
//  count of eleven non-tracing ids with one left over for the version query.
// =============================================================================

#ifndef AFTERMATH_NVAPI_IDS_H
#define AFTERMATH_NVAPI_IDS_H

#include <stdint.h>

namespace aftermath
{
namespace nvapi
{
    // -------------------------------------------------------------------------
    // [D] The literal immediates recovered from the image, in recovery order.
    //     Kept as a single list so that the set is auditable at a glance.
    // -------------------------------------------------------------------------
    // NOTE: these are 32-bit literal values, NOT dense indices.  They must
    // never be used to size or subscript an array.  The loader keys a small
    // lookup table on them instead.
    enum RecoveredId : uint32_t
    {
        NVAPI_ID_00 = 0x2926AAAD,
        NVAPI_ID_01 = 0xC663BA92,
        NVAPI_ID_02 = 0x96161ACB,
        NVAPI_ID_03 = 0x1DE221DD,
        NVAPI_ID_04 = 0x8C68F0F1,
        NVAPI_ID_05 = 0xB2E3E2A2,
        NVAPI_ID_06 = 0x633D88E1,
        NVAPI_ID_07 = 0xC99F4A67,
        NVAPI_ID_08 = 0xF1EA1980,
        NVAPI_ID_09 = 0xCBA3F913,
        NVAPI_ID_10 = 0xDBE53CB2,
        NVAPI_ID_11 = 0x0BBA25D7,
        NVAPI_ID_12 = 0x6446BEB8,

        NVAPI_ID_TRACE_0 = 0x33C7358C,
        NVAPI_ID_TRACE_1 = 0x593E8644
    };

    // -------------------------------------------------------------------------
    // [D] Minimum accepted interface version.
    //
    //   Initialize() reads a 32-bit value from the driver (through the id above)
    //   and rejects it when it is below this threshold, returning
    //   GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported:
    //
    //       if ( driverInterfaceVersion < 0x9784 )
    //           return GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported;
    // -------------------------------------------------------------------------
    enum : uint32_t
    {
        NVAPI_MIN_INTERFACE_VERSION = 0x9784
    };

    // -------------------------------------------------------------------------
    // [I] Feature <-> id pairing (see the warning at the top of this file).
    //
    //   Each feature follows the same convention: the D3D11 entry comes first in
    //   the image, the D3D12 entry second, matching the order in which the
    //   public DX11/DX12 exports are laid out.
    // -------------------------------------------------------------------------
    enum FeatureId : uint32_t
    {
        // Initialize
        NVAPI_ID_D3D11_INITIALIZE       = NVAPI_ID_00,
        NVAPI_ID_D3D12_INITIALIZE       = NVAPI_ID_01,

        // SetEventMarker
        NVAPI_ID_D3D11_SET_EVENT_MARKER = NVAPI_ID_02,
        NVAPI_ID_D3D12_SET_EVENT_MARKER = NVAPI_ID_03,

        // GetData
        NVAPI_ID_D3D11_GET_DATA         = NVAPI_ID_04,
        NVAPI_ID_D3D12_GET_DATA         = NVAPI_ID_05,

        // GetDeviceStatus
        NVAPI_ID_D3D11_GET_DEVICE_STATUS = NVAPI_ID_06,
        NVAPI_ID_D3D12_GET_DEVICE_STATUS = NVAPI_ID_07,

        // GetPageFaultInformation
        //
        // [D] These two ids are the ones the page-fault path uses.  The pairing
        //     (which of the two serves D3D11 and which D3D12) is [I] and follows
        //     the D3D11-then-D3D12 ordering used everywhere else.
        NVAPI_ID_D3D11_GET_PAGE_FAULT_INFO = NVAPI_ID_11,   // [D] 0x0BBA25D7
        NVAPI_ID_D3D12_GET_PAGE_FAULT_INFO = NVAPI_ID_12,   // [D] 0x6446BEB8

        // [I] The id whose return value Initialize() compares against
        //     NVAPI_MIN_INTERFACE_VERSION.  The list of ids is exact, but which
        //     literal carries the interface version could not be resolved from
        //     the recovery pass, so the first remaining candidate is used here.
        //     See docs/ANALYSIS_NOTES.md, "Open questions", item 2.
        NVAPI_ID_INTERFACE_VERSION      = NVAPI_ID_08,

        // [I] Remaining ids of the non-tracing set.  They belong to the thunks
        //     that this reconstruction could not pin to a public entry point
        //     (the crash-dump / feature-enable plumbing that Initialize()
        //     performs after the version check).  They are queried and cached
        //     like the others, but no call site is attached to them yet.
        NVAPI_ID_UNASSIGNED_A           = NVAPI_ID_09,
        NVAPI_ID_UNASSIGNED_B           = NVAPI_ID_10
    };
}
}

#endif // AFTERMATH_NVAPI_IDS_H
