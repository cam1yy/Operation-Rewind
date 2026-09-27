// =============================================================================
//  host_smoke_test.cpp
// -----------------------------------------------------------------------------
//  Behavioural test for the reconstruction, runnable on any host.
//
//  There is no nvapi64.dll and no D3D device here, so the test installs its own
//  NVAPI query function that hands out recording stubs, plus a pair of fake COM
//  objects standing in for the D3D12 device and the command list.  That
//  exercises everything the library actually does:
//
//    * the Initialize() guard, including the latch that the listing revealed;
//    * the version gate, the interface version gate and the debug layer probe;
//    * the attach / enable-features sequence and the handle layout;
//    * the feature flag gates and the per-context error encoding of GetData;
//    * the primary/fallback device status and page fault paths;
//    * the tracing bracket and the driver call refcount.
//
//  The only two things replaced are the module loader and the driver.
//
//  Build and run:
//      make -f Makefile.host test
//
//  Every assertion below corresponds to a behaviour recorded during the
//  recovery pass, so a failure means the reconstruction diverged from the
//  original (or the original was misread -- either way it is worth knowing).
// =============================================================================

#include "../include/GFSDK_Aftermath.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/nvapi/nvapi_ids.h"
#include "../src/nvapi/nvapi_loader.h"
#include "../src/aftermath_internal.h"

// -----------------------------------------------------------------------------
// Test harness
// -----------------------------------------------------------------------------
static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const char* what)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        ::printf("  FAIL  %s\n", what);
    }
    else
    {
        ::printf("  ok    %s\n", what);
    }
}

static void CheckResult(GFSDK_Aftermath_Result actual,
                        GFSDK_Aftermath_Result expected,
                        const char* what)
{
    ++g_checks;
    if (actual != expected)
    {
        ++g_failures;
        ::printf("  FAIL  %s (got 0x%08X, want 0x%08X)\n",
                 what, (unsigned)actual, (unsigned)expected);
    }
    else
    {
        ::printf("  ok    %s\n", what);
    }
}

// -----------------------------------------------------------------------------
// Fake COM objects
//
//   The library calls through two D3D vtables and it matters which:
//
//     * Initialize(D3D12) calls slot 0 of the DEVICE -- QueryInterface -- to
//       look for the debug layer.  A returned S_OK means "debug layer present"
//       and is a FAILURE condition.
//     * GetData calls slot 112 of the D3D11 context (0x380) or slot 8 of the
//       D3D12 command list (0x40) -- GetType -- and compares the result to 1 to
//       detect deferred contexts and bundles.
//
//   One vtable big enough to cover both roles, plus the debug interface that
//   QueryInterface hands back so the release call can be observed.
// -----------------------------------------------------------------------------
enum { kFakeVtableSlots = 128 };

static uint32_t g_fakeGetTypeReturn = 0;
static uint32_t FakeGetType(void* pSelf)
{
    (void)pSelf;
    return g_fakeGetTypeReturn;
}

// QueryInterface.  Defaults to E_NOINTERFACE so that the common case is "no
// debug layer"; the debug-layer test flips the flag.
static bool     g_fakeHasDebugLayer = false;
// Some implementations hand back an interface even on failure.  The library's
// release call sits on exactly that path, so it needs its own switch.
static bool     g_fakeInterfaceOnFailure = false;
static int      g_fakeQueryInterfaceCalls = 0;
static uint32_t g_fakeQueryInterfaceLastHr = 0;

// The object the debug-layer QueryInterface hands back.  The library must
// release it through vtable slot 2 (offset +0x10).
static int g_fakeDebugReleaseCalls = 0;
static uint32_t FakeDebugRelease(void* pSelf)
{
    (void)pSelf;
    ++g_fakeDebugReleaseCalls;
    return 0;
}

static void* g_fakeDebugInterfaceVtable[3];
static void* g_fakeDebugInterfaceObject[1];

static int32_t FakeQueryInterface(void* pSelf, const void* pIid, void** ppOut)
{
    (void)pSelf;
    (void)pIid;
    ++g_fakeQueryInterfaceCalls;

    // S_OK means "the debug layer is here", which the library must treat as a
    // failure -- but it still releases whatever came back first.
    if (g_fakeHasDebugLayer)
    {
        g_fakeQueryInterfaceLastHr = 0;
        *ppOut = g_fakeDebugInterfaceObject;
        return 0;
    }

    g_fakeQueryInterfaceLastHr = 0x80004002;            // E_NOINTERFACE
    *ppOut = g_fakeInterfaceOnFailure ? g_fakeDebugInterfaceObject : nullptr;
    return (int32_t)0x80004002;
}

static void* g_fakeVtable[kFakeVtableSlots];

struct FakeComObject
{
    void**   vtable;        // must be the first word
    uint32_t tag;
};

static void InitFakeCom()
{
    for (int i = 0; i < kFakeVtableSlots; ++i)
    {
        g_fakeVtable[i] = (void*)&FakeGetType;
    }

    g_fakeVtable[0] = (void*)&FakeQueryInterface;       // device slot 0

    g_fakeDebugInterfaceVtable[0] = nullptr;
    g_fakeDebugInterfaceVtable[1] = nullptr;
    g_fakeDebugInterfaceVtable[2] = (void*)&FakeDebugRelease;
    g_fakeDebugInterfaceObject[0] = (void*)g_fakeDebugInterfaceVtable;
}

static FakeComObject g_fakeDevice  = { g_fakeVtable, 0xDE71CE };
static FakeComObject g_fakeContext = { g_fakeVtable, 0xC0E7E };

// -----------------------------------------------------------------------------
// Recording driver stubs
//
//   Signatures mirror the typedefs in src/nvapi/aftermath_driver.cpp.  They are
//   defined with C linkage so the function pointer casts behave exactly as they
//   do under MSVC.
// -----------------------------------------------------------------------------
static const void* kDriverHandleSentinel = (const void*)0xD81F4E;

struct StubState
{
    int      versionInfoCalls;
    int      attachCalls;
    int      enableFeaturesCalls;
    int      setEventMarkerCalls;
    int      getDataCalls;
    int      deviceStatusPrimaryCalls;
    int      deviceStatusFallbackCalls;
    int      pageFaultPrimaryCalls;
    int      pageFaultFallbackCalls;

    void*    lastAttachObject;
    void*    lastEnableHandle;
    void*    lastMarkerHandle;
    const void* lastMarkerData;
    uint32_t lastMarkerSize;
    uint32_t lastEnableFlags;
    uint32_t lastVersionInfoVersion;

    // What the "driver" should report next.
    int32_t  versionInfoResult;
    uint32_t versionInfoInterfaceVersion;
    int32_t  attachResult;
    int32_t  enableFeaturesResult;
    int32_t  setEventMarkerResult;
    int32_t  getDataResult;
    int      getDataFailFirstNCalls;    // sequences results across calls
    uint32_t getDataStatus;
    int32_t  deviceStatusPrimaryResult;
    int32_t  deviceStatusFallbackResult;
    uint32_t deviceStatusWord;
    int32_t  pageFaultPrimaryResult;
    int32_t  pageFaultFallbackResult;

    const void* markerPayload;
};

static StubState g_stub;

static void ResetStub()
{
    ::memset(&g_stub, 0, sizeof(g_stub));
    g_stub.versionInfoResult           = 0;
    g_stub.versionInfoInterfaceVersion = aftermath::nvapi::NVAPI_MIN_INTERFACE_VERSION;
    g_stub.attachResult                = 0;
    g_stub.enableFeaturesResult        = 0;
    g_stub.setEventMarkerResult        = 0;
    g_stub.getDataResult               = 0;
    g_stub.deviceStatusPrimaryResult   = 0;
    g_stub.deviceStatusFallbackResult  = 0;
    g_stub.deviceStatusWord            = 1;      // -> Active
    g_stub.pageFaultPrimaryResult      = 0;
    g_stub.pageFaultFallbackResult     = 0;
    g_stub.markerPayload               = "marker";
}

// -----------------------------------------------------------------------------
// Tracing pair.  Installed once; every check below can then assert that the
// driver call was bracketed correctly.
// -----------------------------------------------------------------------------
static int      g_traceBeginCalls = 0;
static int      g_traceEndCalls   = 0;
static uint32_t g_traceBeginId    = 0;
static uint32_t g_traceEndId      = 0;
static void*    g_traceToken      = nullptr;
static int32_t  g_traceEndStatus  = 0;
static int32_t  g_traceEndRefCount = -1;

static void TraceBegin(uint32_t id, void** ppToken)
{
    ++g_traceBeginCalls;
    g_traceBeginId = id;
    *ppToken = &g_traceToken;                   // any non-null cookie
    g_traceToken = *ppToken;
}

static void TraceEnd(uint32_t id, void* token, int32_t status)
{
    ++g_traceEndCalls;
    g_traceEndId      = id;
    g_traceEndStatus  = status;
    g_traceEndRefCount = aftermath::g_driverRefCount;
    (void)token;
}

static void ResetTrace()
{
    g_traceBeginCalls  = 0;
    g_traceEndCalls    = 0;
    g_traceBeginId     = 0;
    g_traceEndId       = 0;
    g_traceEndStatus   = 0;
    g_traceEndRefCount = -1;
}

extern "C" {

static int32_t StubVersionInfo(void* pVersion, void* pInfo)
{
    ++g_stub.versionInfoCalls;
    if (g_stub.versionInfoResult == 0)
    {
        if (pVersion != nullptr)
        {
            *static_cast<uint32_t*>(pVersion) = g_stub.versionInfoInterfaceVersion;
        }
        if (pInfo != nullptr)
        {
            // The library passes a zeroed 0x40 byte block; leave it zeroed.
            ::memset(pInfo, 0, 0x40);
        }
    }
    return g_stub.versionInfoResult;
}

// The attach entry point is used both for the device (Initialize) and for the
// command list / context (CreateContextHandle).
static int32_t StubAttach(void* pD3DObject, void** ppDriverHandleOut)
{
    ++g_stub.attachCalls;
    g_stub.lastAttachObject = pD3DObject;

    if (g_stub.attachResult == 0)
    {
        *ppDriverHandleOut = const_cast<void*>(kDriverHandleSentinel);
    }
    return g_stub.attachResult;
}

static int32_t StubEnableFeatures(void* driverHandle, uint32_t featureFlags)
{
    ++g_stub.enableFeaturesCalls;
    g_stub.lastEnableHandle = driverHandle;
    g_stub.lastEnableFlags  = featureFlags;
    return g_stub.enableFeaturesResult;
}

static int32_t StubSetEventMarker(void* driverHandle, const void* pMarkerData,
                                  uint32_t markerDataSize)
{
    ++g_stub.setEventMarkerCalls;
    g_stub.lastMarkerHandle = driverHandle;
    g_stub.lastMarkerData   = pMarkerData;
    g_stub.lastMarkerSize   = markerDataSize;
    return g_stub.setEventMarkerResult;
}

static int32_t StubGetData(void* driverHandle, void** ppMarkerData,
                           uint32_t* pMarkerSize, uint32_t* pContextStatus)
{
    (void)driverHandle;
    ++g_stub.getDataCalls;

    if (g_stub.getDataCalls <= g_stub.getDataFailFirstNCalls)
    {
        return -1;                          // Fail
    }

    if (g_stub.getDataResult == 0)
    {
        *ppMarkerData   = const_cast<void*>(g_stub.markerPayload);
        *pMarkerSize    = 6;
        *pContextStatus = g_stub.getDataStatus;
    }
    return g_stub.getDataResult;
}

static int32_t StubDeviceStatusPrimary(void* driverHandle, uint32_t* pStatusOut)
{
    (void)driverHandle;
    ++g_stub.deviceStatusPrimaryCalls;

    if (g_stub.deviceStatusPrimaryResult == 0)
    {
        *pStatusOut = g_stub.deviceStatusWord;
    }
    return g_stub.deviceStatusPrimaryResult;
}

static int32_t StubDeviceStatusFallback(void* driverHandle, uint32_t* pStatusOut)
{
    (void)driverHandle;
    ++g_stub.deviceStatusFallbackCalls;

    if (g_stub.deviceStatusFallbackResult == 0)
    {
        *pStatusOut = g_stub.deviceStatusWord;
    }
    return g_stub.deviceStatusFallbackResult;
}

static int32_t StubPageFaultPrimary(void* driverHandle, void* pInfoOut)
{
    (void)driverHandle;
    (void)pInfoOut;
    ++g_stub.pageFaultPrimaryCalls;
    return g_stub.pageFaultPrimaryResult;
}

static int32_t StubPageFaultFallback(void* driverHandle, void* pInfoOut)
{
    (void)driverHandle;
    (void)pInfoOut;
    ++g_stub.pageFaultFallbackCalls;
    return g_stub.pageFaultFallbackResult;
}

} // extern "C"

// -----------------------------------------------------------------------------
// The injected query function.
// -----------------------------------------------------------------------------
static void* TestQueryInterface(uint32_t id)
{
    switch (id)
    {
    case aftermath::nvapi::NVAPI_ID_VERSION_INFO:            return (void*)&StubVersionInfo;
    case aftermath::nvapi::NVAPI_ID_D3D11_ATTACH:            return (void*)&StubAttach;
    case aftermath::nvapi::NVAPI_ID_D3D12_ATTACH:            return (void*)&StubAttach;
    case aftermath::nvapi::NVAPI_ID_D3D11_ENABLE_FEATURES:   return (void*)&StubEnableFeatures;
    case aftermath::nvapi::NVAPI_ID_D3D12_ENABLE_FEATURES:   return (void*)&StubEnableFeatures;
    case aftermath::nvapi::NVAPI_ID_D3D11_SET_EVENT_MARKER:  return (void*)&StubSetEventMarker;
    case aftermath::nvapi::NVAPI_ID_D3D12_SET_EVENT_MARKER:  return (void*)&StubSetEventMarker;
    case aftermath::nvapi::NVAPI_ID_D3D11_GET_DATA:          return (void*)&StubGetData;
    case aftermath::nvapi::NVAPI_ID_D3D12_GET_DATA:          return (void*)&StubGetData;
    case aftermath::nvapi::NVAPI_ID_D3D11_DEVICE_STATUS:     return (void*)&StubDeviceStatusPrimary;
    case aftermath::nvapi::NVAPI_ID_D3D12_DEVICE_STATUS:     return (void*)&StubDeviceStatusFallback;
    case aftermath::nvapi::NVAPI_ID_PAGE_FAULT_PRIMARY:      return (void*)&StubPageFaultPrimary;
    case aftermath::nvapi::NVAPI_ID_PAGE_FAULT_FALLBACK:     return (void*)&StubPageFaultFallback;
    case aftermath::nvapi::NVAPI_ID_TRACE_BEGIN:             return (void*)&TraceBegin;
    case aftermath::nvapi::NVAPI_ID_TRACE_END:               return (void*)&TraceEnd;
    default:                                                 return nullptr;
    }
}

// -----------------------------------------------------------------------------
// Convenience: bring the library up in the state the next test needs.
// -----------------------------------------------------------------------------
static void ResetLibrary(uint32_t flags)
{
    aftermath::TestResetInitializeGuard();
    aftermath::Shutdown();
    ResetStub();
    ResetTrace();
    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    const GFSDK_Aftermath_Result result = GFSDK_Aftermath_DX12_Initialize(
        GFSDK_Aftermath_Version_API, flags, &g_fakeDevice);

    if (result != GFSDK_Aftermath_Result_Success)
    {
        ++g_failures;
        ++g_checks;
        ::printf("  FAIL  fixture could not initialize (0x%08X)\n", (unsigned)result);
    }
}

// =============================================================================
// Tests
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Version gate.  [D] anything other than 0x13 returns FAIL_VersionMismatch
//    (0xBAD00001) -- not FAIL_ApiError -- before the driver is contacted.
//
//    The second half of this test covers the Initialize() latch, which is the
//    single most surprising thing in the listing: dword_18002029C is set to 1
//    and never cleared, so once an Initialize() has failed, every later call
//    falls through to the unconditional `return 1` and reports Success without
//    doing anything at all.
// -----------------------------------------------------------------------------
static void TestVersionGate()
{
    ::printf("\n[1] API version gate and the initialize latch\n");

    aftermath::TestResetInitializeGuard();
    aftermath::Shutdown();
    ResetStub();
    ResetTrace();
    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    CheckResult(GFSDK_Aftermath_DX12_Initialize(0x12, 0, &g_fakeDevice),
                GFSDK_Aftermath_Result_FAIL_VersionMismatch,
                "version 0x12 rejected with FAIL_VersionMismatch");

    Check(g_stub.versionInfoCalls == 0 && g_stub.attachCalls == 0,
          "driver was not contacted for a rejected version");

    CheckResult(GFSDK_Aftermath_DX11_Initialize(0x14, 0, &g_fakeDevice),
                GFSDK_Aftermath_Result_Success,
                "the latch makes the SECOND initialize report success");

    Check(g_stub.versionInfoCalls == 0 && g_stub.attachCalls == 0,
          "and it reports success without contacting the driver");

    // With the latch cleared, the same bad version is rejected again.
    aftermath::TestResetInitializeGuard();

    CheckResult(GFSDK_Aftermath_DX11_Initialize(0x14, 0, &g_fakeDevice),
                GFSDK_Aftermath_Result_FAIL_VersionMismatch,
                "version 0x14 rejected once the latch is cleared");

    aftermath::TestResetInitializeGuard();
}

// -----------------------------------------------------------------------------
// 2. Initialize.  [D] the order is version+info query, interface version gate,
//    (debug layer probe on DX12,) attach, enable features.
// -----------------------------------------------------------------------------
static void TestInitialize()
{
    ::printf("\n[2] Initialize\n");

    aftermath::TestResetInitializeGuard();
    aftermath::Shutdown();
    ResetStub();
    ResetTrace();
    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    const uint32_t flags =
        GFSDK_Aftermath_FeatureFlags_EnableMarkers |
        GFSDK_Aftermath_FeatureFlags_EnableResourceTracking;

    g_fakeHasDebugLayer = false;

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &g_fakeDevice),
                GFSDK_Aftermath_Result_Success,
                "DX12 initialize succeeds");

    Check(g_stub.versionInfoCalls == 1, "interface version queried once");
    Check(g_stub.attachCalls == 1, "device attach called once");
    Check(g_stub.lastAttachObject == &g_fakeDevice, "device pointer forwarded");
    Check(g_stub.enableFeaturesCalls == 1, "enable features called once");
    Check(g_stub.lastEnableHandle == kDriverHandleSentinel,
          "features enabled on the DRIVER HANDLE, not the device");
    Check(g_stub.lastEnableFlags == flags, "feature flags forwarded");

    // Null device.  [D] checked before the driver is consulted, and it is a
    // plain pointer test -- there is no API selector test at all.
    aftermath::TestResetInitializeGuard();
    ResetStub();

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API, flags, nullptr),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "null device rejected with FAIL_InvalidParameter");
    Check(g_stub.versionInfoCalls == 0, "null device rejected before the query");

    // Interface version below 0x9784.  [D] FAIL_DriverVersionNotSupported.
    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_stub.versionInfoInterfaceVersion = aftermath::nvapi::NVAPI_MIN_INTERFACE_VERSION - 1;

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &g_fakeDevice),
                GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported,
                "driver interface version below 0x9784 rejected");
    Check(g_stub.attachCalls == 0, "attach not reached for an old driver");

    // Exactly 0x9784 is accepted.
    aftermath::TestResetInitializeGuard();
    ResetStub();

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &g_fakeDevice),
                GFSDK_Aftermath_Result_Success,
                "interface version exactly 0x9784 accepted");

    // A failing version query is mapped through the driver status table; -5 is
    // FAIL_InvalidParameter.  [D]
    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_stub.versionInfoResult = -5;

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &g_fakeDevice),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "failing version query mapped: -5 -> FAIL_InvalidParameter");

    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_stub.versionInfoResult = -3;

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &g_fakeDevice),
                GFSDK_Aftermath_Result_FAIL_NvApiIncompatible,
                "failing version query mapped: -3 -> FAIL_NvApiIncompatible");

    // Attach failure.  -1 -> Fail.
    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_stub.attachResult = -1;

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &g_fakeDevice),
                GFSDK_Aftermath_Result_Fail,
                "attach failure mapped: -1 -> Fail");

    // Enable-features failure.  [D] a FIXED code, not a mapped driver status:
    // 0xBAD0000B regardless of what the driver returned.
    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_stub.enableFeaturesResult = -0x82;

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &g_fakeDevice),
                GFSDK_Aftermath_Result_FAIL_DriverInitFailed,
                "enable features failure -> FAIL_DriverInitFailed");

    // The tracing bracket.  The version query and the driver calls are all
    // bracketed with the id of the interface being called.
    ResetTrace();
    aftermath::TestResetInitializeGuard();
    ResetStub();

    GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API, flags, &g_fakeDevice);

    Check(g_traceBeginCalls == g_traceEndCalls && g_traceBeginCalls == 3,
          "three driver calls, three trace brackets");
    Check(g_traceEndRefCount == 1, "refcount was 1 at trace-end time");
    Check(aftermath::g_driverRefCount == 0, "refcount back to 0 afterwards");
}

// -----------------------------------------------------------------------------
// 3. The D3D12 debug layer probe.  [D] Initialize(DX12) calls QueryInterface on
//    the device and treats S_OK as FAIL_D3DDebugLayerNotCompatible; a non-null
//    result is released through vtable +0x10.
// -----------------------------------------------------------------------------
static void TestDebugLayerProbe()
{
    ::printf("\n[3] D3D12 debug layer probe\n");

    aftermath::TestResetInitializeGuard();
    aftermath::Shutdown();
    ResetStub();
    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    g_fakeHasDebugLayer = false;
    g_fakeQueryInterfaceCalls = 0;
    g_fakeDebugReleaseCalls   = 0;

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &g_fakeDevice),
                GFSDK_Aftermath_Result_Success,
                "no debug layer -> initialize proceeds");
    Check(g_fakeQueryInterfaceCalls == 1, "device QueryInterface was called once");
    Check(g_fakeDebugReleaseCalls == 0,
          "nothing to release when the probe returns a null interface");

    // Now with a debug layer present.
    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_fakeHasDebugLayer     = true;
    g_fakeDebugReleaseCalls = 0;

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &g_fakeDevice),
                GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible,
                "debug layer present -> FAIL_D3DDebugLayerNotCompatible");
    Check(g_stub.attachCalls == 0, "the driver is never asked to attach");
    Check(g_fakeDebugReleaseCalls == 0,
          "the returned interface is NOT released on the failure path");

    // QueryInterface fails but hands back an interface anyway: the library must
    // release it through vtable slot 2 and carry on.  [D]
    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_fakeHasDebugLayer        = false;
    g_fakeInterfaceOnFailure   = true;
    g_fakeDebugReleaseCalls    = 0;

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &g_fakeDevice),
                GFSDK_Aftermath_Result_Success,
                "a failed probe with a returned interface still initializes");
    Check(g_fakeDebugReleaseCalls == 1,
          "the returned interface was released through slot 2 (+0x10)");

    g_fakeInterfaceOnFailure = false;

    // The DX11 path does not probe at all.
    aftermath::TestResetInitializeGuard();
    ResetStub();
    g_fakeQueryInterfaceCalls = 0;
    g_fakeHasDebugLayer = true;             // would fail if it probed

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &g_fakeDevice),
                GFSDK_Aftermath_Result_Success,
                "DX11 does not run the debug layer probe");
    Check(g_fakeQueryInterfaceCalls == 0, "no QueryInterface on the DX11 path");

    g_fakeHasDebugLayer = false;
    aftermath::TestResetInitializeGuard();
}

// -----------------------------------------------------------------------------
// 4. Context handle.  [D] 0x18 bytes, zeroed, API word at +4, context pointer
//    at +8, driver handle at +16 written by the attach call.  The handle is
//    published to the caller BEFORE the driver can reject it.
// -----------------------------------------------------------------------------
static void TestContextHandle()
{
    ::printf("\n[4] Context handle\n");

    ResetLibrary(GFSDK_Aftermath_FeatureFlags_EnableMarkers);

    // The fixture's Initialize already attached the device; only the context's
    // own attach should be counted from here on.
    g_stub.attachCalls = 0;

    GFSDK_Aftermath_ContextHandle handle = nullptr;

    CheckResult(GFSDK_Aftermath_DX12_CreateContextHandle(&g_fakeContext, &handle),
                GFSDK_Aftermath_Result_Success,
                "DX12 CreateContextHandle succeeds");
    Check(handle != nullptr, "handle was returned");
    Check(g_stub.attachCalls == 1, "attach called for the context");
    Check(g_stub.lastAttachObject == &g_fakeContext, "context pointer forwarded");

    if (handle != nullptr)
    {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(handle);

        uint64_t reserved = 1;
        uint32_t api = 0;
        void*    ctx = nullptr;
        void*    driverHandle = nullptr;

        ::memcpy(&api, raw + 4, sizeof(api));
        ::memcpy(&ctx, raw + 8, sizeof(ctx));
        ::memcpy(&driverHandle, raw + 16, sizeof(driverHandle));
        ::memcpy(&reserved, raw + 0, sizeof(reserved) - 4);   // 4 bytes at +0

        Check(api == 1, "API word at +4 is 1 for D3D12");
        Check(ctx == &g_fakeContext, "context pointer at +8");
        Check(driverHandle == kDriverHandleSentinel,
              "driver handle at +16 came from the attach call");
        Check(reserved == 0, "reserved word at +0 is zero");
    }

    CheckResult(GFSDK_Aftermath_ReleaseContextHandle(handle),
                GFSDK_Aftermath_Result_Success,
                "ReleaseContextHandle succeeds");

    // Null handle after initialisation.  [D]
    CheckResult(GFSDK_Aftermath_ReleaseContextHandle(nullptr),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "null handle rejected with FAIL_InvalidParameter");

    // Null context pointer.  [D]
    handle = nullptr;
    CheckResult(GFSDK_Aftermath_DX12_CreateContextHandle(nullptr, &handle),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "null context rejected with FAIL_InvalidParameter");

    // A failing attach still publishes the handle and leaks it, exactly as the
    // original does.  [D]
    g_stub.attachResult = -1;
    handle = nullptr;

    CheckResult(GFSDK_Aftermath_DX12_CreateContextHandle(&g_fakeContext, &handle),
                GFSDK_Aftermath_Result_Fail,
                "attach failure mapped onto the return value");
    Check(handle != nullptr, "handle escaped before the driver rejected it");

    if (handle != nullptr)
    {
        g_stub.attachResult = 0;
        GFSDK_Aftermath_ReleaseContextHandle(handle);
    }

    // The handle remembers its API, so a DX11 handle dispatching through the
    // handle path must reach the DX11 entry point.  [D]
    handle = nullptr;
    CheckResult(GFSDK_Aftermath_DX11_CreateContextHandle(&g_fakeContext, &handle),
                GFSDK_Aftermath_Result_Success, "DX11 CreateContextHandle");
    if (handle != nullptr)
    {
        uint32_t api = 0;
        ::memcpy(&api, reinterpret_cast<const uint8_t*>(handle) + 4, sizeof(api));
        Check(api == 0, "API word at +4 is 0 for D3D11");
        GFSDK_Aftermath_ReleaseContextHandle(handle);
    }
}

// -----------------------------------------------------------------------------
// 5. Feature flag gates.  [D] bit 0 (EnableMarkers) on SetEventMarker and
//    GetData, bit 1 (EnableResourceTracking) on GetPageFaultInformation, and NO
//    gate at all on GetDeviceStatus.
// -----------------------------------------------------------------------------
static void TestFeatureGates()
{
    ::printf("\n[5] Feature flag gates\n");

    ResetLibrary(GFSDK_Aftermath_FeatureFlags_Minimum);

    GFSDK_Aftermath_ContextHandle handle = nullptr;
    CheckResult(GFSDK_Aftermath_DX12_CreateContextHandle(&g_fakeContext, &handle),
                GFSDK_Aftermath_Result_Success, "context handle");

    CheckResult(GFSDK_Aftermath_SetEventMarker(handle, "x", 2),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "SetEventMarker without EnableMarkers");
    Check(g_stub.setEventMarkerCalls == 0, "driver not reached");

    GFSDK_Aftermath_ContextData entry;
    ::memset(&entry, 0, sizeof(entry));
    const GFSDK_Aftermath_ContextHandle handles[1] = { handle };

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "GetData without EnableMarkers");
    Check(g_stub.getDataCalls == 0, "driver not reached");

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(nullptr),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "GetPageFaultInformation without EnableResourceTracking");

    // [D] The gate order is feature flags -> null pointer, so a null handle with
    // markers disabled reports the feature, not the parameter.
    CheckResult(GFSDK_Aftermath_SetEventMarker(nullptr, "x", 2),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "the feature gate is tested before the null-handle test");
    CheckResult(GFSDK_Aftermath_GetData(1, handles, nullptr),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "same for GetData's null output pointer");

    GFSDK_Aftermath_Device_Status status = GFSDK_Aftermath_Device_Status_Unknown;
    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_Success,
                "GetDeviceStatus is NOT feature gated");
    Check(g_stub.deviceStatusPrimaryCalls == 1, "driver was reached");

    GFSDK_Aftermath_ReleaseContextHandle(handle);

    // Markers on, resource tracking still off.
    ResetLibrary(GFSDK_Aftermath_FeatureFlags_EnableMarkers);

    handle = nullptr;
    GFSDK_Aftermath_DX12_CreateContextHandle(&g_fakeContext, &handle);

    CheckResult(GFSDK_Aftermath_SetEventMarker(handle, "marker", 6),
                GFSDK_Aftermath_Result_Success,
                "SetEventMarker with EnableMarkers");
    Check(g_stub.setEventMarkerCalls == 1, "driver reached");
    Check(g_stub.lastMarkerHandle == kDriverHandleSentinel,
          "marker delivered to the DRIVER HANDLE from the context");
    Check(g_stub.lastMarkerSize == 6, "marker size forwarded");

    // [D] There is no null-marker test in the original...
    CheckResult(GFSDK_Aftermath_SetEventMarker(handle, nullptr, 0),
                GFSDK_Aftermath_Result_Success,
                "null marker data is NOT rejected");

    // ...and no upper bound on the size either.
    CheckResult(GFSDK_Aftermath_SetEventMarker(handle, "x", 0xFFFFFFFFu),
                GFSDK_Aftermath_Result_Success,
                "no marker size limit is enforced");

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(nullptr),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "page faults still gated without resource tracking");

    GFSDK_Aftermath_ReleaseContextHandle(handle);

    // Resource tracking on.
    ResetLibrary(GFSDK_Aftermath_FeatureFlags_EnableResourceTracking);

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(nullptr),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "page faults reach the parameter test");

    uint8_t info[64];
    ::memset(info, 0, sizeof(info));
    GFSDK_Aftermath_GetPageFaultInformation(
        reinterpret_cast<GFSDK_Aftermath_PageFaultInformation*>(info));
    Check(g_stub.pageFaultPrimaryCalls == 1,
          "page fault query goes to the primary entry point first");
}

// -----------------------------------------------------------------------------
// 6. GetData's per-context error encoding.  [D] a failing context is marked
//    Invalid with the result code widened into the marker pointer slot, and the
//    call itself still reports success.
// -----------------------------------------------------------------------------
static void TestGetData()
{
    ::printf("\n[6] GetData per-context encoding\n");

    ResetLibrary(GFSDK_Aftermath_FeatureFlags_EnableMarkers);

    GFSDK_Aftermath_ContextHandle handle = nullptr;
    GFSDK_Aftermath_DX12_CreateContextHandle(&g_fakeContext, &handle);

    // (a) success path
    GFSDK_Aftermath_ContextData entry;
    ::memset(&entry, 0, sizeof(entry));
    const GFSDK_Aftermath_ContextHandle handles[1] = { handle };
    g_stub.getDataStatus = GFSDK_Aftermath_Context_Status_Finished;

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_Success, "GetData succeeds");
    Check(entry.status == GFSDK_Aftermath_Context_Status_Finished,
          "context status copied through");
    Check(entry.markerSize == 6, "marker size copied through");
    Check(entry.markerData == g_stub.markerPayload,
          "marker payload pointer copied through");

    // (b) a driver status failure is recorded in the entry AND propagated to
    //     the return value: -0x84 -> FAIL_GettingContextDataWithNewCommandList.
    //     [D]
    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataResult = -0x84;

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList,
                "a failing driver call propagates to the return value");
    Check(entry.status == GFSDK_Aftermath_Context_Status_Invalid,
          "failed entry marked Invalid");
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList,
          "entry carries the mapped FAIL_GettingContextDataWithNewCommandList");

    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataResult = -5;

    GFSDK_Aftermath_GetData(1, handles, &entry);
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_InvalidParameter,
          "driver status -5 translated to FAIL_InvalidParameter");

    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataResult = -1;

    GFSDK_Aftermath_GetData(1, handles, &entry);
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_Fail,
          "driver status -1 translated to Fail");

    // (c) null handle -> FAIL_InvalidParameter encoded in the entry.
    ::memset(&entry, 0, sizeof(entry));
    const GFSDK_Aftermath_ContextHandle nullHandles[1] = { nullptr };
    g_stub.getDataResult = 0;

    CheckResult(GFSDK_Aftermath_GetData(1, nullHandles, &entry),
                GFSDK_Aftermath_Result_Success, "null handle does not fail the call");
    Check(entry.status == GFSDK_Aftermath_Context_Status_Invalid,
          "null handle entry marked Invalid");
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_InvalidParameter,
          "entry carries FAIL_InvalidParameter");

    // (c2) the accumulator follows the LAST context that reached the driver,
    //      so a failing entry followed by a good one still reports Success.
    //      [D] the guard is applied once, after the loop.
    GFSDK_Aftermath_ContextHandle secondHandle = nullptr;
    GFSDK_Aftermath_DX12_CreateContextHandle(&g_fakeContext, &secondHandle);

    const GFSDK_Aftermath_ContextHandle pair[2] = { handle, secondHandle };
    GFSDK_Aftermath_ContextData pairData[2];
    ::memset(pairData, 0, sizeof(pairData));

    g_stub.getDataCalls           = 0;
    g_stub.getDataResult          = 0;
    g_stub.getDataFailFirstNCalls = 1;          // the first entry fails
    CheckResult(GFSDK_Aftermath_GetData(2, pair, pairData),
                GFSDK_Aftermath_Result_Success,
                "a later successful entry clears the earlier failure");
    Check(reinterpret_cast<intptr_t>(pairData[0].markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_Fail,
          "the failed entry still carries its code");

    ::memset(pairData, 0, sizeof(pairData));
    g_stub.getDataCalls           = 0;
    g_stub.getDataFailFirstNCalls = 0;          // both succeed
    CheckResult(GFSDK_Aftermath_GetData(2, pair, pairData),
                GFSDK_Aftermath_Result_Success,
                "both entries successful");

    ::memset(pairData, 0, sizeof(pairData));
    g_stub.getDataCalls           = 0;
    g_stub.getDataResult          = -1;         // the LAST one fails
    CheckResult(GFSDK_Aftermath_GetData(2, pair, pairData),
                GFSDK_Aftermath_Result_Fail,
                "a failing last entry is what the call reports");
    g_stub.getDataResult          = 0;
    g_stub.getDataFailFirstNCalls = 0;

    GFSDK_Aftermath_ReleaseContextHandle(secondHandle);

    // (d) a D3D12 BUNDLE command list: vtable slot 8 returns 1.  The driver is
    //     never called and the entry carries FAIL_GetDataOnBundle.  [D]
    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataResult = 0;
    g_stub.getDataCalls  = 0;
    g_fakeGetTypeReturn  = 1;

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_Success, "bundle entry handled");
    Check(g_stub.getDataCalls == 0, "the driver is not called for a bundle");
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_GetDataOnBundle,
          "entry carries FAIL_GetDataOnBundle");

    // (e) a D3D11 DEFERRED device context: vtable slot 112 returns 1.  [D]
    GFSDK_Aftermath_ContextHandle dx11Handle = nullptr;
    GFSDK_Aftermath_DX11_CreateContextHandle(&g_fakeContext, &dx11Handle);

    const GFSDK_Aftermath_ContextHandle dx11Handles[1] = { dx11Handle };
    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataCalls = 0;

    GFSDK_Aftermath_GetData(1, dx11Handles, &entry);
    Check(g_stub.getDataCalls == 0, "the driver is not called for a deferred context");
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext,
          "entry carries FAIL_GetDataOnDeferredContext");

    // (f) with the type back to 0 (immediate context) the driver IS called.
    g_fakeGetTypeReturn = 0;
    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataCalls = 0;

    GFSDK_Aftermath_GetData(1, dx11Handles, &entry);
    Check(g_stub.getDataCalls == 1, "the driver is called for an immediate context");

    // (g) the degenerate call: no contexts at all is a parameter error.  [D]
    CheckResult(GFSDK_Aftermath_GetData(0, handles, &entry),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "zero contexts rejected");

    GFSDK_Aftermath_ReleaseContextHandle(handle);
    GFSDK_Aftermath_ReleaseContextHandle(dx11Handle);
}

// -----------------------------------------------------------------------------
// 7. Device status.  [D] the D3D11 entry point is tried first and the D3D12 one
//    is the fallback; the returned code comes from the second call, which is
//    left at 0 when the first succeeded.
// -----------------------------------------------------------------------------
static void TestDeviceStatus()
{
    ::printf("\n[7] Device status\n");

    ResetLibrary(GFSDK_Aftermath_FeatureFlags_EnableMarkers);

    struct Case { uint32_t driver; GFSDK_Aftermath_Device_Status expected; };
    const Case cases[] =
    {
        { 1, GFSDK_Aftermath_Device_Status_Active },
        { 2, GFSDK_Aftermath_Device_Status_Timeout },
        { 3, GFSDK_Aftermath_Device_Status_Timeout },
        { 4, GFSDK_Aftermath_Device_Status_Timeout },
        { 5, GFSDK_Aftermath_Device_Status_OutOfMemory },
        { 6, GFSDK_Aftermath_Device_Status_PageFault },
        { 7, GFSDK_Aftermath_Device_Status_PageFault },
        { 8, GFSDK_Aftermath_Device_Status_Unknown },
        { 99, GFSDK_Aftermath_Device_Status_Unknown },
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
    {
        GFSDK_Aftermath_Device_Status status = GFSDK_Aftermath_Device_Status_Unknown;
        g_stub.deviceStatusWord = cases[i].driver;

        char label[128];
        ::snprintf(label, sizeof(label),
                   "driver status %u maps to %u",
                   (unsigned)cases[i].driver, (unsigned)cases[i].expected);

        CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                    GFSDK_Aftermath_Result_Success, label);

        ++g_checks;
        if (status != cases[i].expected)
        {
            ++g_failures;
            ::printf("  FAIL  %s (got %u)\n", label, (unsigned)status);
        }
    }

    Check(g_stub.deviceStatusPrimaryCalls == 9 &&
              g_stub.deviceStatusFallbackCalls == 0,
          "the D3D11 entry point served every query on its own");

    // Primary fails, fallback answers.  The status word is translated but the
    // CALL still reports success, because the returned code comes from the
    // second status variable.  [D]
    GFSDK_Aftermath_Device_Status status = GFSDK_Aftermath_Device_Status_Unknown;
    g_stub.deviceStatusPrimaryResult  = -3;
    g_stub.deviceStatusFallbackResult = 0;
    g_stub.deviceStatusWord           = 6;

    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_Success,
                "fallback path still reports Success");
    Check(status == GFSDK_Aftermath_Device_Status_PageFault,
          "fallback status word was translated");
    Check(g_stub.deviceStatusFallbackCalls == 1, "fallback was used");

    // Both fail: the returned code is the mapped fallback status.
    g_stub.deviceStatusPrimaryResult  = -3;
    g_stub.deviceStatusFallbackResult = -1;
    status = GFSDK_Aftermath_Device_Status_Unknown;

    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_Fail,
                "both entry points failing maps the fallback status");
    Check(status == GFSDK_Aftermath_Device_Status_Unknown,
          "status word keeps the pre-set Unknown value");

    // Null output pointer.  [D]
    CheckResult(GFSDK_Aftermath_GetDeviceStatus(nullptr),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "null status pointer rejected");
}

// -----------------------------------------------------------------------------
// 8. Page fault information.  [D] same primary/fallback shape, but both calls
//    share one status variable, so a failing primary followed by a successful
//    fallback reports Success.
// -----------------------------------------------------------------------------
static void TestPageFaultInformation()
{
    ::printf("\n[8] Page fault information\n");

    ResetLibrary(GFSDK_Aftermath_FeatureFlags_EnableResourceTracking);

    uint8_t info[64];
    ::memset(info, 0, sizeof(info));

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(
                    reinterpret_cast<GFSDK_Aftermath_PageFaultInformation*>(info)),
                GFSDK_Aftermath_Result_Success, "primary succeeds");
    Check(g_stub.pageFaultPrimaryCalls == 1 && g_stub.pageFaultFallbackCalls == 0,
          "fallback not used when the primary answers");

    g_stub.pageFaultPrimaryResult  = -1;
    g_stub.pageFaultFallbackResult = 0;

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(
                    reinterpret_cast<GFSDK_Aftermath_PageFaultInformation*>(info)),
                GFSDK_Aftermath_Result_Success,
                "fallback success overwrites the primary failure");
    Check(g_stub.pageFaultFallbackCalls == 1, "fallback was used");

    g_stub.pageFaultPrimaryResult  = -1;
    g_stub.pageFaultFallbackResult = -0x82;

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(
                    reinterpret_cast<GFSDK_Aftermath_PageFaultInformation*>(info)),
                GFSDK_Aftermath_Result_FAIL_OutOfMemory,
                "both failing maps the fallback status (-0x82 -> OutOfMemory)");

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(nullptr),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "null pointer rejected");
}

// -----------------------------------------------------------------------------
// 9. Result codes, version and the recovered constants.
// -----------------------------------------------------------------------------
static void TestResultConstants()
{
    ::printf("\n[9] Result code constants\n");

    Check(GFSDK_Aftermath_Result_Fail == 0xBAD00000u, "Fail base is 0xBAD00000");
    Check(GFSDK_Aftermath_Result_Success == 1u, "Success is 1");
    Check(GFSDK_Aftermath_Result_NotAvailable == 2u, "NotAvailable is 2");
    Check(GFSDK_Aftermath_Result_FAIL_VersionMismatch == 0xBAD00001u,
          "FAIL_VersionMismatch is 0xBAD00001");
    Check(GFSDK_Aftermath_Result_FAIL_NotInitialized == 0xBAD00002u,
          "FAIL_NotInitialized is 0xBAD00002");
    Check(GFSDK_Aftermath_Result_FAIL_InvalidParameter == 0xBAD00004u,
          "FAIL_InvalidParameter is 0xBAD00004");
    Check(GFSDK_Aftermath_Result_FAIL_Unknown == 0xBAD00005u,
          "FAIL_Unknown is 0xBAD00005");
    Check(GFSDK_Aftermath_Result_FAIL_ApiError == 0xBAD00006u,
          "FAIL_ApiError is 0xBAD00006");
    Check(GFSDK_Aftermath_Result_FAIL_NvApiIncompatible == 0xBAD00007u,
          "FAIL_NvApiIncompatible is 0xBAD00007");
    Check(GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList
              == 0xBAD00008u,
          "FAIL_GettingContextDataWithNewCommandList is 0xBAD00008");
    Check(GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible == 0xBAD0000Au,
          "FAIL_D3DDebugLayerNotCompatible is 0xBAD0000A");
    Check(GFSDK_Aftermath_Result_FAIL_DriverInitFailed == 0xBAD0000Bu,
          "FAIL_DriverInitFailed is 0xBAD0000B");
    Check(GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported == 0xBAD0000Cu,
          "FAIL_DriverVersionNotSupported is 0xBAD0000C");
    Check(GFSDK_Aftermath_Result_FAIL_OutOfMemory == 0xBAD0000Du,
          "FAIL_OutOfMemory is 0xBAD0000D");
    Check(GFSDK_Aftermath_Result_FAIL_GetDataOnBundle == 0xBAD0000Eu,
          "FAIL_GetDataOnBundle is 0xBAD0000E");
    Check(GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext == 0xBAD0000Fu,
          "FAIL_GetDataOnDeferredContext is 0xBAD0000F");
    Check(GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled == 0xBAD00010u,
          "FAIL_FeatureNotEnabled is 0xBAD00010");

    Check(GFSDK_Aftermath_Version_API == 0x13, "Version_API is 0x13");
    Check(aftermath::nvapi::NVAPI_MIN_INTERFACE_VERSION == 0x9784,
          "minimum driver interface version is 0x9784");
    Check(aftermath::nvapi::NVAPI_ID_VERSION_INFO == 0x2926AAADu,
          "version-info id is 0x2926AAAD");
    Check(aftermath::nvapi::NVAPI_ID_D3D11_ATTACH == 0xC99F4A67u,
          "D3D11 attach id is 0xC99F4A67");
    Check(aftermath::nvapi::NVAPI_ID_D3D12_ATTACH == 0xF1EA1980u,
          "D3D12 attach id is 0xF1EA1980");
    Check(aftermath::nvapi::NVAPI_ID_D3D11_ENABLE_FEATURES == 0xCBA3F913u,
          "D3D11 enable-features id is 0xCBA3F913");
    Check(aftermath::nvapi::NVAPI_ID_D3D12_ENABLE_FEATURES == 0xDBE53CB2u,
          "D3D12 enable-features id is 0xDBE53CB2");
    Check(aftermath::nvapi::NVAPI_ID_D3D11_DEVICE_STATUS == 0x1DE221DDu,
          "D3D11 device status id is 0x1DE221DD");
    Check(aftermath::nvapi::NVAPI_ID_D3D12_DEVICE_STATUS == 0x633D88E1u,
          "D3D12 device status id is 0x633D88E1");
    Check(aftermath::nvapi::NVAPI_ID_PAGE_FAULT_PRIMARY == 0x0BBA25D7u,
          "primary page fault id is 0x0BBA25D7");

    // The three per-entry sentinels seen as negative immediates in the
    // disassembly must decode onto these constants.
    Check((int32_t)GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext ==
              (int32_t)-0x452FFFF1,
          "-0x452FFFF1 decodes to FAIL_GetDataOnDeferredContext");
    Check((int32_t)GFSDK_Aftermath_Result_FAIL_GetDataOnBundle ==
              (int32_t)-0x452FFFF2,
          "-0x452FFFF2 decodes to FAIL_GetDataOnBundle");
    Check((int32_t)GFSDK_Aftermath_Result_FAIL_InvalidParameter ==
              (int32_t)-0x452FFFFC,
          "-0x452FFFFC decodes to FAIL_InvalidParameter");
    Check((int32_t)GFSDK_Aftermath_Result_FAIL_VersionMismatch ==
              (int32_t)-0x452FFFFF,
          "-0x452FFFFF decodes to FAIL_VersionMismatch");
}

// -----------------------------------------------------------------------------
// 10. Not-initialized guards and shutdown.
// -----------------------------------------------------------------------------
static void TestLifecycle()
{
    ::printf("\n[10] Not-initialized guards\n");

    ResetLibrary(GFSDK_Aftermath_FeatureFlags_EnableMarkers);

    aftermath::Shutdown();

    GFSDK_Aftermath_ContextHandle handle = nullptr;
    GFSDK_Aftermath_Device_Status status = GFSDK_Aftermath_Device_Status_Unknown;
    GFSDK_Aftermath_ContextData entry;
    ::memset(&entry, 0, sizeof(entry));
    const GFSDK_Aftermath_ContextHandle handles[1] = { nullptr };

    CheckResult(GFSDK_Aftermath_DX11_CreateContextHandle(&g_fakeContext, &handle),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "CreateContextHandle before Initialize");

    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "GetDeviceStatus before Initialize");

    CheckResult(GFSDK_Aftermath_ReleaseContextHandle(
                    reinterpret_cast<GFSDK_Aftermath_ContextHandle>(&entry)),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "ReleaseContextHandle before Initialize");

    CheckResult(GFSDK_Aftermath_SetEventMarker(
                    reinterpret_cast<GFSDK_Aftermath_ContextHandle>(&entry), "x", 2),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "SetEventMarker before Initialize");

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "GetData before Initialize");

    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(nullptr),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "GetPageFaultInformation before Initialize");

    // Re-initialise after a shutdown.  The guard has to be cleared by hand,
    // because the original never clears it -- see TestVersionGate.  [D]
    aftermath::TestResetInitializeGuard();
    ResetStub();

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &g_fakeDevice),
                GFSDK_Aftermath_Result_Success, "re-initialize after shutdown");

    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_Success, "usable again");

    aftermath::TestResetInitializeGuard();
    aftermath::Shutdown();
}

// =============================================================================
int main()
{
    ::printf("GFSDK_Aftermath_Lib reconstruction -- host smoke test\n");
    ::printf("====================================================\n");

    InitFakeCom();
    ResetStub();
    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    TestVersionGate();
    TestInitialize();
    TestDebugLayerProbe();
    TestContextHandle();
    TestFeatureGates();
    TestGetData();
    TestDeviceStatus();
    TestPageFaultInformation();
    TestResultConstants();
    TestLifecycle();

    ::printf("\n====================================================\n");
    ::printf("%d checks, %d failure(s)\n", g_checks, g_failures);

    return (g_failures == 0) ? 0 : 1;
}
