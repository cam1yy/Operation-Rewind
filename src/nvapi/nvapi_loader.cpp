// =============================================================================
//  nvapi_loader.cpp
// -----------------------------------------------------------------------------
//  Implementation of the NVAPI discovery / caching layer.  See the header for
//  the mapping back to the disassembly.
// =============================================================================

#include "nvapi_loader.h"
#include "nvapi_ids.h"
#include "../aftermath_internal.h"

#ifndef AFTERMATH_TEST_BUILD
#  include <windows.h>
#else
#  include <string.h>
#endif

namespace aftermath
{
namespace nvapi
{
    // [D] The module name the image probes.  Note the explicit "64": the x86
    //     build of the same library probes "nvapi.dll".  This project only
    //     reconstructs the x64 image.
    const char* const kNvApiModuleName = "nvapi64.dll";

    // [D] Export names, probed in this order.
    static const char* const kQueryInterfaceNames[] =
    {
        "nvapi_QueryInterface",
        "nvapi_pepQueryInterface"
    };
    static const size_t kQueryInterfaceNameCount =
        sizeof(kQueryInterfaceNames) / sizeof(kQueryInterfaceNames[0]);

    // -------------------------------------------------------------------------
    // Cached state.
    //
    //   This mirrors the per-interface slots the original keeps in its .data
    //   section.  The slot array is indexed by RecoveredId, which is why the
    //   ids in nvapi_ids.h are declared as a contiguous 0..N enumeration with
    //   the ordering "all non-tracing ids, then the tracing pair".
    // -------------------------------------------------------------------------
    static QueryInterfaceFn s_queryInterface = nullptr;
    static bool             s_loadAttempted  = false;

    // Resolved-interface cache.
    //
    //   A small open-addressed table keyed on the id, NOT an array indexed by
    //   it: the NVAPI ids are 32-bit literals spread over the whole range (the
    //   largest here is 0xF1EA1980), so they cannot be used as subscripts.
    //
    //   kMaxCachedInterfaces covers the thirteen non-tracing ids recovered from
    //   the image plus room to grow.
    enum { kMaxCachedInterfaces = 16 };

    struct CachedInterface
    {
        uint32_t id;
        void*    fn;
    };

    static CachedInterface s_cache[kMaxCachedInterfaces] = { { 0, nullptr } };
    static size_t          s_cacheCount = 0;

    static uint32_t         s_interfaceVersion = 0;
    static bool             s_versionQueried   = false;

    // -------------------------------------------------------------------------
    // LoadNvApiModule
    //
    //   GetModuleHandleA() first: if the application (or another component) has
    //   already loaded NVAPI, reuse that module rather than pinning a second
    //   copy.  Only when it is absent is LoadLibraryA() used.  [D] this two step
    //   pattern is visible in the recovery region.
    // -------------------------------------------------------------------------
#ifndef AFTERMATH_TEST_BUILD
    static HMODULE LoadNvApiModule()
    {
        HMODULE module = ::GetModuleHandleA(kNvApiModuleName);
        if (module == nullptr)
        {
            module = ::LoadLibraryA(kNvApiModuleName);
        }
        return module;
    }
#endif

    // -------------------------------------------------------------------------
    // ResolveQueryInterface
    // -------------------------------------------------------------------------
    static QueryInterfaceFn ResolveQueryInterface()
    {
#ifndef AFTERMATH_TEST_BUILD
        HMODULE module = LoadNvApiModule();
        if (module == nullptr)
        {
            // No NVAPI at all on this system.  Callers surface this as
            // GFSDK_Aftermath_Result_FAIL_NotInitialized.
            return nullptr;
        }

        for (size_t i = 0; i < kQueryInterfaceNameCount; ++i)
        {
            FARPROC proc = ::GetProcAddress(module, kQueryInterfaceNames[i]);
            if (proc != nullptr)
            {
                return reinterpret_cast<QueryInterfaceFn>(proc);
            }
        }
        return nullptr;
#else
        // The host-side test build injects the query function directly; there is
        // no nvapi64.dll to load.  Returning null here exercises the
        // "NVAPI unavailable" path unless a stub has been installed.
        return nullptr;
#endif
    }

    // -------------------------------------------------------------------------
    // EnsureLoaded
    // -------------------------------------------------------------------------
    bool EnsureLoaded()
    {
        // Already resolved.
        if (s_queryInterface != nullptr)
        {
            return true;
        }

        // A previous attempt failed; do not retry on every call.  The original
        // records the failure state in the interlock at 0x18002029C and returns
        // FAIL_NotInitialized from then on.  [D]
        if (s_loadAttempted)
        {
            return false;
        }

        s_loadAttempted = true;
        s_queryInterface = ResolveQueryInterface();

        return s_queryInterface != nullptr;
    }

    // -------------------------------------------------------------------------
    // QueryInterface
    // -------------------------------------------------------------------------
    void* QueryInterface(uint32_t id)
    {
        // Already resolved?
        for (size_t i = 0; i < s_cacheCount; ++i)
        {
            if (s_cache[i].id == id)
            {
                return s_cache[i].fn;
            }
        }

        if (!EnsureLoaded())
        {
            return nullptr;
        }

        void* fn = s_queryInterface(id);

        // Cache negative results too: a driver that does not implement an id
        // will not start implementing it during this process, and re-querying
        // on every call would be wasteful.  [I]
        if (s_cacheCount < kMaxCachedInterfaces)
        {
            s_cache[s_cacheCount].id = id;
            s_cache[s_cacheCount].fn = fn;
            ++s_cacheCount;
        }

        return fn;
    }

    // -------------------------------------------------------------------------
    // GetInterfaceVersion
    // -------------------------------------------------------------------------
    uint32_t GetInterfaceVersion()
    {
        if (s_versionQueried)
        {
            return s_interfaceVersion;
        }

        s_versionQueried = true;

        // The version interface is a plain "return a uint32_t" entry point.  [I]
        // The recovered call site reads the value and compares it against
        // NVAPI_MIN_INTERFACE_VERSION, which is all the contract needs to be.
        typedef uint32_t(AFTERMATH_CDECL * GetVersionFn)(void);

        GetVersionFn fn = reinterpret_cast<GetVersionFn>(
            QueryInterface(NVAPI_ID_INTERFACE_VERSION));

        if (fn == nullptr)
        {
            // Reported by Initialize() as FAIL_DriverVersionNotSupported
            // (0 is below every real interface version).
            s_interfaceVersion = 0;
        }
        else
        {
            s_interfaceVersion = fn();
        }

        return s_interfaceVersion;
    }

    // -------------------------------------------------------------------------
    // Release
    // -------------------------------------------------------------------------
    void Release()
    {
        for (size_t i = 0; i < kMaxCachedInterfaces; ++i)
        {
            s_cache[i].id = 0;
            s_cache[i].fn = nullptr;
        }
        s_cacheCount      = 0;
        s_interfaceVersion = 0;
        s_versionQueried   = false;

        // Deliberately keeping s_queryInterface and s_loadAttempted: the module
        // stays loaded for the lifetime of the process, so re-resolving would
        // only repeat work.  [I]
    }

    // -------------------------------------------------------------------------
    // Test hook
    // -------------------------------------------------------------------------
    void SetQueryInterfaceForTesting(QueryInterfaceFn fn)
    {
        s_queryInterface = fn;
        s_loadAttempted  = (fn != nullptr);
        Release();
        s_loadAttempted  = (fn != nullptr);
    }
}
}
