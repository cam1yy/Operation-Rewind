// =============================================================================
//  aftermath_globals.cpp
// -----------------------------------------------------------------------------
//  The library's mutable state.
//
//  The original binary keeps these in .data at the addresses noted in
//  aftermath_internal.h.  They are gathered into one translation unit here so
//  that the initialisation order is obvious and so that the shutdown path has a
//  single place to reset from.
// =============================================================================

#include "aftermath_internal.h"

namespace aftermath
{
    // -------------------------------------------------------------------------
    // [D] 0x18002029C -- one-time process initialisation interlock.
    //
    //   Zero-initialised in the image, i.e. OneTimeInit_NotStarted.  The
    //   loader's caching layer performs the equivalent job of the interlock
    //   today; see nvapi/nvapi_loader.cpp.  Keeping the variable makes the
    //   state observable for the shutdown path and documents the original
    //   layout.
    // -------------------------------------------------------------------------
    uint32_t g_oneTimeInitState = OneTimeInit_NotStarted;

    // -------------------------------------------------------------------------
    // [D] 0x1800202A0 -- the device that was passed to the most recent
    //     successful Initialize().  Only one device is supported at a time; a
    //     second successful initialization replaces this pointer.
    // -------------------------------------------------------------------------
    void* g_pDevice = nullptr;

    // -------------------------------------------------------------------------
    // [D] 0x1800202A8 -- the GFSDK_Aftermath_FeatureFlags of the successful
    //     Initialize().  Read by SetEventMarker (bit 0), GetData (bit 0) and
    //     GetPageFaultInformation (bit 1); every other bit is stored and
    //     forwarded to the driver but never inspected by this library.
    // -------------------------------------------------------------------------
    uint32_t g_featureFlags = 0;

    // -------------------------------------------------------------------------
    // [D] 0x1800202AC -- "a successful Initialize() has happened".
    //
    //   Every entry point except the two Initialize flavours tests this byte
    //   first and answers GFSDK_Aftermath_Result_FAIL_NotInitialized when it is
    //   clear.
    // -------------------------------------------------------------------------
    bool g_initialized = false;

    // -------------------------------------------------------------------------
    // [I] See aftermath_internal.h.
    // -------------------------------------------------------------------------
    Api g_activeApi = Api_D3D11;
}
