// =============================================================================
//  nvapi_loader.h
// -----------------------------------------------------------------------------
//  Discovery and caching of the NVAPI entry point.
//
//  The library does not link against nvapi64.lib: it locates nvapi64.dll at
//  run time, resolves the single exported query function, and from then on uses
//  it to obtain the private Aftermath interfaces listed in nvapi_ids.h.
//
//  Corresponds to the recovery region at the top of the image:
//      [D] the module discovery helper
//      [D] the "get query function pointer" helper
//      [D] the one-time initialisation guarded by dword_18002029C
//      [D] the interface version query, whose result Initialize() compares
//          against NVAPI_MIN_INTERFACE_VERSION
// =============================================================================

#ifndef AFTERMATH_NVAPI_LOADER_H
#define AFTERMATH_NVAPI_LOADER_H

#include <stdint.h>
#include <stddef.h>

#include "../aftermath_internal.h"

namespace aftermath
{
namespace nvapi
{
    // -------------------------------------------------------------------------
    // Signature of the NVAPI query function.
    //
    //   NVAPI exports this one symbol; everything else is reached through it.
    //   nvapi_QueryInterface() returns a function pointer for the given id, or
    //   null when the id is unknown to the installed driver.
    //
    //   Both spellings are probed because some NVAPI builds ship only the "pep"
    //   (pre-emulation / platform extension package) variant:  [D]
    //
    //       "nvapi_QueryInterface"
    //       "nvapi_pepQueryInterface"
    // -------------------------------------------------------------------------
    typedef void* (AFTERMATH_CDECL * QueryInterfaceFn)(uint32_t id);

    // The DLL name probed at run time.  [D]
    extern const char* const kNvApiModuleName;

    // -------------------------------------------------------------------------
    // EnsureLoaded
    //
    //   Performs the one-time process initialisation exactly once, guarding the
    //   work with the interlock at 0x18002029C so that concurrent callers cannot
    //   race.  Subsequent calls are cheap.
    //
    //   Returns false when nvapi64.dll could not be loaded or when neither query
    //   spelling is present -- callers turn that into
    //   GFSDK_Aftermath_Result_FAIL_NotInitialized.
    // -------------------------------------------------------------------------
    bool EnsureLoaded();

    // -------------------------------------------------------------------------
    // QueryInterface
    //
    //   Returns the cached function pointer for 'id', resolving it on first use.
    //   Returns null when the driver does not expose the id.
    // -------------------------------------------------------------------------
    void* QueryInterface(uint32_t id);

    // -------------------------------------------------------------------------
    // GetInterfaceVersion
    //
    //   Reads the driver interface version through NVAPI_ID_INTERFACE_VERSION.
    //   Returns 0 when the interface is unavailable, which Initialize() treats
    //   as "below the minimum" and reports as
    //   GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported.
    // -------------------------------------------------------------------------
    uint32_t GetInterfaceVersion();

    // -------------------------------------------------------------------------
    // Release
    //
    //   Drops the cached function pointers.  Called from the DLL's shutdown
    //   path.  The nvapi64.dll module handle is intentionally not freed: the
    //   process may have other users of NVAPI and the original library does not
    //   unload it either.  [I]
    // -------------------------------------------------------------------------
    void Release();

    // -------------------------------------------------------------------------
    // Configuration hooks for the host-side test build (see tests/).
    //   On Windows these are never called.
    // -------------------------------------------------------------------------
    void SetQueryInterfaceForTesting(QueryInterfaceFn fn);
}
}

#endif // AFTERMATH_NVAPI_LOADER_H
