/* The public Aftermath headers are C-compatible in the real SDK; check ours too. */
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_DX11.h"
#include "GFSDK_Aftermath_DX12.h"

int use_it(void* device, void* ctx)
{
    GFSDK_Aftermath_ContextHandle h = 0;
    GFSDK_Aftermath_ContextData   data;
    GFSDK_Aftermath_Device_Status status;
    unsigned int flags = GFSDK_Aftermath_FeatureFlags_EnableMarkers |
                         GFSDK_Aftermath_FeatureFlags_EnableResourceTracking;

    if (GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API, flags, device)
            == GFSDK_Aftermath_Result_Success)
    {
        GFSDK_Aftermath_DX12_CreateContextHandle(ctx, &h);
        GFSDK_Aftermath_SetEventMarker(h, "marker", 6);
        GFSDK_Aftermath_GetData(1, &h, &data);
        GFSDK_Aftermath_GetDeviceStatus(&status);
        GFSDK_Aftermath_ReleaseContextHandle(h);
    }
    return (int)status;
}
