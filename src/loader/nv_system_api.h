/*
 * nv_system_api.h -- lazily resolved Windows API surface.
 *
 * GFSDK_Aftermath_Lib.x64.dll deliberately keeps its import table minimal: the registry,
 * service-control, SetupAPI and Shell helpers it needs are resolved with GetProcAddress
 * after loading the owning DLL *from %SystemRoot%\System32 by absolute path* (that is the
 * whole point of the hardened loader this file belongs to -- see nv_module_loader.cpp).
 *
 * Each accessor returns NULL when the library or one of its exports is unavailable, which
 * is exactly how the decompiled code behaves: every call site bails out and leaves
 * GetLastError() untouched.
 */

#ifndef GFSDK_AFTERMATH_NV_SYSTEM_API_H
#define GFSDK_AFTERMATH_NV_SYSTEM_API_H

#include <windows.h>
#include <devpropdef.h>
#include <setupapi.h>

namespace loader {

/* CSIDL values used by sub_1800025B0 (defined here so that <shlobj.h> -- and with it all
 * of OLE -- stays out of the build). */
const int kCsidlWindows         = 0x24;
const int kCsidlProgramFiles    = 0x26;
const int kCsidlProgramFilesX86 = 0x2A;

struct ShellApi
{
    HRESULT (WINAPI* SHGetFolderPathW)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath);
};

struct RegistryApi
{
    LSTATUS (WINAPI* RegOpenKeyExW)(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
    LSTATUS (WINAPI* RegQueryValueExW)(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
    LSTATUS (WINAPI* RegEnumValueW)(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
    LSTATUS (WINAPI* RegCloseKey)(HKEY hKey);
};

struct ServiceApi
{
    SC_HANDLE (WINAPI* OpenSCManagerW)(LPCWSTR lpMachineName, LPCWSTR lpDatabaseName, DWORD dwDesiredAccess);
    SC_HANDLE (WINAPI* OpenServiceW)(SC_HANDLE hSCManager, LPCWSTR lpServiceName, DWORD dwDesiredAccess);
    BOOL      (WINAPI* QueryServiceConfigW)(SC_HANDLE hService, LPQUERY_SERVICE_CONFIGW lpServiceConfig, DWORD cbBufSize, LPDWORD pcbBytesNeeded);
    BOOL      (WINAPI* CloseServiceHandle)(SC_HANDLE hSCObject);
};

struct SetupApi
{
    HDEVINFO (WINAPI* SetupDiGetClassDevsW)(const GUID* ClassGuid, PCWSTR Enumerator, HWND hwndParent, DWORD Flags);
    BOOL     (WINAPI* SetupDiEnumDeviceInterfaces)(HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData, const GUID* InterfaceClassGuid, DWORD MemberIndex, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData);
    BOOL     (WINAPI* SetupDiGetDeviceInterfaceDetailW)(HDEVINFO DeviceInfoSet, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData, PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData, DWORD DeviceInterfaceDetailDataSize, PDWORD RequiredSize, PSP_DEVINFO_DATA DeviceInfoData);
    BOOL     (WINAPI* SetupDiDestroyDeviceInfoList)(HDEVINFO DeviceInfoSet);
    BOOL     (WINAPI* SetupDiGetDeviceRegistryPropertyW)(HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData, DWORD Property, PDWORD PropertyRegDataType, PBYTE PropertyBuffer, DWORD PropertyBufferSize, PDWORD RequiredSize);
    BOOL     (WINAPI* SetupDiGetDevicePropertyW)(HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData, const DEVPROPKEY* PropertyKey, DEVPROPTYPE* PropertyType, PBYTE PropertyBuffer, DWORD PropertyBufferSize, PDWORD RequiredSize, DWORD Flags);
    BOOL     (WINAPI* SetupGetInfDriverStoreLocationW)(PCWSTR FileName, PSP_ALTPLATFORM_INFO AlternatePlatformInfo, PCWSTR LocaleName, PWSTR ReturnBuffer, DWORD ReturnBufferSize, PDWORD RequiredSize);
};

const ShellApi*    GetShellApi();
const RegistryApi* GetRegistryApi();
const ServiceApi*  GetServiceApi();
const SetupApi*    GetSetupApi();

} /* namespace loader */

#endif /* GFSDK_AFTERMATH_NV_SYSTEM_API_H */
