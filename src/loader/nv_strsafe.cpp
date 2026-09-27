/*
 * nv_strsafe.cpp -- reconstruction of sub_180002070 .. sub_1800022E0.
 *
 * Deviation from the listing (documented in docs/RECONSTRUCTION.md):
 *   the shipped StringCopyWorkerW contains the classic strsafe.h branch
 *
 *        if (cchDest == 0) { pszDest--; cchDest++; hr = STRSAFE_E_INVALID_PARAMETER; }
 *
 *   which writes a NUL *before* the destination buffer.  It is dead code here because
 *   every one of the five public wrappers rejects cchDest == 0 up front, so the
 *   reconstruction returns early instead of reproducing the out of bounds store.
 */

#include "loader/nv_strsafe.h"

namespace loader {
namespace {

/*
 * The shared copy worker.  Copies at most cchSrc characters, never writes more than
 * cchDest characters (terminator included) and always terminates the destination.
 */
HRESULT StringCopyWorkerW(wchar_t* pszDest, size_t cchDest, const wchar_t* pszSrc, size_t cchSrc)
{
    if (cchDest == 0)
    {
        return kStrSafeInvalidParameter;   /* see file header: unreachable in practice */
    }

    HRESULT result = S_OK;

    while (cchDest != 0 && cchSrc != 0 && *pszSrc != L'\0')
    {
        *pszDest++ = *pszSrc++;
        --cchDest;
        --cchSrc;
    }

    if (cchDest == 0)
    {
        /* Truncated: step back onto the last character written and overwrite it. */
        --pszDest;
        result = kStrSafeInsufficientBuffer;
    }

    *pszDest = L'\0';
    return result;
}

/* Length of pszString, bounded by cchMax.  Fails if no terminator is found in range. */
HRESULT StringLengthWorkerW(const wchar_t* pszString, size_t cchMax, size_t* pcchLength)
{
    HRESULT result = S_OK;
    size_t length = 0;

    while (length < cchMax && pszString[length] != L'\0')
    {
        ++length;
    }

    if (length >= cchMax)
    {
        result = kStrSafeInvalidParameter;
    }

    *pcchLength = length;
    return result;
}

} /* anonymous namespace */

/* sub_180002070 */
HRESULT StringCchCopyW(wchar_t* pszDest, size_t cchDest, const wchar_t* pszSrc)
{
    if (cchDest == 0 || cchDest > kStrSafeMaxCch)
    {
        return kStrSafeInvalidParameter;
    }
    return StringCopyWorkerW(pszDest, cchDest, pszSrc, kStrSafeMaxCch);
}

/* sub_1800020F0 */
HRESULT StringCbCopyW(wchar_t* pszDest, size_t cbDest, const wchar_t* pszSrc)
{
    const size_t cchDest = cbDest / sizeof(wchar_t);
    if (cchDest == 0 || cchDest > kStrSafeMaxCch)
    {
        return kStrSafeInvalidParameter;
    }
    return StringCopyWorkerW(pszDest, cchDest, pszSrc, kStrSafeMaxCch);
}

/* sub_180002180 */
HRESULT StringCchCopyNW(wchar_t* pszDest, size_t cchDest, const wchar_t* pszSrc, size_t cchToCopy)
{
    if (cchDest == 0 || cchDest > kStrSafeMaxCch)
    {
        return kStrSafeInvalidParameter;
    }

    if (cchToCopy > kStrSafeMaxCch - 1)
    {
        /* The binary terminates the destination before bailing out. */
        *pszDest = L'\0';
        return kStrSafeInvalidParameter;
    }

    return StringCopyWorkerW(pszDest, cchDest, pszSrc, cchToCopy);
}

/* sub_180002210 */
HRESULT StringCchCatW(wchar_t* pszDest, size_t cchDest, const wchar_t* pszSrc)
{
    if (cchDest == 0 || cchDest > kStrSafeMaxCch)
    {
        return kStrSafeInvalidParameter;
    }

    size_t cchDestLength = 0;
    const HRESULT lengthResult = StringLengthWorkerW(pszDest, cchDest, &cchDestLength);
    if (FAILED(lengthResult))
    {
        return lengthResult;
    }

    return StringCopyWorkerW(pszDest + cchDestLength,
                             cchDest - cchDestLength,
                             pszSrc,
                             kStrSafeMaxCch);
}

/* sub_1800022E0 */
HRESULT StringCbCatW(wchar_t* pszDest, size_t cbDest, const wchar_t* pszSrc)
{
    const size_t cchDest = cbDest / sizeof(wchar_t);
    if (cchDest == 0 || cchDest > kStrSafeMaxCch)
    {
        return kStrSafeInvalidParameter;
    }

    size_t cchDestLength = 0;
    const HRESULT lengthResult = StringLengthWorkerW(pszDest, cchDest, &cchDestLength);
    if (FAILED(lengthResult))
    {
        return lengthResult;
    }

    return StringCopyWorkerW(pszDest + cchDestLength,
                             cchDest - cchDestLength,
                             pszSrc,
                             kStrSafeMaxCch);
}

} /* namespace loader */
