// =============================================================================
//  nvapi_loader.h
// -----------------------------------------------------------------------------
//  Discovery and caching of the NVAPI entry points.
//
//  The library does not link against nvapi64.lib.  It locates nvapi64.dll at
//  run time, resolves the single exported query function, and from then on uses
//  it to obtain the private Aftermath interfaces listed in nvapi_ids.h.
//
//  Recovered from the listing:
//
//    sub_180001000(module, flavour)   probe the query function
//    sub_1800010E0(flavour)           load nvapi64.dll (0) or nvpowerapi.dll (1)
//    sub_180001180(flavour)           ensure loaded, with a bounded wait
//    sub_180001200 .. sub_180001C00   the thirteen interface thunks
// =============================================================================

#ifndef AFTERMATH_NVAPI_LOADER_H
#define AFTERMATH_NVAPI_LOADER_H

#include <stdint.h>
#include <stddef.h>

#include "nvapi_ids.h"
#include "../aftermath_internal.h"

namespace aftermath
{
namespace nvapi
{
    // -------------------------------------------------------------------------
    // Signature of the NVAPI query function.
    //
    //   NVAPI exports this one symbol; everything else is reached through it.
    //   It returns a function pointer for the given id, or null when the id is
    //   unknown to the installed driver.
    // -------------------------------------------------------------------------
    typedef void* (AFTERMATH_CDECL * QueryInterfaceFn)(uint32_t id);

    // -------------------------------------------------------------------------
    // Module flavours.
    //
    //   sub_1800010E0 picks the DLL name from its argument:
    //       flavour 0 -> "nvapi64.dll"
    //       flavour 1 -> "nvpowerapi.dll"
    //   and sub_180001000 picks the export name the same way:
    //       flavour 0 -> "nvapi_QueryInterface"
    //       flavour 1 -> "nvapi_pepQueryInterface"
    //
    //   Only flavour 0 is used by any recovered call site -- every thunk calls
    //   sub_180001180(0) -- but the flavour 1 path is fully present in the
    //   image, so it is modelled here rather than dropped.
    // -------------------------------------------------------------------------
    enum ModuleFlavour : int
    {
        ModuleFlavour_NvApi        = 0,     // nvapi64.dll
        ModuleFlavour_NvPowerApi   = 1,     // nvpowerapi.dll
        ModuleFlavour_Count        = 2
    };

    // Driver status values these functions return.  See nvapi_ids.h.
    enum : int32_t
    {
        EnsureLoaded_Timeout      = -1,
        EnsureLoaded_LoadFailed   = -2
    };

    // -------------------------------------------------------------------------
    // EnsureLoaded
    //
    //   sub_180001180.  Returns 0 on success, or a negative driver status:
    //       -1   the flavour's "loading" flag stayed set for ten 100 ms polls
    //            (one second), i.e. another thread is stuck in the loader
    //       -2   the module could not be loaded
    //       or whatever sub_180001000 returned.
    //
    //   The recovered body reads a per-flavour "busy" byte and waits on it:
    //
    //       while ( byte_18001B9D0[flavour] != 0 )
    //       {
    //           Sleep(100);
    //           if ( ++tries >= 10 ) return -1;
    //       }
    //       if ( module[flavour] != null ) return 0;
    //       return LoadModule(flavour);
    // -------------------------------------------------------------------------
    int32_t EnsureLoaded(int flavour = ModuleFlavour_NvApi);

    // -------------------------------------------------------------------------
    // ResolveInterface
    //
    //   The common body of every thunk: return the cached function pointer for
    //   'id', querying NVAPI on first use.  Returns null when the id is unknown
    //   or the module is unavailable -- callers turn that into
    //   DriverStatus_Unavailable (-3).
    // -------------------------------------------------------------------------
    void* ResolveInterface(uint32_t id);

    // -------------------------------------------------------------------------
    // Tracing bracket (ids 0x33C7358C / 0x593E8644).
    //
    //   These are resolved and cached by sub_180001000 rather than on demand,
    //   and every thunk brackets its driver call with them.  Both may be null,
    //   in which case the thunk simply skips them.
    // -------------------------------------------------------------------------
    TraceBeginFn GetTraceBegin();
    TraceEndFn   GetTraceEnd();

    // -------------------------------------------------------------------------
    // ReadInterfaceVersion
    //
    //   sub_180001200 called with a null info block: asks the driver for its
    //   interface version.  Returns the driver status; on success
    //   *pVersionOut holds the version to compare against
    //   NVAPI_MIN_INTERFACE_VERSION.
    // -------------------------------------------------------------------------
    int32_t ReadInterfaceVersion(uint32_t* pVersionOut);

    // -------------------------------------------------------------------------
    // ReadInterfaceInfo
    //
    //   sub_180001200 with the real 0x40 byte info block.  Used by
    //   GFSDK_Aftermath_DX12_Initialize() to look for a debug layer before the
    //   device is handed to the driver.
    // -------------------------------------------------------------------------
    int32_t ReadInterfaceInfo(uint32_t* pVersionOut, void* pInfoOut);

    // -------------------------------------------------------------------------
    // Module lifetime.
    //
    //   Release drops the cached interface pointers and the trace pair.  As in
    //   the original, the modules themselves are never freed: the process may
    //   have other NVAPI users and the library has no unload hook.
    // -------------------------------------------------------------------------
    void Release();

    // -------------------------------------------------------------------------
    // Configuration hooks for the host-side test build (see tests/).
    //   Unused on Windows.
    // -------------------------------------------------------------------------
    void SetQueryInterfaceForTesting(QueryInterfaceFn fn);
}
}

#endif // AFTERMATH_NVAPI_LOADER_H
