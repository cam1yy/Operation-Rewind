/*
 * ida_defs.h -- MSVC friendly replacements for the types and macros that Hex-Rays
 *               emits (normally supplied by IDA's <defs.h>).
 *
 * The reconstructed sources in this repository do NOT use any of these: every
 * decompiler artefact has been rewritten in terms of standard C++ / Win32.  This
 * header exists so that *raw* Hex-Rays listings can be dropped into the project
 * during further reverse engineering and still compile with MSVC v143 (x64) --
 * which is exactly how the clean-up below was carried out function by function.
 *
 * Usage:   #include "compat/ida_defs.h"   before pasting a raw listing.
 */

#ifndef GFSDK_AFTERMATH_IDA_DEFS_H
#define GFSDK_AFTERMATH_IDA_DEFS_H

#include <stddef.h>
#include <stdint.h>

#if !defined(_MSC_VER)
typedef int8_t   __int8;
typedef int16_t  __int16;
typedef int32_t  __int32;
typedef int64_t  __int64;
#endif

/* ------------------------------------------------------------------ *
 *  Fixed width aliases used by Hex-Rays pseudo code
 * ------------------------------------------------------------------ */
typedef unsigned char       _BYTE;
typedef unsigned short      _WORD;
typedef unsigned int        _DWORD;
typedef unsigned __int64    _QWORD;
typedef char                _BOOL1;
typedef short               _BOOL2;
typedef int                 _BOOL4;

typedef unsigned char       uchar;
typedef unsigned short      ushort;
typedef unsigned int        uint;
typedef unsigned long       ulong;

typedef int8_t              __int8_t_ida;
typedef uint8_t             _UNKNOWN;

/* ------------------------------------------------------------------ *
 *  Sub register accessors
 * ------------------------------------------------------------------ */
#define LOBYTE(x)   (*((_BYTE*)&(x)))
#define LOWORD(x)   (*((_WORD*)&(x)))
#define LODWORD(x)  (*((_DWORD*)&(x)))
#define HIBYTE(x)   (*((_BYTE*)&(x) + sizeof(x) - 1))
#define HIWORD(x)   (*((_WORD*)((char*)&(x) + sizeof(x) - 2)))
#define HIDWORD(x)  (*((_DWORD*)((char*)&(x) + sizeof(x) - 4)))

#define BYTEn(x, n)  (*((_BYTE*)&(x) + (n)))
#define WORDn(x, n)  (*((_WORD*)&(x) + (n)))
#define DWORDn(x, n) (*((_DWORD*)&(x) + (n)))

#define BYTE1(x)  BYTEn(x, 1)
#define BYTE2(x)  BYTEn(x, 2)
#define BYTE3(x)  BYTEn(x, 3)
#define BYTE4(x)  BYTEn(x, 4)
#define BYTE5(x)  BYTEn(x, 5)
#define BYTE6(x)  BYTEn(x, 6)
#define BYTE7(x)  BYTEn(x, 7)
#define WORD1(x)  WORDn(x, 1)
#define WORD2(x)  WORDn(x, 2)
#define WORD3(x)  WORDn(x, 3)
#define SLOBYTE(x)  (*((int8_t*)&(x)))
#define SLOWORD(x)  (*((int16_t*)&(x)))
#define SLODWORD(x) (*((int32_t*)&(x)))

/* Sign / zero extension helpers */
#define SBYTEn(x, n)  (*((int8_t*)&(x) + (n)))
#define SWORDn(x, n)  (*((int16_t*)&(x) + (n)))
#define SDWORDn(x, n) (*((int32_t*)&(x) + (n)))

/* ------------------------------------------------------------------ *
 *  Misc pseudo intrinsics
 * ------------------------------------------------------------------ */
#if defined(__cplusplus)
template <typename T> struct ida_make_unsigned { typedef T type; };
template <> struct ida_make_unsigned<char>     { typedef unsigned char type; };
template <> struct ida_make_unsigned<int8_t>   { typedef uint8_t  type; };
template <> struct ida_make_unsigned<int16_t>  { typedef uint16_t type; };
template <> struct ida_make_unsigned<int32_t>  { typedef uint32_t type; };
template <> struct ida_make_unsigned<int64_t>  { typedef uint64_t type; };

template <typename T>
static inline T ida_rotate_left(T value, int count)
{
    typedef typename ida_make_unsigned<T>::type U;
    const unsigned int bits = (unsigned int)(sizeof(T) * 8u);
    const unsigned int n = ((unsigned int)count) % bits;
    return n == 0 ? value : (T)(((U)value << n) | ((U)value >> (bits - n)));
}

#   define __ROL__(x, y)  ida_rotate_left((x), (y))
#   define __ROR__(x, y)  ida_rotate_left((x), -(y))
#endif

/* Hex-Rays renders "lock xadd" as _InterlockedAdd, which is an ARM-only intrinsic on
 * MSVC.  The reconstructed code uses InterlockedIncrement/InterlockedDecrement instead;
 * this shim keeps raw listings compiling on x64. */
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#   include <intrin.h>
#   define _InterlockedAdd(target, value) (_InterlockedExchangeAdd((target), (value)) + (value))
#endif

#endif /* GFSDK_AFTERMATH_IDA_DEFS_H */
