/*
 * nv_driver_store.h -- "where does the NVIDIA display driver keep <module>.dll?"
 *
 * Reconstruction of sub_180002C60 .. sub_180004580's helper tree:
 *
 *      sub_180002C60  FindNvidiaDisplayDevice
 *      sub_1800030B0  FindServiceBinaryPath
 *      sub_1800032E0  FindDriverModuleViaService
 *      sub_1800035A0  FindDriverModuleInRegistryValues
 *      sub_1800037B0  FindDriverModuleByEnumeratingValues
 *      sub_180003A60  GetDriverStoreDirectory
 *      sub_180003EA0  GetDisplayDeviceDriverKeyPath
 *      sub_1800040D0  ResolveDriverModuleName
 *      sub_180004350  ResolveDriverModulePath
 *
 * Only the last one is used outside this translation unit.
 */

#ifndef GFSDK_AFTERMATH_NV_DRIVER_STORE_H
#define GFSDK_AFTERMATH_NV_DRIVER_STORE_H

#include <windows.h>

namespace loader {

/*
 * Returns the absolute path of <moduleName> as recorded by the installed NVIDIA display
 * driver, or NULL.  The result is LocalAlloc'd; free it with LocalFree().
 *
 * On failure GetLastError() is ERROR_MOD_NOT_FOUND when the driver simply does not know
 * the module, and something more specific when a lookup failed outright.
 */
wchar_t* ResolveDriverModulePath(const wchar_t* moduleName);

} /* namespace loader */

#endif /* GFSDK_AFTERMATH_NV_DRIVER_STORE_H */
