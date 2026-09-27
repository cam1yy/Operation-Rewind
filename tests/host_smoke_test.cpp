// =============================================================================
//  host_smoke_test.cpp
// -----------------------------------------------------------------------------
//  Behavioural test for the reconstruction, runnable on any host.
//
//  There is no nvapi64.dll and no D3D device here, so the test installs its own
//  NVAPI query function that hands out recording stubs.  That exercises the
//  parts of the library that are actually reconstructed -- the version gate,
//  the feature flag gates, the handle layout and dispatch, the driver status
//  translation and the per-context failure encoding -- while replacing only the
//  two things that cannot exist off Windows: the module loader and the driver.
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
// Recording driver stubs
//
//   The signatures mirror the typedefs in src/nvapi/aftermath_driver.cpp.  They
//   are defined with C linkage so the function pointer casts behave exactly as
//   they do under MSVC.
// -----------------------------------------------------------------------------
struct StubState
{
    bool     initializeCalled;
    bool     setEventMarkerCalled;
    bool     getDataCalled;
    bool     getDeviceStatusCalled;
    bool     pageFaultCalled;

    uint32_t lastVersion;
    uint32_t lastFlags;
    uint32_t lastMarkerSize;
    void*    lastDevice;
    void*    lastContext;

    // What the "driver" should report next.
    GFSDK_Aftermath_Result initializeResult;
    GFSDK_Aftermath_Result getDataResult;
    uint32_t                getDataStatus;
    uint32_t                deviceStatus;
    GFSDK_Aftermath_Result deviceStatusResult;

    const void* markerPayload;
};

static StubState g_stub;

static void ResetStub()
{
    ::memset(&g_stub, 0, sizeof(g_stub));
    g_stub.initializeResult   = GFSDK_Aftermath_Result_Success;
    g_stub.getDataResult      = GFSDK_Aftermath_Result_Success;
    g_stub.deviceStatusResult = GFSDK_Aftermath_Result_Success;
    g_stub.deviceStatus       = 1;                 // -> Active
    g_stub.markerPayload      = "marker";
}

extern "C" {

static GFSDK_Aftermath_Result StubInitialize(
    void* pDevice, uint32_t version, uint32_t flags)
{
    g_stub.initializeCalled = true;
    g_stub.lastDevice       = pDevice;
    g_stub.lastVersion      = version;
    g_stub.lastFlags        = flags;
    return g_stub.initializeResult;
}

static GFSDK_Aftermath_Result StubSetEventMarker(
    void* pContext, void* pDriverState, const void* pMarkerData,
    uint32_t markerDataSize)
{
    (void)pDriverState;
    g_stub.setEventMarkerCalled = true;
    g_stub.lastContext          = pContext;
    g_stub.lastMarkerSize       = markerDataSize;
    return GFSDK_Aftermath_Result_Success;
}

static GFSDK_Aftermath_Result StubGetData(
    void* pContext, void* pDriverState, const void** ppMarkerData,
    uint32_t* pMarkerSize, uint32_t* pContextStatus)
{
    (void)pDriverState;
    g_stub.getDataCalled = true;
    g_stub.lastContext   = pContext;

    if (g_stub.getDataResult == GFSDK_Aftermath_Result_Success)
    {
        *ppMarkerData   = g_stub.markerPayload;
        *pMarkerSize    = 6;
        *pContextStatus = g_stub.getDataStatus;
    }
    return g_stub.getDataResult;
}

static GFSDK_Aftermath_Result StubGetDeviceStatus(
    void* pDevice, uint32_t* pDeviceStatus)
{
    (void)pDevice;
    g_stub.getDeviceStatusCalled = true;
    if (g_stub.deviceStatusResult == GFSDK_Aftermath_Result_Success)
    {
        *pDeviceStatus = g_stub.deviceStatus;
    }
    return g_stub.deviceStatusResult;
}

static GFSDK_Aftermath_Result StubGetPageFaultInfo(
    void* pDevice, void* pPageFaultInfo)
{
    (void)pDevice;
    (void)pPageFaultInfo;
    g_stub.pageFaultCalled = true;
    return GFSDK_Aftermath_Result_Success;
}

// The interface version query has its own signature in the loader.
static uint32_t StubGetInterfaceVersion(void)
{
    return 0x9784;      // exactly NVAPI_MIN_INTERFACE_VERSION
}

} // extern "C"

// -----------------------------------------------------------------------------
// The injected query function.
// -----------------------------------------------------------------------------
static void* TestQueryInterface(uint32_t id)
{
    switch (id)
    {
    case aftermath::nvapi::NVAPI_ID_D3D11_INITIALIZE:
    case aftermath::nvapi::NVAPI_ID_D3D12_INITIALIZE:
        return (void*)&StubInitialize;

    case aftermath::nvapi::NVAPI_ID_D3D11_SET_EVENT_MARKER:
    case aftermath::nvapi::NVAPI_ID_D3D12_SET_EVENT_MARKER:
        return (void*)&StubSetEventMarker;

    case aftermath::nvapi::NVAPI_ID_D3D11_GET_DATA:
    case aftermath::nvapi::NVAPI_ID_D3D12_GET_DATA:
        return (void*)&StubGetData;

    case aftermath::nvapi::NVAPI_ID_D3D11_GET_DEVICE_STATUS:
    case aftermath::nvapi::NVAPI_ID_D3D12_GET_DEVICE_STATUS:
        return (void*)&StubGetDeviceStatus;

    case aftermath::nvapi::NVAPI_ID_D3D11_GET_PAGE_FAULT_INFO:
    case aftermath::nvapi::NVAPI_ID_D3D12_GET_PAGE_FAULT_INFO:
        return (void*)&StubGetPageFaultInfo;

    case aftermath::nvapi::NVAPI_ID_INTERFACE_VERSION:
        return (void*)&StubGetInterfaceVersion;

    default:
        return nullptr;
    }
}


// =============================================================================
// Tests
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Version gate: anything other than 0x13 must be rejected with 0xBAD00006
//    before any other work happens.  [D]
// -----------------------------------------------------------------------------
static void TestVersionGate()
{
    ::printf("\n[1] API version gate\n");

    int dummy = 0;
    ResetStub();
    g_stub.initializeResult = GFSDK_Aftermath_Result_Success;

    CheckResult(GFSDK_Aftermath_DX12_Initialize(0x12, 0, &dummy),
                GFSDK_Aftermath_Result_FAIL_ApiError,
                "version 0x12 rejected with FAIL_ApiError");

    CheckResult(GFSDK_Aftermath_DX11_Initialize(0x14, 0, &dummy),
                GFSDK_Aftermath_Result_FAIL_ApiError,
                "version 0x14 rejected with FAIL_ApiError");

    Check(!g_stub.initializeCalled,
          "driver was not contacted for a rejected version");
}

// -----------------------------------------------------------------------------
// 2. Successful initialisation publishes the flags and reaches the driver.  [D]
// -----------------------------------------------------------------------------
static void TestInitialize()
{
    ::printf("\n[2] Initialize\n");

    int dummyDevice = 0;
    ResetStub();

    const uint32_t flags =
        GFSDK_Aftermath_FeatureFlags_EnableMarkers |
        GFSDK_Aftermath_FeatureFlags_EnableResourceTracking;

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API, flags, &dummyDevice),
                GFSDK_Aftermath_Result_Success,
                "DX12 initialize succeeds");

    Check(g_stub.initializeCalled, "driver initialize was called");
    Check(g_stub.lastDevice == &dummyDevice, "device pointer forwarded");
    Check(g_stub.lastVersion == GFSDK_Aftermath_Version_API,
          "version forwarded");
    Check(g_stub.lastFlags == flags, "feature flags forwarded");

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API, flags, nullptr),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "null device rejected");
}

// -----------------------------------------------------------------------------
// 3. Null pointer arguments on the entry points that require an initialise.  [D]
// -----------------------------------------------------------------------------
static void TestNotInitialized()
{
    ::printf("\n[3] Not-initialized guards\n");

    // Tear the library back down so the flag is clear.
    aftermath::Shutdown();
    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    GFSDK_Aftermath_ContextHandle handle = nullptr;
    GFSDK_Aftermath_Device_Status status = GFSDK_Aftermath_Device_Status_Unknown;

    CheckResult(GFSDK_Aftermath_DX11_CreateContextHandle((void*)1, &handle),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "CreateContextHandle before Initialize");

    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "GetDeviceStatus before Initialize");

    CheckResult(GFSDK_Aftermath_ReleaseContextHandle((GFSDK_Aftermath_ContextHandle)(void*)1),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "ReleaseContextHandle before Initialize");
}

// -----------------------------------------------------------------------------
// 4. Context handle layout.  [D] the object is 0x18 bytes with the API word at
//    +4 and the context pointer at +8.
// -----------------------------------------------------------------------------
static void TestContextHandle()
{
    ::printf("\n[4] Context handle\n");

    int dummyDevice = 0;
    int dummyContext = 0;
    ResetStub();

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &dummyDevice),
                GFSDK_Aftermath_Result_Success,
                "re-initialize for the handle test");

    GFSDK_Aftermath_ContextHandle handle = nullptr;
    CheckResult(GFSDK_Aftermath_DX12_CreateContextHandle(&dummyContext, &handle),
                GFSDK_Aftermath_Result_Success,
                "DX12 CreateContextHandle succeeds");
    Check(handle != nullptr, "handle was returned");

    if (handle != nullptr)
    {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(handle);
        uint32_t api = 0;
        void*    ctx = nullptr;
        uint64_t driverState = 1;
        ::memcpy(&api, raw + 4, sizeof(api));
        ::memcpy(&ctx, raw + 8, sizeof(ctx));
        ::memcpy(&driverState, raw + 16, sizeof(driverState));

        Check(api == 1, "API word at +4 is 1 for D3D12");
        Check(ctx == &dummyContext, "context pointer at +8");
        Check(driverState == 0, "driver state at +16 is null on creation");
    }

    // Release must free without touching the driver.
    CheckResult(GFSDK_Aftermath_ReleaseContextHandle(handle),
                GFSDK_Aftermath_Result_Success,
                "ReleaseContextHandle succeeds");

    // A null handle after initialisation is a parameter error.  [D]
    CheckResult(GFSDK_Aftermath_ReleaseContextHandle(nullptr),
                GFSDK_Aftermath_Result_FAIL_InvalidParameter,
                "null handle rejected with FAIL_InvalidParameter");
}

// -----------------------------------------------------------------------------
// 5. The EnableMarkers gate on SetEventMarker and GetData.  [D]
// -----------------------------------------------------------------------------
static void TestFeatureGates()
{
    ::printf("\n[5] Feature flag gates\n");

    int dummyDevice = 0;

    // Initialize with NO markers enabled.
    ResetStub();
    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_Minimum,
                    &dummyDevice),
                GFSDK_Aftermath_Result_Success,
                "initialize with minimum flags");

    GFSDK_Aftermath_ContextHandle handle = nullptr;
    GFSDK_Aftermath_DX12_CreateContextHandle(&dummyDevice, &handle);

    CheckResult(GFSDK_Aftermath_SetEventMarker(handle, "x", 2),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "SetEventMarker without EnableMarkers");

    Check(!g_stub.setEventMarkerCalled,
          "driver not reached when markers are disabled");

    GFSDK_Aftermath_ContextData entry;
    ::memset(&entry, 0, sizeof(entry));
    const GFSDK_Aftermath_ContextHandle handles[1] = { handle };

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "GetData without EnableMarkers");

    // Page fault information needs resource tracking, not markers.  [D]
    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(nullptr),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "GetPageFaultInformation without EnableResourceTracking");

    GFSDK_Aftermath_ReleaseContextHandle(handle);

    // Now with markers enabled.
    ResetStub();
    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &dummyDevice),
                GFSDK_Aftermath_Result_Success,
                "initialize with EnableMarkers");

    handle = nullptr;
    GFSDK_Aftermath_DX12_CreateContextHandle(&dummyDevice, &handle);

    CheckResult(GFSDK_Aftermath_SetEventMarker(handle, "marker", 6),
                GFSDK_Aftermath_Result_Success,
                "SetEventMarker with EnableMarkers");
    Check(g_stub.setEventMarkerCalled, "driver reached");
    Check(g_stub.lastMarkerSize == 6, "marker size forwarded");

    // Page faults still need resource tracking, so still gated.  [D]
    CheckResult(GFSDK_Aftermath_GetPageFaultInformation(nullptr),
                GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled,
                "GetPageFaultInformation still gated without resource tracking");

    GFSDK_Aftermath_ReleaseContextHandle(handle);
}

// -----------------------------------------------------------------------------
// 6. GetData's per-context error encoding.  [D] a failing context is marked
//    Invalid with the result code widened into the marker pointer slot; the
//    call itself still reports success.
// -----------------------------------------------------------------------------
static void TestGetData()
{
    ::printf("\n[6] GetData per-context encoding\n");

    int dummyDevice = 0;
    ResetStub();

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &dummyDevice),
                GFSDK_Aftermath_Result_Success,
                "initialize");

    GFSDK_Aftermath_ContextHandle handle = nullptr;
    GFSDK_Aftermath_DX12_CreateContextHandle(&dummyDevice, &handle);

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

    // (b) the driver refuses the bundle case: the entry, not the call, carries
    //     the verdict.
    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataResult = GFSDK_Aftermath_Result_FAIL_GetDataOnBundle;

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_Success,
                "GetData still succeeds when an entry fails");
    Check(entry.status == GFSDK_Aftermath_Context_Status_Invalid,
          "failed entry marked Invalid");
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_GetDataOnBundle,
          "entry carries the FAIL_GetDataOnBundle code");

    // (c) a driver-internal status (small negative, outside the 0xBAD0 range)
    //     must go through the translation table rather than being stored
    //     verbatim.  [D] the table: -1 -> Fail, -5 -> InvalidParameter.
    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataResult =
        static_cast<GFSDK_Aftermath_Result>(static_cast<int32_t>(-5));

    CheckResult(GFSDK_Aftermath_GetData(1, handles, &entry),
                GFSDK_Aftermath_Result_Success,
                "GetData tolerates a driver-internal status");
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_InvalidParameter,
          "driver status -5 translated to FAIL_InvalidParameter");

    ::memset(&entry, 0, sizeof(entry));
    g_stub.getDataResult =
        static_cast<GFSDK_Aftermath_Result>(static_cast<int32_t>(-1));

    GFSDK_Aftermath_GetData(1, handles, &entry);
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_Fail,
          "driver status -1 translated to Fail");

    // (d) null handle -> FAIL_InvalidParameter encoded the same way.  [D]
    ::memset(&entry, 0, sizeof(entry));
    const GFSDK_Aftermath_ContextHandle nullHandles[1] = { nullptr };
    g_stub.getDataResult = GFSDK_Aftermath_Result_Success;

    CheckResult(GFSDK_Aftermath_GetData(1, nullHandles, &entry),
                GFSDK_Aftermath_Result_Success, "null handle does not fail the call");
    Check(entry.status == GFSDK_Aftermath_Context_Status_Invalid,
          "null handle entry marked Invalid");
    Check(reinterpret_cast<intptr_t>(entry.markerData) ==
              (intptr_t)(int32_t)GFSDK_Aftermath_Result_FAIL_InvalidParameter,
          "entry carries FAIL_InvalidParameter");

    GFSDK_Aftermath_ReleaseContextHandle(handle);
}

// -----------------------------------------------------------------------------
// 7. Device status translation.  [D] the full table, including the pre-set
//    "no information" value that survives a driver failure.
// -----------------------------------------------------------------------------
static void TestDeviceStatus()
{
    ::printf("\n[7] Device status translation\n");

    int dummyDevice = 0;
    ResetStub();

    CheckResult(GFSDK_Aftermath_DX12_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &dummyDevice),
                GFSDK_Aftermath_Result_Success, "initialize");

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
        GFSDK_Aftermath_Device_Status status =
            GFSDK_Aftermath_Device_Status_Unknown;
        g_stub.deviceStatus = cases[i].driver;

        char label[96];
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

    // Driver failure: the status word keeps the pre-set 4.  [D]
    GFSDK_Aftermath_Device_Status status = GFSDK_Aftermath_Device_Status_Active;
    g_stub.deviceStatusResult = GFSDK_Aftermath_Result_Fail;

    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_Fail,
                "driver failure mapped through the status table");
    Check(status == GFSDK_Aftermath_Device_Status_Unknown,
          "status word keeps the pre-set Unknown value");
}

// -----------------------------------------------------------------------------
// 8. The status translation table itself.  [D] reproduced as a unit check so a
//    transcription error in the switch is caught directly.
// -----------------------------------------------------------------------------
static void TestResultConstants()
{
    ::printf("\n[8] Result code constants\n");

    Check(GFSDK_Aftermath_Result_Fail == 0xBAD00000u, "Fail base is 0xBAD00000");
    Check(GFSDK_Aftermath_Result_Success == 1u, "Success is 1");
    Check(GFSDK_Aftermath_Result_NotAvailable == 2u, "NotAvailable is 2");
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

    // The three per-entry sentinels seen as negative immediates in the
    // disassembly must decode onto these constants.
    Check((int32_t)GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext == (int32_t)-0x452FFFF1,
          "-0x452FFFF1 decodes to FAIL_GetDataOnDeferredContext");
    Check((int32_t)GFSDK_Aftermath_Result_FAIL_GetDataOnBundle == (int32_t)-0x452FFFF2,
          "-0x452FFFF2 decodes to FAIL_GetDataOnBundle");
    Check((int32_t)GFSDK_Aftermath_Result_FAIL_InvalidParameter == (int32_t)-0x452FFFFC,
          "-0x452FFFFC decodes to FAIL_InvalidParameter");
}

// -----------------------------------------------------------------------------
// 9. Shutdown leaves the library able to re-initialise.
// -----------------------------------------------------------------------------
static void TestShutdown()
{
    ::printf("\n[9] Shutdown\n");

    int dummyDevice = 0;
    ResetStub();

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &dummyDevice),
                GFSDK_Aftermath_Result_Success, "DX11 initialize");

    GFSDK_Aftermath_Device_Status status = GFSDK_Aftermath_Device_Status_Unknown;
    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_Success, "usable before shutdown");

    aftermath::Shutdown();

    CheckResult(GFSDK_Aftermath_GetDeviceStatus(&status),
                GFSDK_Aftermath_Result_FAIL_NotInitialized,
                "not usable after shutdown");

    // Re-install the hook: Shutdown() drops the cached interfaces.
    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    CheckResult(GFSDK_Aftermath_DX11_Initialize(
                    GFSDK_Aftermath_Version_API,
                    GFSDK_Aftermath_FeatureFlags_EnableMarkers,
                    &dummyDevice),
                GFSDK_Aftermath_Result_Success, "re-initialize after shutdown");
}

// =============================================================================
int main()
{
    ::printf("GFSDK_Aftermath_Lib reconstruction -- host smoke test\n");
    ::printf("====================================================\n");

    aftermath::nvapi::SetQueryInterfaceForTesting(&TestQueryInterface);

    TestVersionGate();
    TestInitialize();
    TestNotInitialized();
    TestContextHandle();
    TestFeatureGates();
    TestGetData();
    TestDeviceStatus();
    TestResultConstants();
    TestShutdown();

    ::printf("\n====================================================\n");
    ::printf("%d checks, %d failure(s)\n", g_checks, g_failures);

    return (g_failures == 0) ? 0 : 1;
}
