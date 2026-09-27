/*
 * nv_strsafe.h -- the five StrSafe workers that the original DLL carries as out of line
 *                 copies (the compiler emitted one function per strsafe.h inline that was
 *                 used):
 *
 *      sub_180002070  StringCchCopyW
 *      sub_1800020F0  StringCbCopyW
 *      sub_180002180  StringCchCopyNW
 *      sub_180002210  StringCchCatW
 *      sub_1800022E0  StringCbCatW
 *
 * They are byte-for-byte the classic Windows SDK implementations: same STRSAFE_MAX_CCH
 * bound, same STRSAFE_E_INVALID_PARAMETER / STRSAFE_E_INSUFFICIENT_BUFFER results and the
 * same "always NUL terminate, even on truncation" behaviour.  They are reproduced here
 * instead of #including <strsafe.h> so that the rebuilt DLL contains the same code as the
 * original; switch the call sites to <strsafe.h> if you would rather depend on the SDK.
 */

#ifndef GFSDK_AFTERMATH_NV_STRSAFE_H
#define GFSDK_AFTERMATH_NV_STRSAFE_H

#include <windows.h>

namespace loader {

/* Values used by the binary (they match the ones in strsafe.h). */
const size_t  kStrSafeMaxCch            = 0x7FFFFFFF;
const HRESULT kStrSafeInvalidParameter  = (HRESULT)0x80070057L;   /* STRSAFE_E_INVALID_PARAMETER  */
const HRESULT kStrSafeInsufficientBuffer= (HRESULT)0x8007007AL;   /* STRSAFE_E_INSUFFICIENT_BUFFER */

HRESULT StringCchCopyW(wchar_t* pszDest, size_t cchDest, const wchar_t* pszSrc);
HRESULT StringCbCopyW(wchar_t* pszDest, size_t cbDest, const wchar_t* pszSrc);
HRESULT StringCchCopyNW(wchar_t* pszDest, size_t cchDest, const wchar_t* pszSrc, size_t cchToCopy);
HRESULT StringCchCatW(wchar_t* pszDest, size_t cchDest, const wchar_t* pszSrc);
HRESULT StringCbCatW(wchar_t* pszDest, size_t cbDest, const wchar_t* pszSrc);

} /* namespace loader */

#endif /* GFSDK_AFTERMATH_NV_STRSAFE_H */
