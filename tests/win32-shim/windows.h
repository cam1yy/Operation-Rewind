/*
 * windows.h -- stub Win32 surface used only by tests/syntax_check.sh.
 *
 * It declares exactly the types, constants and functions the reconstructed sources touch,
 * with the signatures documented on learn.microsoft.com.  Its only job is to let a non
 * Windows compiler type check the translation units; it is NOT a Win32 implementation and
 * is never part of the Visual Studio / CMake build.
 */

#ifndef GFSDK_AFTERMATH_SHIM_WINDOWS_H
#define GFSDK_AFTERMATH_SHIM_WINDOWS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/* ---- calling conventions --------------------------------------------------------- */
#define WINAPI
#define APIENTRY
#define CALLBACK
#define STDMETHODCALLTYPE
#define WINBASEAPI
#define WINADVAPI
#define ANYSIZE_ARRAY 1
#define UNREFERENCED_PARAMETER(x) ((void)(x))

/* ---- base types ------------------------------------------------------------------ */
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef int                 INT;
typedef unsigned int        UINT;
typedef long                LONG;
typedef unsigned long       ULONG;
typedef long long           LONGLONG;
typedef unsigned long long  ULONGLONG;
typedef unsigned long long  DWORDLONG;
typedef char                CHAR;
typedef wchar_t             WCHAR;
typedef int32_t             INT32;
typedef int64_t             INT64;
typedef intptr_t            INT_PTR;
typedef uintptr_t           UINT_PTR;
typedef intptr_t            LONG_PTR;
typedef uintptr_t           ULONG_PTR;
typedef uintptr_t           DWORD_PTR;
typedef size_t              SIZE_T;
typedef void                VOID;
typedef void*               PVOID;
typedef void*               LPVOID;
typedef const void*         LPCVOID;
typedef char*               LPSTR;
typedef const char*         LPCSTR;
typedef WCHAR*              LPWSTR;
typedef const WCHAR*        LPCWSTR;
typedef WCHAR*              PWSTR;
typedef const WCHAR*        PCWSTR;
typedef BYTE*               LPBYTE;
typedef BYTE*               PBYTE;
typedef DWORD*              LPDWORD;
typedef DWORD*              PDWORD;
typedef BOOL*               LPBOOL;
typedef LONG                HRESULT;
typedef LONG                LSTATUS;
typedef DWORD               ACCESS_MASK;
typedef ACCESS_MASK         REGSAM;

typedef void*               HANDLE;
typedef void*               HMODULE;
typedef void*               HINSTANCE;
typedef void*               HWND;
typedef void*               HLOCAL;
typedef void*               HDEVINFO;
typedef void*               SC_HANDLE;
struct HKEY__ { int unused; };
typedef struct HKEY__*      HKEY;
typedef HKEY*               PHKEY;

#define TRUE  1
#define FALSE 0
#ifndef NULL
#   define NULL 0
#endif
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

typedef struct _GUID
{
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
#ifdef __cplusplus
typedef const IID&  REFIID;
typedef const GUID& REFGUID;
#else
typedef const IID*  REFIID;
#endif

/* ---- HRESULT helpers -------------------------------------------------------------- */
#define S_OK            ((HRESULT)0L)
#define S_FALSE         ((HRESULT)1L)
#define E_NOINTERFACE   ((HRESULT)0x80004002L)
#define SUCCEEDED(hr)   (((HRESULT)(hr)) >= 0)
#define FAILED(hr)      (((HRESULT)(hr)) < 0)

/* ---- error codes ------------------------------------------------------------------ */
#define ERROR_SUCCESS               0L
#define ERROR_FILE_NOT_FOUND        2L
#define ERROR_INVALID_DATA          13L
#define ERROR_NO_MORE_ITEMS         259L
#define ERROR_MOD_NOT_FOUND         126L
#define ERROR_PROC_NOT_FOUND        127L
#define ERROR_CALL_NOT_IMPLEMENTED  120L
#define ERROR_BAD_ARGUMENTS         160L
#define ERROR_BAD_PATHNAME          161L
#define ERROR_INSUFFICIENT_BUFFER   122L

/* ---- LocalAlloc ------------------------------------------------------------------- */
#define LMEM_FIXED    0x0000
#define LMEM_ZEROINIT 0x0040
#define LPTR          (LMEM_FIXED | LMEM_ZEROINIT)

extern "C" {

HLOCAL  WINAPI LocalAlloc(UINT uFlags, SIZE_T uBytes);
HLOCAL  WINAPI LocalFree(HLOCAL hMem);

/* ---- module / library ------------------------------------------------------------- */
HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
BOOL    WINAPI FreeLibrary(HMODULE hLibModule);
typedef INT_PTR (WINAPI* FARPROC)();
FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName);

#define LOAD_WITH_ALTERED_SEARCH_PATH       0x00000008
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR    0x00000100
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#define LOAD_LIBRARY_SEARCH_USER_DIRS       0x00000400
#define LOAD_LIBRARY_SEARCH_SYSTEM32        0x00000800
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS    0x00001000

/* ---- misc kernel32 ---------------------------------------------------------------- */
UINT  WINAPI GetSystemDirectoryW(LPWSTR lpBuffer, UINT uSize);
DWORD WINAPI GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart);
DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName);
DWORD WINAPI GetLastError(void);
void  WINAPI SetLastError(DWORD dwErrCode);
void  WINAPI Sleep(DWORD dwMilliseconds);

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH  2
#define DLL_THREAD_DETACH  3

#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_DEVICE    0x00000040
#define GENERIC_READ             0x80000000

/* ---- interlocked ------------------------------------------------------------------ */
LONG WINAPI InterlockedIncrement(LONG volatile* Addend);
LONG WINAPI InterlockedDecrement(LONG volatile* Addend);
LONG WINAPI InterlockedCompareExchange(LONG volatile* Destination, LONG Exchange, LONG Comparand);

/* ---- version ---------------------------------------------------------------------- */
typedef struct _OSVERSIONINFOEXW
{
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    WCHAR szCSDVersion[128];
    WORD  wServicePackMajor;
    WORD  wServicePackMinor;
    WORD  wSuiteMask;
    BYTE  wProductType;
    BYTE  wReserved;
} OSVERSIONINFOEXW, *POSVERSIONINFOEXW, *LPOSVERSIONINFOEXW;

#define VER_MINORVERSION     0x0000001
#define VER_MAJORVERSION     0x0000002
#define VER_BUILDNUMBER      0x0000004
#define VER_SERVICEPACKMAJOR 0x0000020
#define VER_EQUAL            1
#define VER_GREATER          2
#define VER_GREATER_EQUAL    3

ULONGLONG WINAPI VerSetConditionMask(ULONGLONG ConditionMask, DWORD TypeMask, BYTE Condition);
BOOL      WINAPI VerifyVersionInfoW(LPOSVERSIONINFOEXW lpVersionInformation, DWORD dwTypeMask, DWORDLONG dwlConditionMask);

/* ---- registry --------------------------------------------------------------------- */
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002u)
#define REG_NONE      0
#define REG_SZ        1
#define REG_EXPAND_SZ 2
#define REG_BINARY    3
#define REG_DWORD     4
#define REG_MULTI_SZ  7
#define KEY_QUERY_VALUE 0x0001
#define KEY_READ        0x20019

/* ---- services --------------------------------------------------------------------- */
#define SERVICE_KERNEL_DRIVER 0x00000001

typedef struct _QUERY_SERVICE_CONFIGW
{
    DWORD  dwServiceType;
    DWORD  dwStartType;
    DWORD  dwErrorControl;
    LPWSTR lpBinaryPathName;
    LPWSTR lpLoadOrderGroup;
    DWORD  dwTagId;
    LPWSTR lpDependencies;
    LPWSTR lpServiceStartName;
    LPWSTR lpDisplayName;
} QUERY_SERVICE_CONFIGW, *LPQUERY_SERVICE_CONFIGW;

} /* extern "C" */

/* ---- ZeroMemory ------------------------------------------------------------------- */
#define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define CopyMemory(Destination, Source, Length) memcpy((Destination), (Source), (Length))

/* ---- MSVC CRT spellings ----------------------------------------------------------- */
#if !defined(_MSC_VER)
static inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
static inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }
static inline wchar_t* _wcsupr(wchar_t* s)
{
    for (wchar_t* p = s; *p != L'\0'; ++p) { *p = (wchar_t)towupper(*p); }
    return s;
}
#endif

#endif /* GFSDK_AFTERMATH_SHIM_WINDOWS_H */
