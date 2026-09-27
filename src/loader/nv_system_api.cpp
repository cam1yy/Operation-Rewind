/*
 * nv_system_api.cpp -- the GetProcAddress plumbing that the decompiled helpers perform
 * inline (for example inside sub_180002420 for Shell32 and sub_1800028A0 for Advapi32).
 *
 * Pulling it into one place removes a lot of duplicated decompiler noise without changing
 * behaviour: the libraries are still loaded lazily, still by absolute System32 path, and
 * a missing export still makes the caller fail gracefully.
 */

#include "loader/nv_system_api.h"

#include "loader/nv_path_utils.h"

namespace loader {
namespace {

/* Loads a system library exactly once; NULL means "unavailable". */
HMODULE LoadSystemLibraryOnce(HMODULE* pCache, const wchar_t* fileName)
{
    if (*pCache == NULL)
    {
        *pCache = LoadLibraryFromSystemDirectory(fileName, 0);
    }
    return *pCache;
}

template <typename PfnType>
bool Bind(PfnType* pOut, HMODULE module, const char* exportName)
{
    *pOut = reinterpret_cast<PfnType>(::GetProcAddress(module, exportName));
    return *pOut != NULL;
}

HMODULE g_shell32  = NULL;
HMODULE g_advapi32 = NULL;
HMODULE g_setupapi = NULL;

ShellApi    g_shellApi    = {};
RegistryApi g_registryApi = {};
ServiceApi  g_serviceApi  = {};
SetupApi    g_setupApi    = {};

bool g_shellApiReady    = false;
bool g_registryApiReady = false;
bool g_serviceApiReady  = false;
bool g_setupApiReady    = false;

} /* anonymous namespace */

const ShellApi* GetShellApi()
{
    if (!g_shellApiReady)
    {
        const HMODULE module = LoadSystemLibraryOnce(&g_shell32, L"Shell32.dll");
        if (module == NULL)
        {
            return NULL;
        }
        if (!Bind(&g_shellApi.SHGetFolderPathW, module, "SHGetFolderPathW"))
        {
            return NULL;
        }
        g_shellApiReady = true;
    }
    return &g_shellApi;
}

const RegistryApi* GetRegistryApi()
{
    if (!g_registryApiReady)
    {
        const HMODULE module = LoadSystemLibraryOnce(&g_advapi32, L"Advapi32.dll");
        if (module == NULL)
        {
            return NULL;
        }
        if (!Bind(&g_registryApi.RegOpenKeyExW,    module, "RegOpenKeyExW") ||
            !Bind(&g_registryApi.RegQueryValueExW, module, "RegQueryValueExW") ||
            !Bind(&g_registryApi.RegEnumValueW,    module, "RegEnumValueW") ||
            !Bind(&g_registryApi.RegCloseKey,      module, "RegCloseKey"))
        {
            return NULL;
        }
        g_registryApiReady = true;
    }
    return &g_registryApi;
}

const ServiceApi* GetServiceApi()
{
    if (!g_serviceApiReady)
    {
        const HMODULE module = LoadSystemLibraryOnce(&g_advapi32, L"Advapi32.dll");
        if (module == NULL)
        {
            return NULL;
        }
        if (!Bind(&g_serviceApi.OpenSCManagerW,      module, "OpenSCManagerW") ||
            !Bind(&g_serviceApi.OpenServiceW,        module, "OpenServiceW") ||
            !Bind(&g_serviceApi.QueryServiceConfigW, module, "QueryServiceConfigW") ||
            !Bind(&g_serviceApi.CloseServiceHandle,  module, "CloseServiceHandle"))
        {
            return NULL;
        }
        g_serviceApiReady = true;
    }
    return &g_serviceApi;
}

const SetupApi* GetSetupApi()
{
    if (!g_setupApiReady)
    {
        const HMODULE module = LoadSystemLibraryOnce(&g_setupapi, L"SetupApi.dll");
        if (module == NULL)
        {
            return NULL;
        }
        if (!Bind(&g_setupApi.SetupDiGetClassDevsW,              module, "SetupDiGetClassDevsW") ||
            !Bind(&g_setupApi.SetupDiEnumDeviceInterfaces,       module, "SetupDiEnumDeviceInterfaces") ||
            !Bind(&g_setupApi.SetupDiGetDeviceInterfaceDetailW,  module, "SetupDiGetDeviceInterfaceDetailW") ||
            !Bind(&g_setupApi.SetupDiDestroyDeviceInfoList,      module, "SetupDiDestroyDeviceInfoList") ||
            !Bind(&g_setupApi.SetupDiGetDeviceRegistryPropertyW, module, "SetupDiGetDeviceRegistryPropertyW"))
        {
            return NULL;
        }

        /* Vista and newer only; sub_180003A60 checks the OS build before using them and
         * fails with ERROR_CALL_NOT_IMPLEMENTED when they are missing. */
        Bind(&g_setupApi.SetupDiGetDevicePropertyW,        module, "SetupDiGetDevicePropertyW");
        Bind(&g_setupApi.SetupGetInfDriverStoreLocationW,  module, "SetupGetInfDriverStoreLocationW");

        g_setupApiReady = true;
    }
    return &g_setupApi;
}

} /* namespace loader */
