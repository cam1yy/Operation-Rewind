// =============================================================================
//  aftermath_internal.h
// -----------------------------------------------------------------------------
//  Internal declarations shared by the reconstruction.  Nothing in this header
//  is exported; it exists so that the implementation files can be split the way
//  the original binary was structured instead of being one large translation
//  unit.
//
//  Provenance tags used throughout the reconstruction:
//
//    [D]  Recovered from the disassembly (address or immediate value recorded
//         during analysis).
//    [H]  Taken from the published GFSDK_Aftermath header; the DLL's behaviour
//         is consistent with it.
//    [I]  Inferred.  The behaviour is required for the DLL to work, but the
//         exact original construction could not be confirmed from the listing.
//         Every [I] is listed in docs/ANALYSIS_NOTES.md under "Open questions".
// =============================================================================

#ifndef AFTERMATH_INTERNAL_H
#define AFTERMATH_INTERNAL_H

#include "../include/GFSDK_Aftermath.h"

// -----------------------------------------------------------------------------
// Calling convention for the private NVAPI entry points.
//
//   x64 has a single convention, so this is a no-op in practice; it is spelled
//   out because the driver side interfaces are C linkage and the intent should
//   survive a recompile on another architecture.  GCC/Clang do not have the
//   keyword, hence the shim.
// -----------------------------------------------------------------------------
#if defined(_MSC_VER)
#  define AFTERMATH_CDECL __cdecl
#else
#  define AFTERMATH_CDECL
#endif

// -----------------------------------------------------------------------------
// Global state
//
//   The original binary keeps all mutable state in the .data section of the
//   image.  The offsets below are the ones observed in the listing; the names
//   are the ones used by this reconstruction.
//
//     [D] dword_18002029C  0x18002029C   one-time init state
//     [D] qword_1800202A0  0x1800202A0   device pointer (ID3D11Device*/ID3D12Device*)
//     [D] dword_1800202A8  0x1800202A8   GFSDK_Aftermath_FeatureFlags
//     [D] byte_1800202AC   0x1800202AC   "initialized" flag
// -----------------------------------------------------------------------------
namespace aftermath
{
    // [D] dword_18002029C -- guards the one-time process initialization
    //     (nvapi64.dll discovery and driver interface resolution).  The listing
    //     shows the classic three-state interlock: 0 = not started,
    //     1 = in progress, 2 = done.
    enum OneTimeInitState : uint32_t
    {
        OneTimeInit_NotStarted = 0,
        OneTimeInit_InProgress = 1,
        OneTimeInit_Done       = 2
    };

    // Per-API selector.  Every public entry point that comes in a DX11 and a
    // DX12 flavour forwards to a common implementation with this argument
    // pinned.  [D] see the DX11/DX12 export bodies.
    enum Api : uint32_t
    {
        Api_D3D11 = 0,      // [D] GFSDK_Aftermath_DX11_* pass 0
        Api_D3D12 = 1       // [D] GFSDK_Aftermath_DX12_* pass 1
    };

    extern uint32_t g_oneTimeInitState;     // [D] 0x18002029C
    extern void*    g_pDevice;              // [D] 0x1800202A0
    extern uint32_t g_featureFlags;         // [D] 0x1800202A8
    extern bool     g_initialized;          // [D] 0x1800202AC

    // Which flavour of API the stored device belongs to.
    //
    // [I] GFSDK_Aftermath_GetDeviceStatus() and
    //     GFSDK_Aftermath_GetPageFaultInformation() take no context handle and
    //     therefore have no per-call API selector, yet they must still choose
    //     between the D3D11 and the D3D12 driver interface.  The selector has to
    //     come from somewhere in the image's .data section; the location was not
    //     pinned down during the recovery pass, so it is modelled as its own
    //     global here.  See docs/ANALYSIS_NOTES.md, "Open questions", item 3.
    extern Api      g_activeApi;

}

// -----------------------------------------------------------------------------
// Context handle
//
//   GFSDK_Aftermath_DX11/DX12_CreateContextHandle() heap-allocates a 0x18 byte
//   object and returns it as the opaque GFSDK_Aftermath_ContextHandle:
//
//     [D] operator new(0x18)
//     [D] *(uint32_t*)(handle + 4)  = api;        // 0 = D3D11, 1 = D3D12
//     [D] *(void**)(handle + 8)     = pContext;   // ID3D11DeviceContext* / ID3D12CommandList*
//     [D] *(void**)(handle + 16)    = nullptr;    // driver side state
//
//   GFSDK_Aftermath_ReleaseContextHandle() frees the object after checking the
//   initialized flag, so the driver side state at +16 (if any) must be owned by
//   the driver and not by this object.  [I]
//
//   The field at +0 is never read by any of the recovered entry points.  It is
//   carried here so that the allocation keeps its original size and so that the
//   two uint32_t fields land on the offsets the disassembly uses.
// -----------------------------------------------------------------------------
struct GFSDK_Aftermath_ContextHandleImpl
{
    uint32_t reserved0;      // [D] offset 0, never dereferenced by this build
    uint32_t api;            // [D] offset 4, aftermath::Api selector
    void*    pContext;       // [D] offset 8, D3D11 device context / D3D12 command list
    void*    pDriverState;   // [D] offset 16, owned by the driver interface
};

// -----------------------------------------------------------------------------
// Implementation entry points.
//
//   These are the four internal functions that the exported entry points
//   forward to.  Their signatures are the ones recovered from the listing, with
//   the API selector hoisted into the first parameter.
//
//     [D] Initialize          <- GFSDK_Aftermath_DX11/DX12_Initialize
//     [D] CreateContextHandle <- GFSDK_Aftermath_DX11/DX12_CreateContextHandle
//     [D] SetEventMarker      <- dispatch on handle->api
//     [D] GetData             <- dispatch on handle->api, per context
// -----------------------------------------------------------------------------
namespace aftermath
{
    GFSDK_Aftermath_Result Initialize(
        Api                          api,
        GFSDK_Aftermath_Version      version,
        GFSDK_Aftermath_FeatureFlags flags,
        const void*                  pDevice);

    GFSDK_Aftermath_Result CreateContextHandle(
        Api                             api,
        const void*                     pContext,
        GFSDK_Aftermath_ContextHandle*  pHandleOut);

    GFSDK_Aftermath_Result SetEventMarker(
        GFSDK_Aftermath_ContextHandle handle,
        const void*                   markerData,
        uint32_t                      markerDataSize);

    GFSDK_Aftermath_Result GetData(
        uint32_t                             numContexts,
        const GFSDK_Aftermath_ContextHandle* pContextHandles,
        GFSDK_Aftermath_ContextData*         pContextDataOut);

    GFSDK_Aftermath_Result GetDeviceStatus(GFSDK_Aftermath_Device_Status* pStatus);

    GFSDK_Aftermath_Result GetPageFaultInformation(
        GFSDK_Aftermath_PageFaultInformation* pPageFaultInfo);

    // -------------------------------------------------------------------------
    // ReleaseContextHandle
    //
    //   No API selector and no dispatch: the handle owns its own storage.
    // -------------------------------------------------------------------------
    GFSDK_Aftermath_Result ReleaseContextHandle(
        GFSDK_Aftermath_ContextHandle handle);

    // -------------------------------------------------------------------------
    // Shutdown
    //
    //   Called from the DLL's detach path.  Resets the library state and drops
    //   the cached driver interfaces.
    // -------------------------------------------------------------------------
    void Shutdown();
}

// -----------------------------------------------------------------------------
// Result code helpers.
//
//   Several recovered call sites build a failure code by adding an offset to
//   0xBAD00000 rather than by referencing the enumerator, which is why the
//   listing shows negative immediates such as -0x452FFFF1.  The helpers below
//   make those sites readable again.
// -----------------------------------------------------------------------------
#define AFTERMATH_FAIL(code) \
    ((GFSDK_Aftermath_Result)(0xBAD00000u + (uint32_t)(code)))

static inline bool AftermathSucceeded(GFSDK_Aftermath_Result result)
{
    return result == GFSDK_Aftermath_Result_Success;
}

#endif // AFTERMATH_INTERNAL_H
