// =============================================================================
//  tests/windows_shim/windows.h
// -----------------------------------------------------------------------------
//  A deliberately tiny stand-in for <windows.h>, used ONLY by the `wincheck`
//  target in Makefile.host.
//
//  Why it exists: the host smoke test compiles with -DAFTERMATH_TEST_BUILD,
//  which removes the loader and the DllMain from the build.  That means the
//  Windows-only code -- everything inside `#ifndef AFTERMATH_TEST_BUILD`, i.e.
//  the LoadLibrary / GetProcAddress paths -- is never type-checked on a Linux
//  box, which is exactly the code MSVC will compile.
//
//  `make wincheck` compiles the same translation units WITHOUT that define,
//  against this header, so a typo in the Windows branch fails the build here
//  rather than in Visual Studio.
//
//  It declares only the handful of symbols the reconstruction actually touches.
//  It is not a substitute for the real header and must never be used to build
//  the DLL.
// =============================================================================

#ifndef AFTERMATH_TEST_WINDOWS_SHIM_H
#define AFTERMATH_TEST_WINDOWS_SHIM_H

#include <stdint.h>
#include <stddef.h>

typedef int             BOOL;
typedef void*           HMODULE;
typedef void*           HINSTANCE;
typedef void*           LPVOID;
typedef unsigned long   DWORD;

#define WINAPI
#define TRUE  1
#define FALSE 0

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH  2
#define DLL_THREAD_DETACH  3

#ifdef __cplusplus
extern "C" {
#endif

HMODULE GetModuleHandleA(const char* lpModuleName);
HMODULE LoadLibraryA(const char* lpLibFileName);
void*   GetProcAddress(HMODULE hModule, const char* lpProcName);
BOOL    FreeLibrary(HMODULE hLibModule);
void    Sleep(DWORD dwMilliseconds);
BOOL    DisableThreadLibraryCalls(HMODULE hLibModule);

#ifdef __cplusplus
}
#endif

#endif // AFTERMATH_TEST_WINDOWS_SHIM_H
