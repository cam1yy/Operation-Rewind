/*
 * nv_path_utils.h -- string, path and registry helpers used by the hardened driver
 *                    loader.  Reconstruction of:
 *
 *      sub_180001CD0  BuildSystemDirectoryPath
 *      sub_180001DD0  PathContainsSeparator
 *      sub_180001E30  PathIsAbsolute
 *      sub_180001E90  DuplicateSystemDirectory
 *      sub_180001F00  FileNameMatchesModule
 *      sub_1800023B0  LoadLibraryFromSystemDirectory
 *      sub_180002420  BuildKnownFolderPath
 *      sub_1800025B0  IsPathInTrustedLocation
 *      sub_180002770  DuplicateString
 *      sub_180002800  DuplicateStringN
 *      sub_1800028A0  RegQueryValueAlloc
 *      sub_180002A10  RegQueryValueAllocEx
 *      sub_180002B70  IsWindowsBuildOrGreater
 *      sub_180002F80  EnsureDllExtension
 *      sub_180003990  LoadLibraryFromTrustedPath   (sub_1800039C0)
 *      sub_180003D90  JoinPath
 *
 * Every function that returns a wchar_t* returns memory allocated with
 * LocalAlloc(LPTR, ...); release it with LocalFree().  That is what the binary does and
 * the ownership rules matter, because the results are handed across helper boundaries.
 */

#ifndef GFSDK_AFTERMATH_NV_PATH_UTILS_H
#define GFSDK_AFTERMATH_NV_PATH_UTILS_H

#include <windows.h>

namespace loader {

/* Windows 10 build 14308 -- the first build whose driver store layout the "resolve the
 * driver module by absolute path" code path depends on (sub_180004580, sub_180004350). */
const unsigned int kWindows10Build14308 = 0x37E4;

/* Windows Vista (6.0) build number, used to gate SetupDiGetDevicePropertyW. */
const unsigned int kWindowsVistaBuild6000 = 0x1770;

wchar_t* DuplicateString(const wchar_t* text);
wchar_t* DuplicateStringN(const wchar_t* text, size_t cchToCopy);

wchar_t* BuildSystemDirectoryPath(const wchar_t* fileName);
wchar_t* DuplicateSystemDirectory();
wchar_t* BuildKnownFolderPath(int csidl, const wchar_t* relativePath);
wchar_t* JoinPath(const wchar_t* left, const wchar_t* right);
wchar_t* EnsureDllExtension(const wchar_t* fileName);

bool PathContainsSeparator(const wchar_t* path);
bool PathIsAbsolute(const wchar_t* path);
bool IsPathInTrustedLocation(const wchar_t* path);
bool FileNameMatchesModule(const wchar_t* path, const wchar_t* moduleName);
bool IsWindowsBuildOrGreater(unsigned int build);

HMODULE LoadLibraryFromSystemDirectory(const wchar_t* fileName, DWORD flags);
HMODULE LoadLibraryFromTrustedPath(const wchar_t* path, DWORD flags);

/* Both return a Win32 error code (ERROR_SUCCESS on success) and hand back a
 * LocalAlloc'd copy of the value data. */
LSTATUS RegQueryValueAlloc(HKEY key, const wchar_t* valueName, DWORD* pOutType, void** ppOutData);
LSTATUS RegQueryValueAllocEx(HKEY rootKey, const wchar_t* subKey, const wchar_t* valueName, DWORD* pOutType, void** ppOutData);

} /* namespace loader */

#endif /* GFSDK_AFTERMATH_NV_PATH_UTILS_H */
