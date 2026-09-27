/*
 * nv_path_utils.cpp -- reconstruction of the string/path/registry helpers listed in
 *                      nv_path_utils.h.
 *
 * Clean-up notes (full list in docs/RECONSTRUCTION.md):
 *   - Hex-Rays renders every wcslen() as an inline "do { ... } while (v);" loop with a
 *     negated counter (v = -len - 2).  Those are written back as wcslen().
 *   - The CRT's isalpha()/isdigit() are called with raw UTF-16 code units by the original
 *     code, which is undefined for values > 255 and asserts in debug CRTs.  The
 *     reconstruction uses explicit ASCII tests, which is what the call sites mean.
 *   - Buffers are allocated with LocalAlloc(LPTR, ...) exactly as in the binary, so the
 *     implicit zero initialisation that the original relies on for NUL termination is
 *     preserved (the reconstruction terminates explicitly as well).
 */

#include "loader/nv_path_utils.h"

#include <string.h>
#include <wchar.h>

#include "loader/nv_strsafe.h"
#include "loader/nv_system_api.h"

namespace loader {
namespace {

inline bool IsAsciiAlpha(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

inline bool IsAsciiDigit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

/*
 * Inlined in several places in the binary: scan backwards from the terminator and return
 * the '.' that starts the extension, or the terminator itself when there is none (a '\\'
 * stops the scan, so "c:\\dir.x\\file" has no extension).
 */
const wchar_t* FindExtension(const wchar_t* text)
{
    const wchar_t* const end = text + wcslen(text);
    const wchar_t* cursor = end;
    while (cursor > text && *cursor != L'\\' && *cursor != L'.')
    {
        --cursor;
    }
    return (*cursor == L'.') ? cursor : end;
}

const wchar_t* FindFileNamePart(const wchar_t* path)
{
    const wchar_t* const lastSeparator = wcsrchr(path, L'\\');
    return (lastSeparator != NULL) ? (lastSeparator + 1) : path;
}

/* Cached results of the two OS version probes. */
int g_windowsBuildNumber = 0;          /* 0x18001F10C */
int g_isWindows7OrGreater = -1;        /* 0x18001B000 holds the inverse of this */

} /* anonymous namespace */

/* ---------------------------------------------------------------------------------- *
 * sub_180002770
 * ---------------------------------------------------------------------------------- */
wchar_t* DuplicateString(const wchar_t* text)
{
    if (text == NULL)
    {
        return NULL;
    }

    const size_t length = wcslen(text);
    wchar_t* const copy = static_cast<wchar_t*>(::LocalAlloc(LPTR, (length + 1) * sizeof(wchar_t)));
    if (copy != NULL)
    {
        ::memcpy(copy, text, length * sizeof(wchar_t));
        copy[length] = L'\0';
    }
    return copy;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180002800
 * ---------------------------------------------------------------------------------- */
wchar_t* DuplicateStringN(const wchar_t* text, size_t cchToCopy)
{
    if (text == NULL)
    {
        return NULL;
    }

    wchar_t* copy = static_cast<wchar_t*>(::LocalAlloc(LPTR, (cchToCopy + 1) * sizeof(wchar_t)));
    if (copy != NULL)
    {
        if (FAILED(StringCchCopyNW(copy, cchToCopy + 1, text, cchToCopy)))
        {
            ::LocalFree(copy);
            copy = NULL;
        }
    }
    return copy;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180001CD0 -- "%SystemRoot%\System32\<fileName>"
 * ---------------------------------------------------------------------------------- */
wchar_t* BuildSystemDirectoryPath(const wchar_t* fileName)
{
    if (fileName == NULL)
    {
        return NULL;
    }

    /* GetSystemDirectoryW(NULL, 0) yields the required size *including* the terminator. */
    const UINT systemDirectorySize = ::GetSystemDirectoryW(NULL, 0);
    const size_t fileNameLength = wcslen(fileName);

    wchar_t* const path = static_cast<wchar_t*>(
        ::LocalAlloc(LPTR, (systemDirectorySize + 1 + fileNameLength) * sizeof(wchar_t)));
    if (path == NULL)
    {
        return NULL;
    }

    UINT written = ::GetSystemDirectoryW(path, systemDirectorySize);
    if (written == 0)
    {
        ::LocalFree(path);
        return NULL;
    }

    if (path[written - 1] != L'\\')
    {
        path[written++] = L'\\';
    }

    ::memcpy(path + written, fileName, fileNameLength * sizeof(wchar_t));
    path[written + fileNameLength] = L'\0';
    return path;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180001E90
 * ---------------------------------------------------------------------------------- */
wchar_t* DuplicateSystemDirectory()
{
    const UINT systemDirectorySize = ::GetSystemDirectoryW(NULL, 0);
    if (systemDirectorySize == 0)
    {
        return NULL;
    }

    wchar_t* directory = static_cast<wchar_t*>(::LocalAlloc(LPTR, systemDirectorySize * sizeof(wchar_t)));
    if (directory != NULL)
    {
        if (::GetSystemDirectoryW(directory, systemDirectorySize) == 0)
        {
            ::LocalFree(directory);
            directory = NULL;
        }
    }
    return directory;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180001DD0
 * ---------------------------------------------------------------------------------- */
bool PathContainsSeparator(const wchar_t* path)
{
    if (path == NULL)
    {
        return false;
    }

    for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor)
    {
        if (*cursor == L'\\' || *cursor == L'/')
        {
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180001E30 -- "<drive letter>:\..." is the only shape treated as absolute; UNC
 * paths are deliberately rejected so that LoadLibraryFromTrustedPath can never reach out
 * to a remote share.
 * ---------------------------------------------------------------------------------- */
bool PathIsAbsolute(const wchar_t* path)
{
    if (path == NULL)
    {
        return false;
    }

    return IsAsciiAlpha(path[0]) &&
           path[1] == L':' &&
           (path[2] == L'\\' || path[2] == L'/');
}

/* ---------------------------------------------------------------------------------- *
 * sub_180001F00 -- does <path>'s file name denote the module <moduleName>?
 *
 * "nvapi64" matches "C:\\windows\\system32\\NVAPI64.DLL" but not "nvapi64x.dll"; an empty
 * extension and ".dll" are interchangeable on both sides.
 * ---------------------------------------------------------------------------------- */
bool FileNameMatchesModule(const wchar_t* path, const wchar_t* moduleName)
{
    const wchar_t* const fileName = FindFileNamePart(path);

    if (_wcsicmp(fileName, moduleName) == 0)
    {
        return true;
    }

    const wchar_t* const fileNameExtension = FindExtension(fileName);
    const wchar_t* const moduleExtension   = FindExtension(moduleName);

    /* Both stems must have the same length ... */
    if ((fileNameExtension - fileName) != (moduleExtension - moduleName))
    {
        return false;
    }

    /* ... both extensions must be either absent or ".dll" ... */
    if (*moduleExtension != L'\0' && _wcsicmp(moduleExtension, L".dll") != 0)
    {
        return false;
    }
    if (*fileNameExtension != L'\0' && _wcsicmp(fileNameExtension, L".dll") != 0)
    {
        return false;
    }

    /* ... and the stems must match case insensitively. */
    return _wcsnicmp(fileName, moduleName, static_cast<size_t>(moduleExtension - moduleName)) == 0;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800023B0
 * ---------------------------------------------------------------------------------- */
HMODULE LoadLibraryFromSystemDirectory(const wchar_t* fileName, DWORD flags)
{
    wchar_t* const path = BuildSystemDirectoryPath(fileName);
    if (path == NULL)
    {
        return NULL;
    }

    const HMODULE module = ::LoadLibraryExW(path, NULL, flags);
    ::LocalFree(path);
    return module;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180002420 -- "<CSIDL folder>\<relativePath>"
 * ---------------------------------------------------------------------------------- */
wchar_t* BuildKnownFolderPath(int csidl, const wchar_t* relativePath)
{
    const ShellApi* const shell = GetShellApi();
    if (shell == NULL)
    {
        return NULL;
    }

    wchar_t folder[MAX_PATH];
    folder[0] = L'\0';

    /* dwFlags == 0 == SHGFP_TYPE_CURRENT */
    if (FAILED(shell->SHGetFolderPathW(NULL, csidl, NULL, 0, folder)))
    {
        return NULL;
    }

    /* folder + '\\' + relativePath + '\0' */
    const size_t cchBuffer = wcslen(folder) + 1 + wcslen(relativePath) + 1;
    wchar_t* const path = static_cast<wchar_t*>(::LocalAlloc(LPTR, cchBuffer * sizeof(wchar_t)));
    if (path == NULL)
    {
        return NULL;
    }

    if (FAILED(StringCchCopyW(path, cchBuffer, folder)) ||
        FAILED(StringCchCatW(path, cchBuffer, L"\\")) ||
        FAILED(StringCchCatW(path, cchBuffer, relativePath)))
    {
        ::LocalFree(path);
        return NULL;
    }

    return path;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800025B0 -- is <path> below %WINDIR%, %ProgramFiles% or %ProgramFiles(x86)%?
 *
 * Before Windows 7 the answer is always "yes": the KB2533623 hardening this check
 * belongs to does not exist there, so the loader falls back to the plain System32 path.
 * ---------------------------------------------------------------------------------- */
bool IsPathInTrustedLocation(const wchar_t* path)
{
    if (g_isWindows7OrGreater < 0)
    {
        OSVERSIONINFOEXW versionInfo;
        ::ZeroMemory(&versionInfo, sizeof(versionInfo));
        versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
        versionInfo.dwMajorVersion = 6;
        versionInfo.dwMinorVersion = 1;
        versionInfo.wServicePackMajor = 0;

        ULONGLONG conditionMask = 0;
        conditionMask = ::VerSetConditionMask(conditionMask, VER_MINORVERSION,     VER_GREATER_EQUAL);
        conditionMask = ::VerSetConditionMask(conditionMask, VER_MAJORVERSION,     VER_GREATER_EQUAL);
        conditionMask = ::VerSetConditionMask(conditionMask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

        g_isWindows7OrGreater = ::VerifyVersionInfoW(&versionInfo,
                                                     VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR,
                                                     conditionMask) ? 1 : 0;
    }

    if (g_isWindows7OrGreater == 0)
    {
        return true;
    }

    wchar_t fullPath[MAX_PATH];
    fullPath[0] = L'\0';
    if (::GetFullPathNameW(path, MAX_PATH, fullPath, NULL) == 0)
    {
        return false;
    }

    static const int kTrustedFolders[] = { kCsidlWindows, kCsidlProgramFiles, kCsidlProgramFilesX86 };

    for (size_t i = 0; i < sizeof(kTrustedFolders) / sizeof(kTrustedFolders[0]); ++i)
    {
        wchar_t* const trustedFolder = BuildKnownFolderPath(kTrustedFolders[i], L"");
        if (trustedFolder == NULL)
        {
            continue;
        }

        const bool isTrusted = _wcsnicmp(fullPath, trustedFolder, wcslen(trustedFolder)) == 0;
        ::LocalFree(trustedFolder);

        if (isTrusted)
        {
            return true;
        }
    }

    return false;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800028A0
 * ---------------------------------------------------------------------------------- */
LSTATUS RegQueryValueAlloc(HKEY key, const wchar_t* valueName, DWORD* pOutType, void** ppOutData)
{
    const RegistryApi* const registry = GetRegistryApi();
    if (registry == NULL)
    {
        return ERROR_PROC_NOT_FOUND;
    }

    DWORD type = REG_NONE;
    DWORD cbData = 0;

    LSTATUS status = registry->RegQueryValueExW(key, valueName, NULL, &type, NULL, &cbData);
    if (status != ERROR_SUCCESS || cbData == 0)
    {
        return status;
    }

    /* Registry strings are not guaranteed to be terminated; over-allocate so that the
     * caller can always treat the result as a (multi) string. */
    size_t extraBytes = 0;
    if (type == REG_SZ || type == REG_EXPAND_SZ)
    {
        extraBytes = sizeof(wchar_t);
    }
    else if (type == REG_MULTI_SZ)
    {
        extraBytes = 2 * sizeof(wchar_t);
    }

    *ppOutData = ::LocalAlloc(LPTR, extraBytes + cbData);
    if (*ppOutData == NULL)
    {
        return static_cast<LSTATUS>(::GetLastError());
    }

    status = registry->RegQueryValueExW(key, valueName, NULL, pOutType,
                                        static_cast<LPBYTE>(*ppOutData), &cbData);
    if (status != ERROR_SUCCESS)
    {
        ::LocalFree(*ppOutData);
        *ppOutData = NULL;
    }
    return status;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180002A10
 * ---------------------------------------------------------------------------------- */
LSTATUS RegQueryValueAllocEx(HKEY rootKey, const wchar_t* subKey, const wchar_t* valueName,
                             DWORD* pOutType, void** ppOutData)
{
    const RegistryApi* const registry = GetRegistryApi();
    if (registry == NULL)
    {
        return ERROR_PROC_NOT_FOUND;
    }

    HKEY key = NULL;
    LSTATUS status = registry->RegOpenKeyExW(rootKey, subKey, 0, KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS)
    {
        return status;
    }

    status = RegQueryValueAlloc(key, valueName, pOutType, ppOutData);
    registry->RegCloseKey(key);
    return status;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180002B70 -- HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\CurrentBuildNumber
 * ---------------------------------------------------------------------------------- */
bool IsWindowsBuildOrGreater(unsigned int build)
{
    if (g_windowsBuildNumber == 0)
    {
        DWORD type = REG_NONE;
        void* data = NULL;

        const LSTATUS status = RegQueryValueAllocEx(HKEY_LOCAL_MACHINE,
                                                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                                    L"CurrentBuildNumber",
                                                    &type,
                                                    &data);
        if (status == ERROR_SUCCESS && type == REG_SZ && data != NULL)
        {
            const wchar_t* cursor = static_cast<const wchar_t*>(data);
            const wchar_t* const end = cursor + wcslen(cursor);

            while (cursor < end)
            {
                if (!IsAsciiDigit(*cursor))
                {
                    g_windowsBuildNumber = 0;   /* not a plain number -- give up */
                    break;
                }
                g_windowsBuildNumber = g_windowsBuildNumber * 10 + (*cursor - L'0');
                ++cursor;
            }
        }

        if (data != NULL)
        {
            ::LocalFree(data);
        }
    }

    return static_cast<unsigned int>(g_windowsBuildNumber) >= build;
}

/* ---------------------------------------------------------------------------------- *
 * sub_180002F80 -- "nvapi64" -> "nvapi64.dll", "nvoglv64." -> "nvoglv64",
 *                  "nvapi64.dll" -> unchanged.
 * ---------------------------------------------------------------------------------- */
wchar_t* EnsureDllExtension(const wchar_t* fileName)
{
    if (fileName == NULL)
    {
        return NULL;
    }

    const size_t length = wcslen(fileName);
    const size_t extensionLength = wcslen(FindExtension(fileName));

    if (extensionLength == 1)
    {
        /* Ends with a bare '.': strip it. */
        return DuplicateStringN(fileName, length - 1);
    }

    if (extensionLength != 0)
    {
        return DuplicateString(fileName);
    }

    static const wchar_t kDllExtension[] = L".dll";
    const size_t cchBuffer = length + (sizeof(kDllExtension) / sizeof(wchar_t) - 1) + 1;

    wchar_t* const result = static_cast<wchar_t*>(::LocalAlloc(LPTR, cchBuffer * sizeof(wchar_t)));
    if (result == NULL)
    {
        return NULL;
    }

    if (FAILED(StringCbCopyW(result, cchBuffer * sizeof(wchar_t), fileName)) ||
        FAILED(StringCbCatW(result, cchBuffer * sizeof(wchar_t), kDllExtension)))
    {
        ::LocalFree(result);
        return NULL;
    }

    return result;
}

/* ---------------------------------------------------------------------------------- *
 * sub_1800039C0 -- LoadLibraryEx, but only for absolute paths inside a trusted folder,
 * and with every "search path" flag masked off.
 * ---------------------------------------------------------------------------------- */
HMODULE LoadLibraryFromTrustedPath(const wchar_t* path, DWORD flags)
{
    /* LOAD_WITH_ALTERED_SEARCH_PATH plus all LOAD_LIBRARY_SEARCH_* bits. */
    const DWORD kForbiddenFlags = 0x00001F08;

    ::SetLastError(ERROR_SUCCESS);

    if (path == NULL || !PathIsAbsolute(path))
    {
        ::SetLastError(ERROR_BAD_ARGUMENTS);
        return NULL;
    }

    if (!IsPathInTrustedLocation(path))
    {
        ::SetLastError(ERROR_BAD_PATHNAME);
        return NULL;
    }

    return ::LoadLibraryExW(path, NULL, flags & ~kForbiddenFlags);
}

/* ---------------------------------------------------------------------------------- *
 * sub_180003D90
 * ---------------------------------------------------------------------------------- */
wchar_t* JoinPath(const wchar_t* left, const wchar_t* right)
{
    if (left == NULL)
    {
        return DuplicateString(right);
    }
    if (right == NULL)
    {
        return DuplicateString(left);
    }

    const size_t leftLength  = wcslen(left);
    const size_t rightLength = wcslen(right);

    const bool needsSeparator = leftLength != 0 && rightLength != 0 &&
                                left[leftLength - 1] != L'\\' && right[0] != L'\\';

    const size_t cbBuffer = (leftLength + rightLength + (needsSeparator ? 1 : 0) + 1) * sizeof(wchar_t);

    wchar_t* const path = static_cast<wchar_t*>(::LocalAlloc(LPTR, cbBuffer));
    if (path == NULL)
    {
        return NULL;
    }

    if (FAILED(StringCbCopyW(path, cbBuffer, left)) ||
        (needsSeparator && FAILED(StringCbCatW(path, cbBuffer, L"\\"))) ||
        FAILED(StringCbCatW(path, cbBuffer, right)))
    {
        ::LocalFree(path);
        return NULL;
    }

    return path;
}

} /* namespace loader */
