// =============================================================================
//  aftermath_internal.h
// -----------------------------------------------------------------------------
//  Internal declarations shared by the reconstruction.  Nothing in this header
//  is exported.
//
//  Provenance tags used throughout:
//
//    [D]  Recovered from the listing.  Every address and immediate quoted in
//         this tree is now taken directly from the decompiled source rather
//         than inferred.
//    [H]  Taken from the published Aftermath header.
//    [I]  Inferred.  Listed in docs/ANALYSIS_NOTES.md under "Open questions".
// =============================================================================

#ifndef AFTERMATH_INTERNAL_H
#define AFTERMATH_INTERNAL_H

#include "../include/GFSDK_Aftermath.h"

// -----------------------------------------------------------------------------
// Interlocked primitives.
//
//   The original uses _InterlockedAdd and _InterlockedCompareExchange.  The GCC
//   builtins are used off Windows so the host test can exercise the same code.
// -----------------------------------------------------------------------------
#if defined(_MSC_VER)
#  include <intrin.h>
#  define AFTERMATH_INTERLOCKED_INC(ptr) \
       _InterlockedIncrement(reinterpret_cast<volatile long*>(ptr))
#  define AFTERMATH_INTERLOCKED_DEC(ptr) \
       _InterlockedDecrement(reinterpret_cast<volatile long*>(ptr))
#  define AFTERMATH_INTERLOCKED_CAS(ptr, exchange, comparand) \
       _InterlockedCompareExchange(reinterpret_cast<volatile long*>(ptr), \
                                   (exchange), (comparand))
#else
#  define AFTERMATH_INTERLOCKED_INC(ptr) __sync_add_and_fetch((ptr), 1)
#  define AFTERMATH_INTERLOCKED_DEC(ptr) __sync_sub_and_fetch((ptr), 1)
#  define AFTERMATH_INTERLOCKED_CAS(ptr, exchange, comparand) \
       __sync_val_compare_and_swap((ptr), (comparand), (exchange))
#endif

namespace aftermath
{
    // -------------------------------------------------------------------------
    // Global state.
    //
    //   The image keeps all mutable state in .data.  Note that two of the four
    //   Aftermath globals are NOT what a first reading suggests:
    //
    //     [D] 0x18001B9D8  dword_18001B9D8   driver call refcount, bumped by
    //                                        every NVAPI thunk
    //     [D] 0x18002029C  dword_18002029C   Initialize() guard -- see the very
    //                                        important note below
    //     [D] 0x1800202A0  qword_1800202A0   the driver handle for the DEVICE
    //                                        (not the device pointer!)
    //     [D] 0x1800202A8  dword_1800202A8   GFSDK_Aftermath_FeatureFlags
    //     [D] 0x1800202AC  byte_1800202AC    "initialized" flag
    // -------------------------------------------------------------------------

    // [D] 0x18001B9D8 -- incremented before and decremented after every driver
    //     call, in all thirteen thunks.  It serialises driver calls against
    //     whatever the loader's busy flag coordinates, and is never read
    //     anywhere else in the image.
    extern volatile int32_t g_driverRefCount;

    // [D] 0x18002029C -- the Initialize() guard.
    //
    //     ------------------------------------------------------------------
    //     This is NOT a one-time-init interlock, despite looking like one.
    //     The recovered code is:
    //
    //         if ( _InterlockedCompareExchange(&dword_18002029C, 1, 0) == 0
    //              || byte_1800202AC != 0 )
    //         {
    //             ... the real initialisation ...
    //         }
    //         return 1;                     // success either way
    //
    //     Two consequences, both reproduced here:
    //
    //       1. The value is set to 1 and never reset.  A second
    //          GFSDK_Aftermath_*_Initialize() call therefore takes the
    //          `byte_1800202AC != 0` branch only if the first one succeeded;
    //          if the first one FAILED, every later call falls straight through
    //          to `return 1` and reports success without doing anything.
    //
    //       2. On failure the function still returns 1
    //          (GFSDK_Aftermath_Result_Success) from that leaf, because the
    //          fall-through return is unconditional.  Only the early `return`
    //          statements inside the guarded block produce failure codes.
    //     ------------------------------------------------------------------
    extern uint32_t g_initGuard;

    // [D] 0x1800202A0 -- the driver handle produced by the device Attach call in
    //     Initialize(), NOT the ID3D11Device*/ID3D12Device* the caller passed.
    extern void* g_deviceDriverHandle;

    // [D] 0x1800202A8 -- the feature flags of the successful Initialize().
    extern uint32_t g_featureFlags;

    // [D] 0x1800202AC -- "a successful Initialize() has happened".
    //     Every entry point except Initialize() itself tests this byte first.
    extern bool g_initialized;

    // -------------------------------------------------------------------------
    // Per-API selector.  Every public entry point that comes in a DX11 and a
    // DX12 flavour forwards to a common implementation with this pinned.
    //
    //   [D] GFSDK_Aftermath_DX11_* pass 0, GFSDK_Aftermath_DX12_* pass 1.
    // -------------------------------------------------------------------------
    enum Api : uint32_t
    {
        Api_D3D11 = 0,
        Api_D3D12 = 1
    };
}

// -----------------------------------------------------------------------------
// Context handle.
//
//   CreateContextHandle() heap-allocates 0x18 bytes.  The recovered stores are
//   [D]:
//
//       handle = operator new(0x18);
//       *handle           = 0;                      // 8 bytes at +0
//       handle[2]         = 0;                      // 8 bytes at +16
//       *(uint32_t*)(handle + 4) = api;             // +4, upper half of the
//                                                   // zeroed qword
//       handle[1]         = pContext;               // +8
//       handle[2]         = 0;
//       Attach(api, pContext, &handle[2]);          // driver handle -> +16
//
//   So +0 and +4 are one zeroed 64-bit slot whose upper half is then overwritten
//   by the API selector, and +16 is the DRIVER HANDLE returned by the attach
//   call.  Reproduction of the original's deliberate double-store of handle[2]
//   would be pointless, so +16 is simply left for Attach() to fill.
//
//   SetEventMarker() and GetData() dispatch on `api` at +4 and pass the driver
//   handle at +16; nothing ever reads +0.
// -----------------------------------------------------------------------------
struct GFSDK_Aftermath_ContextHandleImpl
{
    uint32_t reserved0;      // [D] +0  first half of the zeroed qword; never read
    uint32_t api;            // [D] +4  aftermath::Api selector
    void*    pD3DObject;     // [D] +8  ID3D11DeviceContext* / ID3D12CommandList*
    void*    pDriverHandle;  // [D] +16 driver handle from the Attach call
};

// -----------------------------------------------------------------------------
// Implementation entry points.
// -----------------------------------------------------------------------------
namespace aftermath
{
    // The shared implementations behind the DX11 and DX12 export pairs.
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

    GFSDK_Aftermath_Result ReleaseContextHandle(
        GFSDK_Aftermath_ContextHandle handle);

    // Called from the module detach path.  Not part of the original binary.
    void Shutdown();

#ifdef AFTERMATH_TEST_BUILD
    // Host-test helper: clears the Initialize() guard, which the original sets
    // once and never resets, so that a test binary can run more than one
    // Initialize() scenario.
    void TestResetInitializeGuard();
#endif
}

// -----------------------------------------------------------------------------
// Result code helpers.
// -----------------------------------------------------------------------------
static inline bool AftermathSucceeded(GFSDK_Aftermath_Result result)
{
    return result == GFSDK_Aftermath_Result_Success;
}

#endif // AFTERMATH_INTERNAL_H
