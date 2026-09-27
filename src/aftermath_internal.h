/*
 * aftermath_internal.h -- private types shared by the public entry points.
 */

#ifndef GFSDK_AFTERMATH_INTERNAL_H
#define GFSDK_AFTERMATH_INTERNAL_H

#include <windows.h>
#include <unknwn.h>

#include "GFSDK_Aftermath.h"
#include "nvapi/nvapi_types.h"

namespace aftermath {

/*
 * Which Direct3D flavour a handle belongs to.  Stored at +4 of the context handle and
 * passed as the first argument of the shared Initialize/CreateContextHandle workers
 * (sub_180004690 / sub_180004AC0), where 0 selects the D3D11 NVAPI entry points and 1
 * selects the D3D12 ones.  Any other value yields GFSDK_Aftermath_Result_FAIL_ApiError.
 */
enum ApiType
{
    kApiTypeDX11 = 0,
    kApiTypeDX12 = 1
};

/*
 * What GFSDK_Aftermath_ContextHandle really points at: 0x18 bytes from operator new,
 * laid out by sub_180004AC0 as
 *
 *      +0x00   dword   always written as 0 (the whole first qword is zeroed first)
 *      +0x04   dword   ApiType
 *      +0x08   qword   the ID3D11DeviceContext* / ID3D12CommandList* the caller gave us
 *      +0x10   qword   the handle the driver returned
 */
struct ContextHandleImpl
{
    unsigned int             reserved;
    unsigned int             apiType;
    void*                    pD3DObject;
    NvAftermathContextHandle nvHandle;
};

#if defined(_WIN64)
/* The allocation size in the binary is a literal 0x18. */
static_assert(sizeof(ContextHandleImpl) == 0x18, "context handle layout must match the original");
#endif

/* Minimum driver version that implements the Aftermath NVAPI entry points: 38788 == the
 * 387.84 branch (sub_180004690 rejects anything older). */
const NvU32 kMinimumDriverVersion = 0x9784;

/* Shared NVAPI -> GFSDK_Aftermath_Result translation (inlined at every call site in the
 * binary). */
GFSDK_Aftermath_Result TranslateNvApiStatus(NvAPI_Status status);

/* sub_180004690 and sub_180004AC0, shared by the DX11 and DX12 spellings. */
GFSDK_Aftermath_Result Initialize(ApiType apiType,
                                  GFSDK_Aftermath_Version version,
                                  unsigned int flags,
                                  IUnknown* pDevice);

GFSDK_Aftermath_Result CreateContextHandle(ApiType apiType,
                                           void* pD3DObject,
                                           GFSDK_Aftermath_ContextHandle* pOutContextHandle);

} /* namespace aftermath */

#endif /* GFSDK_AFTERMATH_INTERNAL_H */
