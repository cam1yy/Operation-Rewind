/*
 * smoke_test.cpp -- run this on Windows against the rebuilt DLL.
 *
 *      cl /EHsc /W4 /I..\include smoke_test.cpp
 *      smoke_test.exe ..\build\x64\Release\GFSDK_Aftermath_Lib.x64.dll
 *
 * It checks the two things a rebuild can get wrong without anyone noticing:
 *
 *   1. the export table -- all nine names must resolve, and each must sit on the ordinal
 *      the original DLL used (GetProcAddress by ordinal must return the same address);
 *   2. the documented pre-initialisation behaviour of every entry point.
 *
 * It deliberately never touches a real D3D device, so it is safe to run anywhere,
 * including on machines without an NVIDIA GPU.
 */

#include <windows.h>
#include <stdio.h>

#include "GFSDK_Aftermath.h"

namespace {

struct ExpectedExport
{
    const char* name;
    WORD        ordinal;
};

const ExpectedExport kExpectedExports[] =
{
    { "GFSDK_Aftermath_DX11_CreateContextHandle", 1 },
    { "GFSDK_Aftermath_DX11_Initialize",          2 },
    { "GFSDK_Aftermath_DX12_CreateContextHandle", 3 },
    { "GFSDK_Aftermath_DX12_Initialize",          4 },
    { "GFSDK_Aftermath_GetData",                  5 },
    { "GFSDK_Aftermath_GetDeviceStatus",          6 },
    { "GFSDK_Aftermath_GetPageFaultInformation",  7 },
    { "GFSDK_Aftermath_ReleaseContextHandle",     8 },
    { "GFSDK_Aftermath_SetEventMarker",           9 }
};

int g_failures = 0;

void Check(bool condition, const char* what)
{
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
    {
        ++g_failures;
    }
}

typedef GFSDK_Aftermath_Result(__cdecl* PfnGetDeviceStatus)(GFSDK_Aftermath_Device_Status*);
typedef GFSDK_Aftermath_Result(__cdecl* PfnGetPageFaultInformation)(GFSDK_Aftermath_PageFaultInformation*);
typedef GFSDK_Aftermath_Result(__cdecl* PfnSetEventMarker)(GFSDK_Aftermath_ContextHandle, const void*, unsigned int);
typedef GFSDK_Aftermath_Result(__cdecl* PfnGetData)(unsigned int, const GFSDK_Aftermath_ContextHandle*, GFSDK_Aftermath_ContextData*);
typedef GFSDK_Aftermath_Result(__cdecl* PfnCreateContextHandle)(void*, GFSDK_Aftermath_ContextHandle*);
typedef GFSDK_Aftermath_Result(__cdecl* PfnInitialize)(GFSDK_Aftermath_Version, unsigned int, void*);

} /* anonymous namespace */

int main(int argc, char** argv)
{
    const char* const modulePath = (argc > 1) ? argv[1] : "GFSDK_Aftermath_Lib.x64.dll";

    const HMODULE module = LoadLibraryA(modulePath);
    if (module == NULL)
    {
        printf("could not load %s (error %lu)\n", modulePath, GetLastError());
        return 1;
    }

    printf("Export table (%s)\n", modulePath);
    for (size_t i = 0; i < sizeof(kExpectedExports) / sizeof(kExpectedExports[0]); ++i)
    {
        const ExpectedExport& expected = kExpectedExports[i];

        const FARPROC byName = GetProcAddress(module, expected.name);
        const FARPROC byOrdinal = GetProcAddress(module, MAKEINTRESOURCEA(expected.ordinal));

        char message[256];
        sprintf_s(message, "%s == @%u", expected.name, expected.ordinal);
        Check(byName != NULL && byName == byOrdinal, message);
    }

    printf("\nPre-initialisation behaviour\n");
    {
        const PfnGetDeviceStatus getDeviceStatus =
            (PfnGetDeviceStatus)GetProcAddress(module, "GFSDK_Aftermath_GetDeviceStatus");
        const PfnGetPageFaultInformation getPageFaultInformation =
            (PfnGetPageFaultInformation)GetProcAddress(module, "GFSDK_Aftermath_GetPageFaultInformation");
        const PfnSetEventMarker setEventMarker =
            (PfnSetEventMarker)GetProcAddress(module, "GFSDK_Aftermath_SetEventMarker");
        const PfnGetData getData =
            (PfnGetData)GetProcAddress(module, "GFSDK_Aftermath_GetData");
        const PfnCreateContextHandle createContextHandle =
            (PfnCreateContextHandle)GetProcAddress(module, "GFSDK_Aftermath_DX11_CreateContextHandle");

        GFSDK_Aftermath_Device_Status deviceStatus = GFSDK_Aftermath_Device_Status_Active;
        GFSDK_Aftermath_ContextHandle contextHandle = NULL;

        Check(getDeviceStatus(&deviceStatus) == GFSDK_Aftermath_Result_FAIL_NotInitialized,
              "GetDeviceStatus -> FAIL_NotInitialized");
        Check(getPageFaultInformation(NULL) == GFSDK_Aftermath_Result_FAIL_NotInitialized,
              "GetPageFaultInformation -> FAIL_NotInitialized");
        Check(setEventMarker(NULL, "x", 1) == GFSDK_Aftermath_Result_FAIL_NotInitialized,
              "SetEventMarker -> FAIL_NotInitialized");
        Check(getData(0, NULL, NULL) == GFSDK_Aftermath_Result_FAIL_NotInitialized,
              "GetData -> FAIL_NotInitialized");
        Check(createContextHandle(NULL, &contextHandle) == GFSDK_Aftermath_Result_FAIL_NotInitialized,
              "DX11_CreateContextHandle -> FAIL_NotInitialized");
    }

    printf("\nInitialisation guard\n");
    {
        const PfnInitialize dx11Initialize =
            (PfnInitialize)GetProcAddress(module, "GFSDK_Aftermath_DX11_Initialize");

        /* First call claims the one-shot guard and gets as far as the version check. */
        Check(dx11Initialize((GFSDK_Aftermath_Version)0x999, 0, NULL) ==
                  GFSDK_Aftermath_Result_FAIL_VersionMismatch,
              "DX11_Initialize(bad version) -> FAIL_VersionMismatch");

        /* The guard is never released, so every later call short circuits to Success
         * without initialising anything.  This reproduces the original's behaviour --
         * see docs/RECONSTRUCTION.md, "Quirks kept on purpose". */
        Check(dx11Initialize(GFSDK_Aftermath_Version_API, 0, NULL) ==
                  GFSDK_Aftermath_Result_Success,
              "DX11_Initialize(after a failed attempt) -> Success (one-shot guard)");

        GFSDK_Aftermath_Device_Status deviceStatus = GFSDK_Aftermath_Device_Status_Active;
        const PfnGetDeviceStatus getDeviceStatus =
            (PfnGetDeviceStatus)GetProcAddress(module, "GFSDK_Aftermath_GetDeviceStatus");
        Check(getDeviceStatus(&deviceStatus) == GFSDK_Aftermath_Result_FAIL_NotInitialized,
              "... and the library still reports FAIL_NotInitialized");
    }

    FreeLibrary(module);

    printf("\n%s\n", (g_failures == 0) ? "smoke test passed" : "smoke test FAILED");
    return (g_failures == 0) ? 0 : 1;
}
