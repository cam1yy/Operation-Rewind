/*
 * nv_module_loader.cpp -- reconstruction of sub_180004580.
 */

#include "loader/nv_module_loader.h"

#include <string.h>
#include <wchar.h>

#include "loader/nv_driver_store.h"
#include "loader/nv_path_utils.h"

namespace loader {

HMODULE NvLoadLibrary(const wchar_t* moduleName, DWORD flags)
{
    ::SetLastError(ERROR_SUCCESS);

    if (PathContainsSeparator(moduleName))
    {
        ::SetLastError(ERROR_BAD_ARGUMENTS);
        return NULL;
    }

    /*
     * Deviation: the binary reaches the _wcsnicmp below even when moduleName is NULL
     * (the NULL test only skips the separator scan).  Nothing inside the DLL ever passes
     * NULL, so the reconstruction guards it instead of reproducing the dereference.
     */
    static const wchar_t kNvidiaPrefix[] = L"nv";

    if (moduleName != NULL &&
        IsWindowsBuildOrGreater(kWindows10Build14308) &&
        _wcsnicmp(moduleName, kNvidiaPrefix, sizeof(kNvidiaPrefix) / sizeof(wchar_t) - 1) == 0)
    {
        wchar_t* const fullPath = ResolveDriverModulePath(moduleName);
        const DWORD resolveError = ::GetLastError();

        if (fullPath != NULL)
        {
            const HMODULE module = LoadLibraryFromTrustedPath(fullPath, flags);
            ::LocalFree(fullPath);
            return module;
        }

        /* Anything other than "the driver has no record of it" is fatal: do not silently
         * fall back to a System32 load in that case. */
        if (resolveError != ERROR_MOD_NOT_FOUND)
        {
            return NULL;
        }
    }

    return LoadLibraryFromSystemDirectory(moduleName, flags);
}

} /* namespace loader */
