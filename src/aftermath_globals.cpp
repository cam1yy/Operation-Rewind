// =============================================================================
//  aftermath_globals.cpp
// -----------------------------------------------------------------------------
//  The library's mutable state.
//
//  The image keeps these in .data at the addresses noted in
//  aftermath_internal.h.  They are gathered into one translation unit so the
//  layout is auditable at a glance.
// =============================================================================

#include "aftermath_internal.h"

namespace aftermath
{
    // -------------------------------------------------------------------------
    // [D] 0x18001B9D8 -- driver call refcount.
    //
    //   Every NVAPI thunk does `_InterlockedAdd(dword_18001B9D8, 1)` on entry
    //   and `_InterlockedAdd(dword_18001B9D8, -1)` before returning.
    // -------------------------------------------------------------------------
    volatile int32_t g_driverRefCount = 0;

    // -------------------------------------------------------------------------
    // [D] 0x18002029C -- the Initialize() guard.
    //
    //   Zero-initialised in the image.  Set to 1 by the compare-exchange inside
    //   Initialize() and never reset -- see the long note in
    //   aftermath_internal.h for why that matters.
    // -------------------------------------------------------------------------
    uint32_t g_initGuard = 0;

    // -------------------------------------------------------------------------
    // [D] 0x1800202A0 -- the DRIVER HANDLE for the device.
    //
    //   This is written by the device Attach call inside Initialize(), not by
    //   the caller's device pointer.  GetDeviceStatus() and
    //   GetPageFaultInformation() pass it straight back to the driver.
    // -------------------------------------------------------------------------
    void* g_deviceDriverHandle = nullptr;

    // -------------------------------------------------------------------------
    // [D] 0x1800202A8 -- the feature flags of the successful Initialize().
    //
    //   Read by SetEventMarker (bit 0), GetData (bit 0) and
    //   GetPageFaultInformation (bit 1).  Every other bit is stored and
    //   forwarded to the driver but never inspected by this library.
    // -------------------------------------------------------------------------
    uint32_t g_featureFlags = 0;

    // -------------------------------------------------------------------------
    // [D] 0x1800202AC -- "a successful Initialize() has happened".
    // -------------------------------------------------------------------------
    bool g_initialized = false;
}
