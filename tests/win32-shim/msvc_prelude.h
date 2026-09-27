/*
 * msvc_prelude.h -- force included (-include) by tests/syntax_check.sh.
 *
 * Neutralises the MSVC only spellings so that a non Microsoft compiler can parse the
 * sources.  Never part of the real build.
 */

#ifndef GFSDK_AFTERMATH_MSVC_PRELUDE_H
#define GFSDK_AFTERMATH_MSVC_PRELUDE_H

#if !defined(_MSC_VER)

#define __declspec(x)
#define __cdecl
#define __stdcall
#define __fastcall
#define __forceinline inline

typedef signed char        __int8;
typedef short              __int16;
typedef int                __int32;
typedef long long          __int64;

#endif /* !_MSC_VER */

#endif /* GFSDK_AFTERMATH_MSVC_PRELUDE_H */
