/*
 * nv_module_loader.h -- reconstruction of sub_180004580, the entry point of NVIDIA's
 *                       hardened "load a driver library" helper.
 */

#ifndef GFSDK_AFTERMATH_NV_MODULE_LOADER_H
#define GFSDK_AFTERMATH_NV_MODULE_LOADER_H

#include <windows.h>

namespace loader {

/*
 * Loads <moduleName> (a bare file name -- paths are rejected) the way the NVIDIA driver
 * stack expects:
 *
 *   - on Windows 10 build 14308 and newer, modules whose name starts with "nv" are
 *     resolved to an absolute path through the driver's own registry/DriverStore records
 *     and loaded only if that path sits in a trusted location;
 *   - everything else (and every failure that means "the driver does not know this
 *     module") falls back to an explicit %SystemRoot%\System32\<moduleName> load.
 *
 * Never uses the process search path, which is the point of the exercise.
 */
HMODULE NvLoadLibrary(const wchar_t* moduleName, DWORD flags);

} /* namespace loader */

#endif /* GFSDK_AFTERMATH_NV_MODULE_LOADER_H */
