// =============================================================================
//  nvapi_loader.cpp
// -----------------------------------------------------------------------------
//  Implementation of the NVAPI discovery / caching layer.
//
//  Mirrors the recovered functions one for one:
//
//      ProbeQueryInterface(module, flavour)  <- sub_180001000
//      LoadModule(flavour)                   <- sub_1800010E0
//      EnsureLoaded(flavour)                 <- sub_180001180
//      CallVersionInfo(...)                  <- sub_180001200
//      ResolveInterface(id)                  <- the shared tail of every thunk
// =============================================================================

#include "nvapi_loader.h"
#include "nvapi_ids.h"
#include "../aftermath_internal.h"

#ifndef AFTERMATH_TEST_BUILD
#  include <windows.h>
#endif

#include <string.h>

namespace aftermath
{
namespace nvapi
{
    // -------------------------------------------------------------------------
    // Module names and export names, indexed by ModuleFlavour.
    //
    //   [D] sub_1800010E0 selects the DLL name with
    //           v3 = L"nvpowerapi.dll";
    //           if ( a1 == 0 ) v3 = L"nvapi64.dll";
    //       and sub_180001000 selects the export with
    //           a2 == 0 -> "nvapi_QueryInterface"
    //           a2 == 1 -> "nvapi_pepQueryInterface"
    // -------------------------------------------------------------------------
#ifndef AFTERMATH_TEST_BUILD

    static const char* const kModuleNames[ModuleFlavour_Count] =
    {
        "nvapi64.dll",              // ModuleFlavour_NvApi
        "nvpowerapi.dll"            // ModuleFlavour_NvPowerApi
    };

    static const char* const kQueryExportNames[ModuleFlavour_Count] =
    {
        "nvapi_QueryInterface",     // ModuleFlavour_NvApi
        "nvapi_pepQueryInterface"   // ModuleFlavour_NvPowerApi
    };

#endif // !AFTERMATH_TEST_BUILD

    // -------------------------------------------------------------------------
    // Cached state.  Recovered globals, one slot per flavour:
    //     0x18001B9B0  module handle
    //     0x18001B9E8  "loaded" flag
    //     0x18001B9D0  "loading in progress" byte
    //
    //   All three are indexed directly by the flavour argument, which every
    //   recovered call site passes as 0.
    // -------------------------------------------------------------------------
    static void*    s_queryFunction[ModuleFlavour_Count] = { nullptr, nullptr };
    static void*    s_modules[ModuleFlavour_Count]       = { nullptr, nullptr };
    static uint8_t  s_loadedFlags[ModuleFlavour_Count]   = { 0, 0 };

    // [D] 0x18001B9D0 -- the "loading" byte that EnsureLoaded() polls.
    //
    //   Nothing in this image ever sets it: it is a handshake with a loader
    //   thread that is not part of this binary.  The wait loop is nevertheless
    //   reproduced, including the ten second-ish timeout, because it is
    //   observable behaviour.
    static volatile uint8_t s_loadBusy[ModuleFlavour_Count] = { 0, 0 };

    // -------------------------------------------------------------------------
    // Resolved interfaces, keyed on the NVAPI id.
    //
    //   A small open-addressed table rather than an array indexed by the id:
    //   the ids are 32-bit literals spread over the whole range, so they cannot
    //   be used as subscripts.
    // -------------------------------------------------------------------------
    enum { kMaxCachedInterfaces = 16 };

    struct CachedInterface
    {
        uint32_t id;
        void*    fn;
    };

    static CachedInterface s_cache[kMaxCachedInterfaces];
    static size_t          s_cacheCount = 0;

    // [D] 0x18001B9A0 / 0x18001B9A8 -- the trace pair.  Cached separately from
    // the interface table because sub_180001000 resolves it at load time.
    static TraceBeginFn s_traceBegin = nullptr;
    static TraceEndFn   s_traceEnd   = nullptr;

#ifdef AFTERMATH_TEST_BUILD
    // The host test has no nvapi64.dll and drives the loader through an injected
    // query function, so LoadModule() has nothing to load.
    static bool s_testInjected = false;
#endif

    // =========================================================================
    // Forward declarations
    // =========================================================================
#ifndef AFTERMATH_TEST_BUILD
    static int32_t ProbeQueryInterface(void* module, int flavour);
#endif
    static int32_t LoadModule(int flavour);

    // =========================================================================
    // LoadModule      <- sub_1800010E0
    //
    //   [D] the recovered body:
    //         if ( module[flavour] != 0 ) return 0;
    //         name = flavour == 0 ? L"nvapi64.dll" : L"nvpowerapi.dll";
    //         module = LoadLibrary(name, 0);
    //         if ( module == 0 ) return -2;
    //         status = ProbeQueryInterface(module, flavour);
    //         if ( status != 0 ) { FreeLibrary(module); return status; }
    //         module[flavour] = module;
    //         loaded[flavour] = 1;
    //         return 0;
    // =========================================================================
    static int32_t LoadModule(int flavour)
    {
        if (s_modules[flavour] != nullptr)
        {
            return 0;
        }

#ifndef AFTERMATH_TEST_BUILD
        HMODULE module = ::GetModuleHandleA(kModuleNames[flavour]);
        if (module == nullptr)
        {
            module = ::LoadLibraryA(kModuleNames[flavour]);
        }

        if (module == nullptr)
        {
            return EnsureLoaded_LoadFailed;
        }

        s_loadBusy[flavour] = 1;
        const int32_t status = ProbeQueryInterface(module, flavour);
        s_loadBusy[flavour] = 0;

        if (status != 0)
        {
            ::FreeLibrary(module);
            return status;
        }

        s_modules[flavour]     = module;
        s_loadedFlags[flavour] = 1;
#else
        if (!s_testInjected)
        {
            return EnsureLoaded_LoadFailed;
        }

        s_modules[flavour]     = reinterpret_cast<void*>(1);
        s_loadedFlags[flavour] = 1;
#endif

        return 0;
    }

    // =========================================================================
    // EnsureLoaded      <- sub_180001180
    //
    //   [D] the recovered body, reproduced shape for shape:
    //
    //       for ( tries = 0; busy[flavour] != 0; )
    //       {
    //           Sleep(100);
    //           if ( ++tries >= 10 ) return -1;
    //       }
    //
    //       if ( module[flavour] != 0 ) return 0;
    //       return LoadModule(flavour);
    // =========================================================================
    int32_t EnsureLoaded(int flavour)
    {
        if (flavour < 0 || flavour >= ModuleFlavour_Count)
        {
            return EnsureLoaded_LoadFailed;
        }

        uint32_t tries = 0;
        while (s_loadBusy[flavour] != 0)
        {
#ifndef AFTERMATH_TEST_BUILD
            ::Sleep(100);
#endif
            ++tries;

            if (tries >= 10)
            {
                return EnsureLoaded_Timeout;
            }
        }

        if (s_modules[flavour] != nullptr)
        {
            return 0;
        }

        return LoadModule(flavour);
    }

#ifndef AFTERMATH_TEST_BUILD
    // =========================================================================
    // ProbeQueryInterface      <- sub_180001000
    //
    //   [D] flavour 0:
    //         queryFunction = GetProcAddress(module, "nvapi_QueryInterface");
    //         if ( queryFunction == 0 )                      return -1;
    //         probe = queryFunction(0x150E828);
    //         if ( probe == 0 ) { queryFunction = 0;         return -1; }
    //         status = probe();
    //         if ( status != 0 ) { queryFunction = 0;        return status; }
    //         traceBegin = queryFunction(0x33C7358C);
    //         traceEnd   = queryFunction(0x593E8644);
    //         if ( traceBegin == 0 || traceEnd != 0 ) { clear both; }
    //
    //   [D] flavour 1 checks GetProcAddress("nvapi_pepQueryInterface") only.
    //
    //   NOTE ON THE TRACE-PAIR TEST: the condition above is transcribed exactly
    //   as the listing renders it, which requires evaluating `traceEnd != 0`
    //   only when `traceBegin != 0` -- true for a short-circuiting `||`.  Taken
    //   literally it means the pair survives only when traceBegin resolves AND
    //   traceEnd does not, i.e. tracing is always disabled in practice.  This
    //   looks like a decompiler rendering of an inverted test rather than the
    //   original source; it is flagged in docs/ANALYSIS_NOTES.md as the one
    //   place where the recovered text is self-contradictory.  It is harmless
    //   either way, because every call site skips a null trace function.
    // =========================================================================
    static int32_t ProbeQueryInterface(void* module, int flavour)
    {
        HMODULE hModule = static_cast<HMODULE>(module);

        if (flavour == ModuleFlavour_NvPowerApi)
        {
            s_queryFunction[flavour] = reinterpret_cast<void*>(
                ::GetProcAddress(hModule, kQueryExportNames[flavour]));

            return (s_queryFunction[flavour] != nullptr) ? 0 : -1;
        }

        if (flavour != ModuleFlavour_NvApi)
        {
            return -1;
        }

        s_queryFunction[flavour] = reinterpret_cast<void*>(
            ::GetProcAddress(hModule, kQueryExportNames[flavour]));

        if (s_queryFunction[flavour] == nullptr)
        {
            return -1;
        }

        QueryInterfaceFn query =
            reinterpret_cast<QueryInterfaceFn>(s_queryFunction[flavour]);

        // The "is this NVAPI usable" probe.  Its id is not one of the Aftermath
        // interfaces and its contract is unknown from this side; all that the
        // recovered code does is check for a non-null result and call it.  [I]
        typedef int32_t(AFTERMATH_CDECL * NvApiProbeFn)(void);
        NvApiProbeFn probe = reinterpret_cast<NvApiProbeFn>(query(0x150E828u));

        if (probe == nullptr)
        {
            s_queryFunction[flavour] = nullptr;
            return -1;
        }

        const int32_t probeStatus = probe();

        if (probeStatus != 0)
        {
            s_queryFunction[flavour] = nullptr;
            return probeStatus;
        }

        s_traceBegin = reinterpret_cast<TraceBeginFn>(query(NVAPI_ID_TRACE_BEGIN));
        s_traceEnd   = reinterpret_cast<TraceEndFn>(query(NVAPI_ID_TRACE_END));

        // See the note above: transcribed literally.
        if (s_traceBegin == nullptr || s_traceEnd != nullptr)
        {
            s_traceBegin = nullptr;
            s_traceEnd   = nullptr;
        }

        return 0;
    }

#endif // !AFTERMATH_TEST_BUILD

    // =========================================================================
    // ResolveInterface
    //
    //   The shared tail of the thirteen thunks: return the cached pointer for
    //   'id', querying NVAPI on first use.  Callers map null onto
    //   DriverStatus_Unavailable (-3), exactly as the recovered thunks do.
    // =========================================================================
    void* ResolveInterface(uint32_t id)
    {
        for (size_t i = 0; i < s_cacheCount; ++i)
        {
            if (s_cache[i].id == id)
            {
                return s_cache[i].fn;
            }
        }

        if (EnsureLoaded(ModuleFlavour_NvApi) != 0 || s_queryFunction[ModuleFlavour_NvApi] == nullptr)
        {
            return nullptr;
        }

        QueryInterfaceFn query =
            reinterpret_cast<QueryInterfaceFn>(s_queryFunction[ModuleFlavour_NvApi]);

        void* fn = query(id);

        // Negative results are cached too: a driver will not start implementing
        // an id during this process, and re-querying on every call would be
        // wasteful.  [I]
        if (s_cacheCount < kMaxCachedInterfaces)
        {
            s_cache[s_cacheCount].id = id;
            s_cache[s_cacheCount].fn = fn;
            ++s_cacheCount;
        }

        return fn;
    }

    // =========================================================================
    // Tracing pair.
    //
    //   [D] every thunk reads these around its driver call and skips them
    //   unconditionally when null.
    // =========================================================================
    TraceBeginFn GetTraceBegin()
    {
        return s_traceBegin;
    }

    TraceEndFn GetTraceEnd()
    {
        return s_traceEnd;
    }

    // =========================================================================
    // CallVersionInfo      <- sub_180001200
    //
    //   [D] the full recovered body:
    //         ++refcount;
    //         status = EnsureLoaded(0);
    //         if ( status == 0 )
    //         {
    //             fn = cached ?: queryFunction(0x2926AAAD);
    //             if ( fn == 0 ) status = -3;
    //             else { traceBegin(id,&token); status = fn(a2, a3); traceEnd(id,token,status); }
    //         }
    //         --refcount;
    //         return status;
    //
    //   GFSDK_Aftermath_DX12_Initialize passes a real 0x40 byte info block so
    //   it can inspect the debug layer; the DX11 path and the version-only query
    //   pass an all-zero one.  [D]
    // =========================================================================
    int32_t ReadInterfaceInfo(uint32_t* pVersionOut, void* pInfoOut)
    {
        typedef int32_t(AFTERMATH_CDECL * VersionInfoFn)(void* pVersion, void* pInfo);

        AFTERMATH_INTERLOCKED_INC(&g_driverRefCount);

        int32_t status = EnsureLoaded(ModuleFlavour_NvApi);

        if (status == 0)
        {
            VersionInfoFn fn =
                reinterpret_cast<VersionInfoFn>(ResolveInterface(NVAPI_ID_VERSION_INFO));

            if (fn == nullptr)
            {
                status = DriverStatus_Unavailable;
            }
            else
            {
                void* token = nullptr;

                TraceBeginFn traceBegin = GetTraceBegin();
                if (traceBegin != nullptr)
                {
                    traceBegin(NVAPI_ID_VERSION_INFO, &token);
                }

                status = fn(pVersionOut, pInfoOut);

                TraceEndFn traceEnd = GetTraceEnd();
                if (traceEnd != nullptr)
                {
                    traceEnd(NVAPI_ID_VERSION_INFO, token, status);
                }
            }
        }

        AFTERMATH_INTERLOCKED_DEC(&g_driverRefCount);
        return status;
    }

    int32_t ReadInterfaceVersion(uint32_t* pVersionOut)
    {
        uint8_t zeroInfo[0x40];
        ::memset(zeroInfo, 0, sizeof(zeroInfo));
        return ReadInterfaceInfo(pVersionOut, zeroInfo);
    }

    // =========================================================================
    // Release
    // =========================================================================
    void Release()
    {
        for (size_t i = 0; i < kMaxCachedInterfaces; ++i)
        {
            s_cache[i].id = 0;
            s_cache[i].fn = nullptr;
        }
        s_cacheCount = 0;

        s_traceBegin = nullptr;
        s_traceEnd   = nullptr;

        // Deliberately keeping the module handles and the query function: the
        // modules stay loaded for the lifetime of the process.  [I]
    }

    // =========================================================================
    // Test hook
    // =========================================================================
    void SetQueryInterfaceForTesting(QueryInterfaceFn fn)
    {
        Release();

        for (int i = 0; i < ModuleFlavour_Count; ++i)
        {
            s_queryFunction[i] = nullptr;
            s_modules[i]       = nullptr;
            s_loadedFlags[i]   = 0;
        }

#ifndef AFTERMATH_TEST_BUILD
        (void)fn;
        // On Windows the loader always goes through LoadLibrary; the hook is not
        // usable there.
#else
        s_queryFunction[ModuleFlavour_NvApi] = reinterpret_cast<void*>(fn);
        s_testInjected = (fn != nullptr);

        if (fn != nullptr)
        {
            s_modules[ModuleFlavour_NvApi]     = reinterpret_cast<void*>(1);
            s_loadedFlags[ModuleFlavour_NvApi] = 1;

            s_traceBegin = reinterpret_cast<TraceBeginFn>(fn(NVAPI_ID_TRACE_BEGIN));
            s_traceEnd   = reinterpret_cast<TraceEndFn>(fn(NVAPI_ID_TRACE_END));
        }
#endif
    }
}
}
