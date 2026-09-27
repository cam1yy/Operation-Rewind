/*
 * setupapi.h -- stub used only by tests/syntax_check.sh (see windows.h).
 */

#ifndef GFSDK_AFTERMATH_SHIM_SETUPAPI_H
#define GFSDK_AFTERMATH_SHIM_SETUPAPI_H

#include <windows.h>
#include <devpropdef.h>

typedef struct _SP_DEVINFO_DATA
{
    DWORD     cbSize;
    GUID      ClassGuid;
    DWORD     DevInst;
    ULONG_PTR Reserved;
} SP_DEVINFO_DATA, *PSP_DEVINFO_DATA;

typedef struct _SP_DEVICE_INTERFACE_DATA
{
    DWORD     cbSize;
    GUID      InterfaceClassGuid;
    DWORD     Flags;
    ULONG_PTR Reserved;
} SP_DEVICE_INTERFACE_DATA, *PSP_DEVICE_INTERFACE_DATA;

typedef struct _SP_DEVICE_INTERFACE_DETAIL_DATA_W
{
    DWORD cbSize;
    WCHAR DevicePath[ANYSIZE_ARRAY];
} SP_DEVICE_INTERFACE_DETAIL_DATA_W, *PSP_DEVICE_INTERFACE_DETAIL_DATA_W;

typedef struct _SP_ALTPLATFORM_INFO_V2
{
    DWORD cbSize;
    DWORD Platform;
    DWORD MajorVersion;
    DWORD MinorVersion;
    WORD  ProcessorArchitecture;
    WORD  Reserved;
    DWORD FirstValidatedMajorVersion;
    DWORD FirstValidatedMinorVersion;
} SP_ALTPLATFORM_INFO, *PSP_ALTPLATFORM_INFO;

#define DIGCF_DEFAULT         0x00000001
#define DIGCF_PRESENT         0x00000002
#define DIGCF_ALLCLASSES      0x00000004
#define DIGCF_PROFILE         0x00000008
#define DIGCF_DEVICEINTERFACE 0x00000010

#define SPDRP_SERVICE      0x00000004
#define SPDRP_DRIVER       0x00000009
#define SPDRP_UPPERFILTERS 0x00000011
#define SPDRP_LOWERFILTERS 0x00000012

#endif /* GFSDK_AFTERMATH_SHIM_SETUPAPI_H */
