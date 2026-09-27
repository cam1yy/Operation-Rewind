// =============================================================================
//  defs.h
// -----------------------------------------------------------------------------
//  Decompiler compatibility shims.
//
//  The reconstruction in src/ is written in portable C++ and no longer contains
//  any of the artifacts below: raw byte extraction macros, the _QWORD style
//  integer aliases, the __fastcall decorations, or the FUN_/DAT_/data_ symbol
//  placeholders.  Every one of those has been replaced by a named type, a named
//  constant or a function with a real name.
//
//  This header exists for the other direction: it lets source that is still in
//  its straight-off-the-decompiler form be compiled alongside the clean code,
//  which is what makes it possible to diff a cleaned function against its
//  original listing inside one translation unit.
//
//  It is deliberately NOT included by GFSDK_Aftermath.h.  The public headers
//  must not depend on it -- they are what a client application sees.
//
//  Usage:
//      #define AFTERMATH_ALLOW_IDA_TYPES 1
//      #include "defs.h"
//
//  Without the guard, including this header is a no-op, so it cannot leak into
//  the public surface by accident.
// =============================================================================

#ifndef AFTERMATH_DEFS_H
#define AFTERMATH_DEFS_H

#include <stdint.h>

#if defined(AFTERMATH_ALLOW_IDA_TYPES)

// -----------------------------------------------------------------------------
// 1. Integer aliases
// -----------------------------------------------------------------------------
typedef int8_t    __int8;
typedef int16_t   __int16;
typedef int32_t   __int32;
typedef int64_t   __int64;

typedef uint8_t   unsigned__int8;
typedef uint16_t  unsigned__int16;
typedef uint32_t  unsigned__int32;
typedef uint64_t  unsigned__int64;

typedef uint8_t   _BYTE;
typedef uint16_t  _WORD;
typedef uint32_t  _DWORD;
typedef uint64_t  _QWORD;
typedef float     _FLOAT;
typedef double    _DOUBLE;
typedef uint8_t   _OWORD[16];

// The decompiler uses these for values whose width it could not prove.
typedef uint64_t  QWORD;
typedef uint32_t  DWORD;
typedef uint16_t  WORD;
typedef uint8_t   BYTE;

// -----------------------------------------------------------------------------
// 2. Byte and word extraction macros
//
//   In the reconstruction these are replaced by explicit shifts on a named
//   local.  For example, where the listing has
//
//       v5 = BYTE3(dwFlags);
//
//   the clean code uses
//
//       const uint32_t flagByte3 = (dwFlags >> 24) & 0xFF;
//
//   so that the field being extracted is obvious at the call site.
// -----------------------------------------------------------------------------
#define LOBYTE(x)   ((uint8_t)(((uint32_t)(x)) & 0xFF))
#define HIBYTE(x)   ((uint8_t)((((uint32_t)(x)) >> 8) & 0xFF))
#define LOWORD(x)   ((uint16_t)(((uint32_t)(x)) & 0xFFFF))
#define HIWORD(x)   ((uint16_t)((((uint32_t)(x)) >> 16) & 0xFFFF))
#define LODWORD(x)  ((uint32_t)(((uint64_t)(x)) & 0xFFFFFFFF))
#define HIDWORD(x)  ((uint32_t)((((uint64_t)(x)) >> 32) & 0xFFFFFFFF))

#define BYTE1(x)    ((uint8_t)((((uint32_t)(x)) >>  8) & 0xFF))
#define BYTE2(x)    ((uint8_t)((((uint32_t)(x)) >> 16) & 0xFF))
#define BYTE3(x)    ((uint8_t)((((uint32_t)(x)) >> 24) & 0xFF))
#define BYTE4(x)    ((uint8_t)((((uint64_t)(x)) >> 32) & 0xFF))
#define BYTE5(x)    ((uint8_t)((((uint64_t)(x)) >> 40) & 0xFF))
#define BYTE6(x)    ((uint8_t)((((uint64_t)(x)) >> 48) & 0xFF))
#define BYTE7(x)    ((uint8_t)((((uint64_t)(x)) >> 56) & 0xFF))

#define WORD1(x)    ((uint16_t)((((uint32_t)(x)) >> 16) & 0xFFFF))
#define WORD2(x)    ((uint16_t)((((uint64_t)(x)) >> 32) & 0xFFFF))
#define WORD3(x)    ((uint16_t)((((uint64_t)(x)) >> 48) & 0xFFFF))

#define DWORD1(x)   ((uint32_t)((((uint64_t)(x)) >> 32) & 0xFFFFFFFF))

// The extraction helpers the decompiler emits for sign extended loads.
#define SBYTEn(x, n)   ((int8_t)(((uint64_t)(x)) >> ((n) * 8)))
#define SWORDn(x, n)   ((int16_t)(((uint64_t)(x)) >> ((n) * 16)))

// -----------------------------------------------------------------------------
// 3. Calling convention decorations
//
//   x64 has exactly one calling convention, so under MSVC these are all
//   accepted (and ignored) and under GCC/Clang they are not keywords at all.
//   Mapping them away keeps decompiler output compilable on either toolchain.
// -----------------------------------------------------------------------------
#if !defined(_MSC_VER)
#  ifndef __fastcall
#    define __fastcall
#  endif
#  ifndef __cdecl
#    define __cdecl
#  endif
#  ifndef __stdcall
#    define __stdcall
#  endif
#  ifndef __declspec
#    define __declspec(x)
#  endif
#  ifndef __forceinline
#    define __forceinline inline
#  endif
#endif

// -----------------------------------------------------------------------------
// 4. Non-standard scalar types used by Microsoft headers
// -----------------------------------------------------------------------------
#if !defined(_MSC_VER)
typedef int                 BOOL;
typedef unsigned char       BYTE_IMPORTED;
typedef unsigned long       DWORD_IMPORTED;
typedef void*               LPVOID;
typedef const char*         LPCSTR;
typedef char*               LPSTR;
#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
#ifndef NULL
#  define NULL 0
#endif
#endif

// -----------------------------------------------------------------------------
// 5. Symbol placeholder conventions
//
//   The decompiler renders unknown globals as DAT_<addr>, unknown functions as
//   FUN_<addr> and (in the Ghidra flavoured listings) globals as data_<addr>.
//   None of those survive into the reconstruction: every one of them was given
//   a name that says what the value is.  The mapping used is recorded below so
//   that the cleaned source can still be read side by side with the listing.
//
//     DAT_18002029C / data_18002029c  ->  aftermath::g_oneTimeInitState
//     DAT_1800202A0 / data_1800202a0  ->  aftermath::g_pDevice
//     DAT_1800202A8 / data_1800202a8  ->  aftermath::g_featureFlags
//     DAT_1800202AC / data_1800202ac  ->  aftermath::g_initialized
//
//     FUN_1800012d0 (D3D11 set event marker)  ->  DriverSetEventMarker(Api_D3D11, ...)
//     FUN_180001570 (D3D12 set event marker)  ->  DriverSetEventMarker(Api_D3D12, ...)
//     FUN_1800013b0 (D3D11 get data)          ->  DriverGetData(Api_D3D11, ...)
//     FUN_180001650 (D3D12 get data)          ->  DriverGetData(Api_D3D12, ...)
//     FUN_1800014a0 (D3D11 device status)     ->  DriverGetDeviceStatus(Api_D3D11, ...)
//     FUN_180001740 (D3D12 device status)     ->  DriverGetDeviceStatus(Api_D3D12, ...)
//
//   The two D3D11/D3D12 thunks that shared a body in the listing were folded
//   into a single function that takes the aftermath::Api selector, which is
//   what the exported wrappers in the original were already doing by hand.
// -----------------------------------------------------------------------------

#endif // AFTERMATH_ALLOW_IDA_TYPES

#endif // AFTERMATH_DEFS_H
