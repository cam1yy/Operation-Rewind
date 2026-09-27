/*
 * nv_driver_store.cpp -- reconstruction of the driver module discovery helpers.
 *
 * The strategy the original code implements, in order:
 *
 *   1. Ask the display device's service (and its upper/lower filters) for a kernel mode
 *      binary whose file name matches      -> nvlddmkm.sys & friends.
 *   2. Look at HKLM\SYSTEM\...\Control\Class\{driver key}\UserModeDriverName,
 *      OpenGLDriverName and UserModeDListDriverName.
 *   3. Look at the value *names* under the Khronos Vulkan/OpenCL registration keys.
 *   4. Look at DriverSupportModules in the same class key.
 *   5. Failing all of that, fall back to the INF driver store directory for the device
 *      (Windows 10 1511+) or to System32.
 *
 * This is how nvapi64.dll can be loaded by absolute path from the DriverStore instead of
 * being picked up from System32, which is the whole point of the hardened loader.
 */

#include "loader/nv_driver_store.h"

#include <string.h>
#include <wchar.h>

#include "loader/nv_path_utils.h"
#include "loader/nv_system_api.h"

namespace loader {
namespace {

/*
 * 0x180011360 -- GUID_DISPLAY_DEVICE_ARRIVAL, the device interface class every display
 * adapter publishes.  The raw bytes are in .rdata, which was not part of the supplied
 * listings; the value is inferred from the call (SetupDiGetClassDevsW with
 * DIGCF_DEVICEINTERFACE while looking for "VEN_10DE") and matches ntddvdeo.h.
 */
const GUID kDisplayDeviceInterfaceGuid =
    { 0x1CA05180, 0xA699, 0x450A, { 0x9A, 0x0C, 0xDE, 0x4F, 0xBE, 0x3D, 0xDD, 0x89 } };

/*
 * 0x180011348 -- a 20 byte DEVPROPKEY handed to SetupDiGetDevicePropertyW and whose
 * result is passed straight to SetupGetInfDriverStoreLocationW, i.e.
 * DEVPKEY_Device_DriverInfPath.  Same caveat as above: inferred from usage.
 */
const DEVPROPKEY kDevPkeyDeviceDriverInfPath =
    { { 0xA8B865DD, 0x2E3D, 0x4094, { 0xAD, 0x97, 0xE5, 0x93, 0xA7, 0x0C, 0x75, 0xD6 } }, 5 };

const wchar_t kDeviceClassKeyPrefix[] = L"SYSTEM\\CurrentControlSet\\Control\\Class\\";

/* ---------------------------------------------------------------------------------- *
 * sub_180002C60 -- first present display adapter whose device path contains VEN_10DE.
 *
 * On success the caller owns *pOutDeviceInfoSet and must destroy it.
 * ---------------------------------------------------------------------------------- */
bool FindNvidiaDisplayDevice(const GUID* interfaceClassGuid,
                             HDEVINFO* pOutDeviceInfoSet,
                             SP_DEVINFO_DATA* pOutDeviceInfoData)
{
    const SetupApi* const setup = GetSetupApi();
    if (setup == NULL)
    {
        return false;
    }

    const HDEVINFO deviceInfoSet =
        setup->SetupDiGetClassDevsW(interfaceClassGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool found = false;

    for (DWORD memberIndex = 0; !found; ++memberIndex)
    {
        SP_DEVICE_INTERFACE_DATA interfaceData;
        ::ZeroMemory(&interfaceData, sizeof(interfaceData));
        interfaceData.cbSize = sizeof(interfaceData);          /* 32 on x64 */

        if (!setup->SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, interfaceClassGuid, memberIndex, &interfaceData))
        {
            break;   /* ERROR_NO_MORE_ITEMS */
        }

        DWORD requiredSize = 0;
        setup->SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, NULL, 0, &requiredSize, NULL);
        if (requiredSize == 0)
        {
            continue;
        }

        SP_DEVICE_INTERFACE_DETAIL_DATA_W* const detail =
            static_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(::LocalAlloc(LPTR, requiredSize));
        if (detail == NULL)
        {
            break;
        }
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);   /* 8 on x64 */

        SP_DEVINFO_DATA deviceInfoData;
        ::ZeroMemory(&deviceInfoData, sizeof(deviceInfoData));
        deviceInfoData.cbSize = sizeof(deviceInfoData);               /* 32 on x64 */

        if (setup->SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detail,
                                                    requiredSize, NULL, &deviceInfoData))
        {
            _wcsupr(detail->DevicePath);
            if (wcsstr(detail->DevicePath, L"VEN_10DE") != NULL)
            {
                found = true;
                *pOutDeviceInfoSet  = deviceInfoSet;
                *pOutDeviceInfoData = deviceInfoData;
            }
        }

        ::LocalFree(detail);
    }

    if (!found)
    {
        setup->SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }
    return found;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800030B0 -- binary path of <serviceName>, if it is a kernel driver whose image
 * file name matches <moduleName>.
 * ---------------------------------------------------------------------------------- */
wchar_t* FindServiceBinaryPath(const wchar_t* moduleName, const wchar_t* serviceName)
{
    const ServiceApi* const services = GetServiceApi();
    if (services == NULL)
    {
        return NULL;
    }

    const SC_HANDLE serviceManager = services->OpenSCManagerW(NULL, NULL, GENERIC_READ);
    if (serviceManager == NULL)
    {
        return NULL;
    }

    wchar_t* result = NULL;

    const SC_HANDLE service = services->OpenServiceW(serviceManager, serviceName, GENERIC_READ);
    if (service != NULL)
    {
        DWORD requiredSize = 0;
        services->QueryServiceConfigW(service, NULL, 0, &requiredSize);

        if (requiredSize != 0)
        {
            QUERY_SERVICE_CONFIGW* const config =
                static_cast<QUERY_SERVICE_CONFIGW*>(::LocalAlloc(LPTR, requiredSize));
            if (config != NULL)
            {
                if (services->QueryServiceConfigW(service, config, requiredSize, &requiredSize) &&
                    config->dwServiceType == SERVICE_KERNEL_DRIVER &&
                    FileNameMatchesModule(config->lpBinaryPathName, moduleName))
                {
                    result = DuplicateString(config->lpBinaryPathName);
                }
                ::LocalFree(config);
            }
        }

        services->CloseServiceHandle(service);
    }

    services->CloseServiceHandle(serviceManager);
    return result;
}

/* Walks a REG_MULTI_SZ block, calling Visitor for each string until one returns non NULL. */
template <typename Visitor>
wchar_t* ForEachMultiSzString(const wchar_t* multiSz, Visitor& visitor)
{
    for (const wchar_t* cursor = multiSz; *cursor != L'\0'; cursor += wcslen(cursor) + 1)
    {
        wchar_t* const result = visitor(cursor);
        if (result != NULL)
        {
            return result;
        }
    }
    return NULL;
}

struct ServiceLookupVisitor
{
    const wchar_t* moduleName;
    wchar_t* operator()(const wchar_t* serviceName) const
    {
        return FindServiceBinaryPath(moduleName, serviceName);
    }
};

/* ---------------------------------------------------------------------------------- *
 * sub_1800032E0 -- SPDRP_SERVICE / SPDRP_UPPERFILTERS / SPDRP_LOWERFILTERS of the
 * display adapter.
 * ---------------------------------------------------------------------------------- */
wchar_t* FindDriverModuleViaService(const wchar_t* moduleName, const GUID* interfaceClassGuid)
{
    const SetupApi* const setup = GetSetupApi();
    if (setup == NULL)
    {
        return NULL;
    }

    HDEVINFO deviceInfoSet = NULL;
    SP_DEVINFO_DATA deviceInfoData;
    if (!FindNvidiaDisplayDevice(interfaceClassGuid, &deviceInfoSet, &deviceInfoData))
    {
        return NULL;
    }

    static const DWORD kProperties[] = { SPDRP_SERVICE, SPDRP_UPPERFILTERS, SPDRP_LOWERFILTERS };

    ServiceLookupVisitor visitor;
    visitor.moduleName = moduleName;

    wchar_t* result = NULL;

    for (size_t i = 0; i < sizeof(kProperties) / sizeof(kProperties[0]) && result == NULL; ++i)
    {
        DWORD propertyType = REG_NONE;
        DWORD requiredSize = 0;

        setup->SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, kProperties[i],
                                                 &propertyType, NULL, 0, &requiredSize);
        if (requiredSize == 0)
        {
            continue;
        }

        /* Two extra WCHARs so the buffer is always a valid (multi) string. */
        wchar_t* const buffer =
            static_cast<wchar_t*>(::LocalAlloc(LPTR, requiredSize + 2 * sizeof(wchar_t)));
        if (buffer == NULL)
        {
            break;
        }

        if (setup->SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, kProperties[i],
                                                     &propertyType, reinterpret_cast<PBYTE>(buffer),
                                                     requiredSize, NULL))
        {
            if (propertyType == REG_SZ)
            {
                result = FindServiceBinaryPath(moduleName, buffer);
            }
            else if (propertyType == REG_MULTI_SZ)
            {
                result = ForEachMultiSzString(buffer, visitor);
            }
        }

        ::LocalFree(buffer);
    }

    setup->SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return result;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800035A0 -- named REG_SZ / REG_MULTI_SZ values under one key.
 * ---------------------------------------------------------------------------------- */
wchar_t* FindDriverModuleInRegistryValues(const wchar_t* moduleName, HKEY rootKey,
                                          const wchar_t* subKey, const wchar_t* const* valueNames)
{
    const RegistryApi* const registry = GetRegistryApi();
    if (registry == NULL)
    {
        return NULL;
    }

    HKEY key = NULL;
    if (registry->RegOpenKeyExW(rootKey, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return NULL;
    }

    wchar_t* result = NULL;

    for (const wchar_t* const* valueName = valueNames; *valueName != NULL && result == NULL; ++valueName)
    {
        DWORD valueType = REG_NONE;
        void* data = NULL;

        if (RegQueryValueAlloc(key, *valueName, &valueType, &data) != ERROR_SUCCESS || data == NULL)
        {
            continue;
        }

        const wchar_t* const text = static_cast<const wchar_t*>(data);

        if (valueType == REG_SZ)
        {
            if (FileNameMatchesModule(text, moduleName))
            {
                result = DuplicateString(text);
            }
        }
        else if (valueType == REG_MULTI_SZ)
        {
            for (const wchar_t* cursor = text; *cursor != L'\0'; cursor += wcslen(cursor) + 1)
            {
                if (FileNameMatchesModule(cursor, moduleName))
                {
                    result = DuplicateString(cursor);
                    break;
                }
            }
        }
        else
        {
            ::SetLastError(ERROR_INVALID_DATA);
        }

        ::LocalFree(data);
    }

    registry->RegCloseKey(key);
    return result;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800037B0 -- the Khronos style keys, where the *value name* is the module path.
 * ---------------------------------------------------------------------------------- */
wchar_t* FindDriverModuleByEnumeratingValues(const wchar_t* moduleName, HKEY rootKey,
                                             const wchar_t* const* subKeys)
{
    const RegistryApi* const registry = GetRegistryApi();
    if (registry == NULL)
    {
        return NULL;
    }

    wchar_t* result = NULL;

    for (const wchar_t* const* subKey = subKeys; *subKey != NULL && result == NULL; ++subKey)
    {
        HKEY key = NULL;
        if (registry->RegOpenKeyExW(rootKey, *subKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            continue;
        }

        /* 0x10000 bytes; the enumeration asks for at most 0x7FFF characters. */
        wchar_t* const valueName = static_cast<wchar_t*>(::LocalAlloc(LPTR, 0x10000));
        if (valueName != NULL)
        {
            for (DWORD index = 0; ; ++index)
            {
                DWORD cchValueName = 0x7FFF;
                if (registry->RegEnumValueW(key, index, valueName, &cchValueName,
                                            NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                {
                    break;   /* ERROR_NO_MORE_ITEMS */
                }

                if (FileNameMatchesModule(valueName, moduleName))
                {
                    result = DuplicateString(valueName);
                    break;
                }
            }

            ::LocalFree(valueName);
        }

        registry->RegCloseKey(key);
    }

    return result;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180003A60 -- DriverStore directory that holds the display driver's INF.
 * ---------------------------------------------------------------------------------- */
wchar_t* GetDriverStoreDirectory(const GUID* interfaceClassGuid)
{
    const SetupApi* const setup = GetSetupApi();

    if (!IsWindowsBuildOrGreater(kWindowsVistaBuild6000) ||
        setup == NULL ||
        setup->SetupDiGetDevicePropertyW == NULL ||
        setup->SetupGetInfDriverStoreLocationW == NULL)
    {
        ::SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return NULL;
    }

    HDEVINFO deviceInfoSet = NULL;
    SP_DEVINFO_DATA deviceInfoData;
    if (!FindNvidiaDisplayDevice(interfaceClassGuid, &deviceInfoSet, &deviceInfoData))
    {
        return NULL;
    }

    wchar_t* result = NULL;

    DEVPROPTYPE propertyType = 0;
    DWORD requiredBytes = 0;
    setup->SetupDiGetDevicePropertyW(deviceInfoSet, &deviceInfoData, &kDevPkeyDeviceDriverInfPath,
                                     &propertyType, NULL, 0, &requiredBytes, 0);

    if (requiredBytes != 0)
    {
        /* The binary allocates 2 * RequiredSize even though RequiredSize is already a
         * byte count; the slack is harmless and is reproduced here. */
        wchar_t* const infName = static_cast<wchar_t*>(::LocalAlloc(LPTR, 2 * requiredBytes));
        if (infName != NULL)
        {
            if (setup->SetupDiGetDevicePropertyW(deviceInfoSet, &deviceInfoData,
                                                 &kDevPkeyDeviceDriverInfPath, &propertyType,
                                                 reinterpret_cast<PBYTE>(infName), requiredBytes,
                                                 NULL, 0))
            {
                DWORD cchRequired = 0;
                setup->SetupGetInfDriverStoreLocationW(infName, NULL, NULL, NULL, 0, &cchRequired);

                if (cchRequired != 0)
                {
                    wchar_t* const infPath =
                        static_cast<wchar_t*>(::LocalAlloc(LPTR, cchRequired * sizeof(wchar_t)));
                    if (infPath != NULL)
                    {
                        if (setup->SetupGetInfDriverStoreLocationW(infName, NULL, NULL, infPath,
                                                                   cchRequired, &cchRequired))
                        {
                            /* Keep the directory, drop "\\<inf file name>". */
                            const wchar_t* const separator = wcsrchr(infPath, L'\\');
                            if (separator != NULL)
                            {
                                result = DuplicateStringN(infPath, static_cast<size_t>(separator - infPath));
                            }
                        }
                        ::LocalFree(infPath);
                    }
                }
            }
            ::LocalFree(infName);
        }
    }

    setup->SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return result;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180003EA0 -- HKLM\SYSTEM\CurrentControlSet\Control\Class\{class}\<instance>
 * ---------------------------------------------------------------------------------- */
wchar_t* GetDisplayDeviceDriverKeyPath(const GUID* interfaceClassGuid)
{
    const SetupApi* const setup = GetSetupApi();
    if (setup == NULL)
    {
        return NULL;
    }

    HDEVINFO deviceInfoSet = NULL;
    SP_DEVINFO_DATA deviceInfoData;
    if (!FindNvidiaDisplayDevice(interfaceClassGuid, &deviceInfoSet, &deviceInfoData))
    {
        return NULL;
    }

    wchar_t* result = NULL;

    DWORD propertyType = REG_NONE;
    DWORD requiredSize = 0;
    setup->SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_DRIVER,
                                             &propertyType, NULL, 0, &requiredSize);

    if (requiredSize != 0)
    {
        wchar_t* const driverKey =
            static_cast<wchar_t*>(::LocalAlloc(LPTR, requiredSize + sizeof(wchar_t)));
        if (driverKey != NULL)
        {
            if (setup->SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_DRIVER,
                                                         &propertyType, reinterpret_cast<PBYTE>(driverKey),
                                                         requiredSize, NULL) &&
                propertyType == REG_SZ)
            {
                result = JoinPath(kDeviceClassKeyPrefix, driverKey);
            }
            ::LocalFree(driverKey);
        }
    }

    setup->SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return result;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800040D0 -- the module path exactly as the driver recorded it (may be relative).
 * ---------------------------------------------------------------------------------- */
wchar_t* ResolveDriverModuleName(const wchar_t* moduleName)
{
    ::SetLastError(ERROR_SUCCESS);

    if (moduleName == NULL || PathContainsSeparator(moduleName))
    {
        ::SetLastError(ERROR_BAD_ARGUMENTS);
        return NULL;
    }

    /*
     * The binary builds both the native and the WOW64 spelling of these value names and
     * then uses the one that matches its own bitness.  Only the native list is reachable
     * in the 64 bit build; the WOW list is kept for completeness.
     */
#if defined(_WIN64)
    static const wchar_t* const kUserModeValueNames[] =
    {
        L"UserModeDriverName",
        L"OpenGLDriverName",
        L"UserModeDListDriverName",
        NULL
    };
    static const wchar_t* const kSupportModuleValueNames[] = { L"DriverSupportModules", NULL };
#else
    /* A 32 bit build reads the WOW64 spellings instead; both sets are written to the
     * stack by the original function, which then picks one according to its bitness. */
    static const wchar_t* const kUserModeValueNames[] =
    {
        L"UserModeDriverNameWow",
        L"OpenGLDriverNameWow",
        L"UserModeDListDriverNameWow",
        NULL
    };
    static const wchar_t* const kSupportModuleValueNames[] = { L"DriverSupportModulesWow", NULL };
#endif

    static const wchar_t* const kKhronosKeys[] =
    {
        L"SOFTWARE\\Khronos\\Vulkan\\Drivers",
        L"SOFTWARE\\Khronos\\OpenCL\\Vendors",
        NULL
    };

    wchar_t* result = NULL;

    wchar_t* const classKeyPath = GetDisplayDeviceDriverKeyPath(&kDisplayDeviceInterfaceGuid);
    if (classKeyPath != NULL)
    {
        wchar_t* const fileName = EnsureDllExtension(moduleName);
        if (fileName != NULL)
        {
            const wchar_t* const extension = wcsrchr(fileName, L'.');
            const bool looksLikeDll = extension != NULL && _wcsicmp(extension, L".dll") == 0;
            const bool looksLikeSys = extension != NULL && _wcsicmp(extension, L".sys") == 0;

            if (!looksLikeDll && ::GetLastError() == ERROR_SUCCESS)
            {
                result = FindDriverModuleViaService(fileName, &kDisplayDeviceInterfaceGuid);
            }

            if (!looksLikeSys)
            {
                if (result == NULL && ::GetLastError() == ERROR_SUCCESS)
                {
                    result = FindDriverModuleInRegistryValues(fileName, HKEY_LOCAL_MACHINE,
                                                              classKeyPath, kUserModeValueNames);
                }
                if (result == NULL && ::GetLastError() == ERROR_SUCCESS)
                {
                    result = FindDriverModuleByEnumeratingValues(fileName, HKEY_LOCAL_MACHINE, kKhronosKeys);
                }
            }

            if (result == NULL && ::GetLastError() == ERROR_SUCCESS)
            {
                result = FindDriverModuleInRegistryValues(fileName, HKEY_LOCAL_MACHINE,
                                                          classKeyPath, kSupportModuleValueNames);
            }

            ::LocalFree(fileName);
        }

        ::LocalFree(classKeyPath);
    }

    if (result != NULL)
    {
        ::SetLastError(ERROR_SUCCESS);
        return result;
    }

    if (::GetLastError() == ERROR_SUCCESS)
    {
        ::SetLastError(ERROR_MOD_NOT_FOUND);
    }
    return NULL;
}

} /* anonymous namespace */

/* ---------------------------------------------------------------------------------- *
 * sub_180004350
 * ---------------------------------------------------------------------------------- */
wchar_t* ResolveDriverModulePath(const wchar_t* moduleName)
{
    ::SetLastError(ERROR_SUCCESS);

    if (moduleName == NULL || PathContainsSeparator(moduleName))
    {
        ::SetLastError(ERROR_BAD_ARGUMENTS);
        return NULL;
    }

    wchar_t* result = NULL;
    wchar_t* recordedPath = ResolveDriverModuleName(moduleName);

    if (recordedPath != NULL)
    {
        wchar_t* const systemDirectory = DuplicateSystemDirectory();
        if (systemDirectory != NULL)
        {
            if (!PathContainsSeparator(recordedPath))
            {
                result = JoinPath(systemDirectory, recordedPath);
            }
            else
            {
                /* The driver records system files either bare, as "system32\\x.dll" or as
                 * "\\SystemRoot\\system32\\x.dll"; rebase those onto the real System32. */
                static const wchar_t* const kSystem32Prefixes[] =
                {
                    L"system32\\",
                    L"\\SystemRoot\\system32\\"
                };

                bool rebased = false;
                for (size_t i = 0; i < sizeof(kSystem32Prefixes) / sizeof(kSystem32Prefixes[0]); ++i)
                {
                    const size_t prefixLength = wcslen(kSystem32Prefixes[i]);
                    if (_wcsnicmp(recordedPath, kSystem32Prefixes[i], prefixLength) == 0)
                    {
                        result = JoinPath(systemDirectory, recordedPath + prefixLength);
                        rebased = true;
                        break;
                    }
                }

                if (!rebased && PathIsAbsolute(recordedPath))
                {
                    /* Already absolute -- hand ownership of the buffer to the caller. */
                    result = recordedPath;
                    recordedPath = NULL;
                }
            }

            ::LocalFree(systemDirectory);
        }

        if (recordedPath != NULL)
        {
            ::LocalFree(recordedPath);
        }
    }
    else if (::GetLastError() == ERROR_MOD_NOT_FOUND)
    {
        /* The driver does not know this module: guess at the DriverStore (Windows 10
         * build 14308 and newer) or at System32. */
        wchar_t* const fileName = EnsureDllExtension(moduleName);
        if (fileName != NULL)
        {
            wchar_t* const directory = IsWindowsBuildOrGreater(kWindows10Build14308)
                                           ? GetDriverStoreDirectory(&kDisplayDeviceInterfaceGuid)
                                           : DuplicateSystemDirectory();
            if (directory != NULL)
            {
                result = JoinPath(directory, fileName);

                if (result != NULL)
                {
                    const DWORD attributes = ::GetFileAttributesW(result);
                    if (attributes == INVALID_FILE_ATTRIBUTES ||
                        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) != 0)
                    {
                        if (::GetLastError() == ERROR_FILE_NOT_FOUND)
                        {
                            ::SetLastError(ERROR_SUCCESS);
                        }
                        ::LocalFree(result);
                        result = NULL;
                    }
                }

                ::LocalFree(directory);
            }

            ::LocalFree(fileName);
        }
    }

    if (result != NULL)
    {
        ::SetLastError(ERROR_SUCCESS);
        return result;
    }

    if (::GetLastError() == ERROR_SUCCESS)
    {
        ::SetLastError(ERROR_MOD_NOT_FOUND);
    }
    return NULL;
}

} /* namespace loader */
