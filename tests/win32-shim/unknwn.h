/*
 * unknwn.h -- stub used only by tests/syntax_check.sh (see windows.h).
 */

#ifndef GFSDK_AFTERMATH_SHIM_UNKNWN_H
#define GFSDK_AFTERMATH_SHIM_UNKNWN_H

#include <windows.h>

#ifdef __cplusplus

struct IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual ULONG   STDMETHODCALLTYPE AddRef(void) = 0;
    virtual ULONG   STDMETHODCALLTYPE Release(void) = 0;
};

#endif /* __cplusplus */

#endif /* GFSDK_AFTERMATH_SHIM_UNKNWN_H */
