/*
 * devpropdef.h -- stub used only by tests/syntax_check.sh (see windows.h).
 */

#ifndef GFSDK_AFTERMATH_SHIM_DEVPROPDEF_H
#define GFSDK_AFTERMATH_SHIM_DEVPROPDEF_H

#include <windows.h>

typedef ULONG DEVPROPTYPE;
typedef GUID  DEVPROPGUID;
typedef ULONG DEVPROPID;

typedef struct _DEVPROPKEY
{
    DEVPROPGUID fmtid;
    DEVPROPID   pid;
} DEVPROPKEY;

#define DEVPROP_TYPE_STRING 0x00000012

#endif /* GFSDK_AFTERMATH_SHIM_DEVPROPDEF_H */
