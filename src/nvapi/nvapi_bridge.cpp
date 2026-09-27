/*
 * nvapi_bridge.cpp -- reconstruction of sub_180001000 .. sub_180001C00.
 *
 * This is NVIDIA's standard "NVAPI stub" pattern, emitted once per entry point:
 *
 *      lock xadd [g_activeCallCount], 1          <- Hex-Rays renders this _InterlockedAdd
 *      status = EnsureModuleLoaded(0)
 *      if (status == NVAPI_OK) {
 *          if (!cachedFn && nvapi_QueryInterface)
 *              cachedFn = nvapi_QueryInterface(<interface id>)
 *          if (cachedFn) {
 *              if (onEnter) onEnter(<interface id>, &token)
 *              status = cachedFn(...)
 *              if (onLeave) onLeave(<interface id>, token, status)
 *          } else {
 *              status = NVAPI_NO_IMPLEMENTATION
 *          }
 *      }
 *      lock xadd [g_activeCallCount], -1
 *      return status
 *
 * The 13 thunks differ only in the interface id, the cache slot and the argument list, so
 * they are expressed here with a single variadic helper instead of 13 copies.
 */

#include "nvapi/nvapi_bridge.h"

#include <windows.h>

#include "loader/nv_module_loader.h"

namespace nvapi {
namespace {

/* ------------------------------------------------------------------------------------ *
 * Globals.  The original addresses are given so the reconstruction can be diffed against
 * the binary.
 * ------------------------------------------------------------------------------------ */

/* 0x18001B998 -- nvapi64.dll!nvapi_QueryInterface */
PfnNvApiQueryInterface g_queryInterface = NULL;

/* nvpowerapi.dll!nvapi_pepQueryInterface (own slot, adjacent to the one above). */
PfnNvApiQueryInterface g_pepQueryInterface = NULL;

/* 0x18001B9A0 / 0x18001B9A8 -- optional call tracing hooks published by NVAPI itself. */
PfnNvApiEnterEntryPoint g_onEnterEntryPoint = NULL;
PfnNvApiLeaveEntryPoint g_onLeaveEntryPoint = NULL;

/* 0x18001B9B0 -- module handle per ModuleId. */
HMODULE g_moduleHandles[kModuleCount] = { NULL };

/* 0x18001B9D0 -- non-zero while a module is being unloaded; callers spin on it.  Nothing
 * inside this DLL ever sets it (the unload path lives in the shared NVAPI stub code that
 * was not linked in), so in practice the spin below never triggers. */
volatile char g_moduleUnloading[8] = { 0 };

/* 0x18001B9D8 -- number of NVAPI calls currently in flight. */
volatile LONG g_activeCallCount = 0;

/* 0x18001B9E8 -- "module was loaded by us" flag per ModuleId. */
LONG g_moduleLoaded[kModuleCount] = { 0 };

/*
 * 0x18001BA00 .. 0x18001EEC8 -- a single 0x34C8 byte block of resolved entry points that
 * sub_180001000 clears with one memset.  Each thunk owns one slot; the indices below are
 * (original address - 0x18001BA00) / 8.
 */
enum { kEntryPointCacheSlots = 0x34C8 / sizeof(void*) };   /* 1689 */

enum EntryPointSlot
{
    kSlot_SysGetDriverAndBranchVersion     = 0x0198 / 8,   /* 0x18001BB98 */
    kSlot_D3D11_SetEventMarker             = 0x29A0 / 8,   /* 0x18001E3A0 */
    kSlot_D3D11_GetData                    = 0x29A8 / 8,   /* 0x18001E3A8 */
    kSlot_GetDeviceState                   = 0x29B0 / 8,   /* 0x18001E3B0 */
    kSlot_D3D12_SetEventMarker             = 0x29B8 / 8,   /* 0x18001E3B8 */
    kSlot_D3D12_GetData                    = 0x29C0 / 8,   /* 0x18001E3C0 */
    kSlot_GetDeviceState2                  = 0x29C8 / 8,   /* 0x18001E3C8 */
    kSlot_D3D11_CreateHandle               = 0x29F0 / 8,   /* 0x18001E3F0 */
    kSlot_D3D12_CreateHandle               = 0x29F8 / 8,   /* 0x18001E3F8 */
    kSlot_D3D11_EnableFeatures             = 0x2A28 / 8,   /* 0x18001E428 */
    kSlot_D3D12_EnableFeatures             = 0x2A30 / 8,   /* 0x18001E430 */
    kSlot_GetPageFaultInformation          = 0x2A38 / 8,   /* 0x18001E438 */
    kSlot_GetPageFaultInformation2         = 0x2A40 / 8    /* 0x18001E440 */
};

/* Stored as generic function pointers rather than void* so that every access is a
 * well formed function-pointer cast (the binary simply keeps 8 byte slots here). */
typedef void (*GenericEntryPoint)();
GenericEntryPoint g_entryPointCache[kEntryPointCacheSlots] = { NULL };

/* ------------------------------------------------------------------------------------ *
 * sub_180001000 -- bind the exported entry point of a freshly loaded driver library.
 * ------------------------------------------------------------------------------------ */
NvAPI_Status BindModuleEntryPoints(HMODULE module, ModuleId moduleId)
{
    if (moduleId == kModuleNvApi64)
    {
        g_queryInterface = reinterpret_cast<PfnNvApiQueryInterface>(
            ::GetProcAddress(module, "nvapi_QueryInterface"));
        if (g_queryInterface == NULL)
        {
            return NVAPI_ERROR;
        }

        const PfnNvApiInitialize initialize =
            reinterpret_cast<PfnNvApiInitialize>(g_queryInterface(kNvApiId_Initialize));
        if (initialize == NULL)
        {
            g_queryInterface = NULL;
            return NVAPI_ERROR;
        }

        const NvAPI_Status status = initialize();
        if (status != NVAPI_OK)
        {
            g_queryInterface = NULL;
            return status;
        }

        g_onEnterEntryPoint = reinterpret_cast<PfnNvApiEnterEntryPoint>(g_queryInterface(kNvApiId_EnterEntryPoint));
        g_onLeaveEntryPoint = reinterpret_cast<PfnNvApiLeaveEntryPoint>(g_queryInterface(kNvApiId_LeaveEntryPoint));

        /* Verbatim from the binary:
         *      if (enter == NULL || leave != NULL) { enter = NULL; leave = NULL; }
         * The second half of the test is almost certainly a typo in the original source
         * (one would expect "leave == NULL"); its practical effect is that the tracing
         * hooks are disabled unless the driver publishes an "enter" hook and no "leave"
         * hook.  Reproduced as-is -- see docs/RECONSTRUCTION.md. */
        if (g_onEnterEntryPoint == NULL || g_onLeaveEntryPoint != NULL)
        {
            g_onEnterEntryPoint = NULL;
            g_onLeaveEntryPoint = NULL;
        }
    }
    else if (moduleId == kModuleNvPowerApi)
    {
        g_pepQueryInterface = reinterpret_cast<PfnNvApiQueryInterface>(
            ::GetProcAddress(module, "nvapi_pepQueryInterface"));
        if (g_pepQueryInterface == NULL)
        {
            return NVAPI_ERROR;
        }
    }

    /* Any entry point resolved against a previous incarnation of the driver library is
     * now stale. */
    ::ZeroMemory(g_entryPointCache, sizeof(g_entryPointCache));
    return NVAPI_OK;
}

/* ------------------------------------------------------------------------------------ *
 * sub_1800010E0 -- load a driver library through the hardened loader.
 * ------------------------------------------------------------------------------------ */
NvAPI_Status LoadModule(ModuleId moduleId)
{
    if (g_moduleHandles[moduleId] != NULL)
    {
        return NVAPI_OK;
    }

    const wchar_t* const moduleName = (moduleId == kModuleNvApi64) ? L"nvapi64.dll" : L"nvpowerapi.dll";

    const HMODULE module = loader::NvLoadLibrary(moduleName, 0);
    if (module == NULL)
    {
        return NVAPI_LIBRARY_NOT_FOUND;
    }

    const NvAPI_Status status = BindModuleEntryPoints(module, moduleId);
    if (status != NVAPI_OK)
    {
        ::FreeLibrary(module);
        return status;
    }

    g_moduleHandles[moduleId] = module;
    g_moduleLoaded[moduleId] = 1;
    return NVAPI_OK;
}

/* ------------------------------------------------------------------------------------ *
 * The shared body of the 13 thunks.
 * ------------------------------------------------------------------------------------ */
template <typename PfnEntryPoint>
PfnEntryPoint ResolveEntryPoint(EntryPointSlot slot, NvApiInterfaceId interfaceId)
{
    PfnEntryPoint entryPoint = reinterpret_cast<PfnEntryPoint>(g_entryPointCache[slot]);
    if (entryPoint == NULL && g_queryInterface != NULL)
    {
        entryPoint = reinterpret_cast<PfnEntryPoint>(g_queryInterface(static_cast<unsigned int>(interfaceId)));
        g_entryPointCache[slot] = reinterpret_cast<GenericEntryPoint>(entryPoint);
    }
    return entryPoint;
}

class ActiveCallScope
{
public:
    ActiveCallScope()  { ::InterlockedIncrement(&g_activeCallCount); }
    ~ActiveCallScope() { ::InterlockedDecrement(&g_activeCallCount); }
private:
    ActiveCallScope(const ActiveCallScope&);
    ActiveCallScope& operator=(const ActiveCallScope&);
};

/* RAII wrapper around the optional enter/leave tracing hooks. */
class TraceScope
{
public:
    explicit TraceScope(NvApiInterfaceId interfaceId)
        : m_interfaceId(static_cast<unsigned int>(interfaceId))
        , m_token(0)
        , m_status(NVAPI_OK)
    {
        if (g_onEnterEntryPoint != NULL)
        {
            g_onEnterEntryPoint(m_interfaceId, &m_token);
        }
    }

    NvAPI_Status Complete(NvAPI_Status status)
    {
        m_status = status;
        return status;
    }

    ~TraceScope()
    {
        if (g_onLeaveEntryPoint != NULL)
        {
            g_onLeaveEntryPoint(m_interfaceId, m_token, m_status);
        }
    }

private:
    TraceScope(const TraceScope&);
    TraceScope& operator=(const TraceScope&);

    unsigned int m_interfaceId;
    ULONG_PTR    m_token;
    NvAPI_Status m_status;
};

#define NVAPI_THUNK_PROLOGUE(PfnType, slot, interfaceId, entryPointVariable)          \
    const ActiveCallScope activeCallScope;                                            \
    const NvAPI_Status loadStatus = EnsureModuleLoaded(kModuleNvApi64);               \
    if (loadStatus != NVAPI_OK)                                                       \
    {                                                                                 \
        return loadStatus;                                                            \
    }                                                                                 \
    const PfnType entryPointVariable = ResolveEntryPoint<PfnType>((slot), (interfaceId)); \
    if (entryPointVariable == NULL)                                                   \
    {                                                                                 \
        return NVAPI_NO_IMPLEMENTATION;                                               \
    }                                                                                 \
    TraceScope traceScope(interfaceId)

} /* anonymous namespace */

/* ------------------------------------------------------------------------------------ *
 * sub_180001180
 * ------------------------------------------------------------------------------------ */
NvAPI_Status EnsureModuleLoaded(ModuleId moduleId)
{
    for (int attempt = 0; g_moduleUnloading[moduleId] != 0; ++attempt)
    {
        if (attempt >= 10)
        {
            return NVAPI_ERROR;
        }
        ::Sleep(100);
    }

    if (g_moduleHandles[moduleId] != NULL)
    {
        return NVAPI_OK;
    }

    return LoadModule(moduleId);
}

/* ------------------------------------------------------------------------------------ *
 * The thunks themselves.
 * ------------------------------------------------------------------------------------ */

NvAPI_Status SYS_GetDriverAndBranchVersion(NvU32* pDriverVersion, char* szBuildBranchString)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiSysGetDriverAndBranchVersion,
                         kSlot_SysGetDriverAndBranchVersion,
                         kNvApiId_SYS_GetDriverAndBranchVersion, entryPoint);
    return traceScope.Complete(entryPoint(pDriverVersion, szBuildBranchString));
}

NvAPI_Status D3D11_AftermathCreateHandle(void* pD3D11DeviceOrContext, NvAftermathContextHandle* pOutHandle)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathCreateHandle,
                         kSlot_D3D11_CreateHandle,
                         kNvApiId_D3D11_AftermathCreateHandle, entryPoint);
    return traceScope.Complete(entryPoint(pD3D11DeviceOrContext, pOutHandle));
}

NvAPI_Status D3D11_AftermathEnableFeatures(NvAftermathContextHandle handle, NvU32 featureFlags)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathEnableFeatures,
                         kSlot_D3D11_EnableFeatures,
                         kNvApiId_D3D11_AftermathEnableFeatures, entryPoint);
    return traceScope.Complete(entryPoint(handle, featureFlags));
}

NvAPI_Status D3D11_AftermathSetEventMarker(NvAftermathContextHandle handle, const void* pMarkerData, NvU32 markerSize)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathSetEventMarker,
                         kSlot_D3D11_SetEventMarker,
                         kNvApiId_D3D11_AftermathSetEventMarker, entryPoint);
    return traceScope.Complete(entryPoint(handle, pMarkerData, markerSize));
}

NvAPI_Status D3D11_AftermathGetData(NvAftermathContextHandle handle, void** ppOutMarkerData, NvU32* pOutMarkerSize, NvU32* pOutStatus)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathGetData,
                         kSlot_D3D11_GetData,
                         kNvApiId_D3D11_AftermathGetData, entryPoint);
    return traceScope.Complete(entryPoint(handle, ppOutMarkerData, pOutMarkerSize, pOutStatus));
}

NvAPI_Status D3D12_AftermathCreateHandle(void* pD3D12DeviceOrCommandList, NvAftermathContextHandle* pOutHandle)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathCreateHandle,
                         kSlot_D3D12_CreateHandle,
                         kNvApiId_D3D12_AftermathCreateHandle, entryPoint);
    return traceScope.Complete(entryPoint(pD3D12DeviceOrCommandList, pOutHandle));
}

NvAPI_Status D3D12_AftermathEnableFeatures(NvAftermathContextHandle handle, NvU32 featureFlags)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathEnableFeatures,
                         kSlot_D3D12_EnableFeatures,
                         kNvApiId_D3D12_AftermathEnableFeatures, entryPoint);
    return traceScope.Complete(entryPoint(handle, featureFlags));
}

NvAPI_Status D3D12_AftermathSetEventMarker(NvAftermathContextHandle handle, const void* pMarkerData, NvU32 markerSize)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathSetEventMarker,
                         kSlot_D3D12_SetEventMarker,
                         kNvApiId_D3D12_AftermathSetEventMarker, entryPoint);
    return traceScope.Complete(entryPoint(handle, pMarkerData, markerSize));
}

NvAPI_Status D3D12_AftermathGetData(NvAftermathContextHandle handle, void** ppOutMarkerData, NvU32* pOutMarkerSize, NvU32* pOutStatus)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathGetData,
                         kSlot_D3D12_GetData,
                         kNvApiId_D3D12_AftermathGetData, entryPoint);
    return traceScope.Complete(entryPoint(handle, ppOutMarkerData, pOutMarkerSize, pOutStatus));
}

NvAPI_Status AftermathGetDeviceState(NvAftermathContextHandle handle, NvU32* pOutState)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathGetDeviceState,
                         kSlot_GetDeviceState,
                         kNvApiId_AftermathGetDeviceState, entryPoint);
    return traceScope.Complete(entryPoint(handle, pOutState));
}

NvAPI_Status AftermathGetDeviceState2(NvAftermathContextHandle handle, NvU32* pOutState)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathGetDeviceState,
                         kSlot_GetDeviceState2,
                         kNvApiId_AftermathGetDeviceState2, entryPoint);
    return traceScope.Complete(entryPoint(handle, pOutState));
}

NvAPI_Status AftermathGetPageFaultInformation(NvAftermathContextHandle handle, void* pOutPageFaultInformation)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathGetPageFaultInformation,
                         kSlot_GetPageFaultInformation,
                         kNvApiId_AftermathGetPageFaultInformation, entryPoint);
    return traceScope.Complete(entryPoint(handle, pOutPageFaultInformation));
}

NvAPI_Status AftermathGetPageFaultInformation2(NvAftermathContextHandle handle, void* pOutPageFaultInformation)
{
    NVAPI_THUNK_PROLOGUE(PfnNvApiAftermathGetPageFaultInformation,
                         kSlot_GetPageFaultInformation2,
                         kNvApiId_AftermathGetPageFaultInformation2, entryPoint);
    return traceScope.Complete(entryPoint(handle, pOutPageFaultInformation));
}

} /* namespace nvapi */
