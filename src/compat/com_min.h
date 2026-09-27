/*
 * com_min.h -- the tiny slice of COM/D3D knowledge this DLL actually needs.
 *
 * The original GFSDK_Aftermath_Lib.x64.dll does not link against d3d11.lib/d3d12.lib and
 * carries no type information for the Direct3D interfaces: it only ever
 *
 *   - calls IUnknown::QueryInterface / IUnknown::Release on the device it is given, and
 *   - calls two v-table slots by index:
 *       * [8]   on an ID3D12CommandList      (byte offset 0x40)  -> GetType()
 *       * [112] on an ID3D11DeviceContext    (byte offset 0x380) -> GetType()
 *
 * Reproducing that with raw v-table indices (rather than #including d3d11.h/d3d12.h)
 * keeps the rebuilt DLL free of any DirectX SDK dependency, exactly like the original.
 */

#ifndef GFSDK_AFTERMATH_COM_MIN_H
#define GFSDK_AFTERMATH_COM_MIN_H

#include <windows.h>
#include <unknwn.h>

namespace aftermath {

/* Index of ID3D11DeviceContext::GetType() -- IUnknown(3) + ID3D11DeviceChild(4) +
 * ID3D11DeviceContext methods.  The decompiled call is  (*(ctx + 0x380))(ctx). */
const unsigned int kID3D11DeviceContext_GetType_VTableIndex = 0x380 / sizeof(void*);   /* 112 */

/* Index of ID3D12CommandList::GetType() -- IUnknown(3) + ID3D12Object(4) +
 * ID3D12DeviceChild(1).  The decompiled call is  (*(list + 0x40))(list). */
const unsigned int kID3D12CommandList_GetType_VTableIndex = 0x40 / sizeof(void*);      /*   8 */

/* D3D11_DEVICE_CONTEXT_TYPE / D3D12_COMMAND_LIST_TYPE values the library tests for. */
const unsigned int kD3D11DeviceContextDeferred = 1;   /* D3D11_DEVICE_CONTEXT_DEFERRED */
const unsigned int kD3D12CommandListTypeBundle = 1;   /* D3D12_COMMAND_LIST_TYPE_BUNDLE */

/*
 * Call a v-table entry by index on a COM object that we deliberately keep untyped.
 * Only used for the two GetType() calls documented above.
 */
template <typename ResultType>
inline ResultType CallVTableByIndex(void* pComObject, unsigned int index)
{
    typedef ResultType(STDMETHODCALLTYPE* PfnMethod)(void*);
    void** const vtable = *reinterpret_cast<void***>(pComObject);
    return reinterpret_cast<PfnMethod>(vtable[index])(pComObject);
}

inline unsigned int GetD3D11DeviceContextType(void* pDeviceContext)
{
    return CallVTableByIndex<unsigned int>(pDeviceContext, kID3D11DeviceContext_GetType_VTableIndex);
}

inline unsigned int GetD3D12CommandListType(void* pCommandList)
{
    return CallVTableByIndex<unsigned int>(pCommandList, kID3D12CommandList_GetType_VTableIndex);
}

} /* namespace aftermath */

#endif /* GFSDK_AFTERMATH_COM_MIN_H */
