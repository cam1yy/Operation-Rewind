// =============================================================================
//  GFSDK_Aftermath_Defines.h
// -----------------------------------------------------------------------------
//  Part of the reconstruction of GFSDK_Aftermath_Lib.x64.dll (Nsight Aftermath,
//  library revision 1.3 / API version 0x13).
//
//  This header contains every type that the reconstructed binary actually
//  consumes, plus the public SDK enumerations that its return values are drawn
//  from.  The numeric values below were *not* copied blindly from a modern SDK
//  header: they were recovered from the disassembly (see
//  docs/ANALYSIS_NOTES.md, section "Result codes") and cross-checked against the
//  published Aftermath header.
//
//  Recovered evidence for the version of the SDK that this DLL implements:
//     * GFSDK_Aftermath_DX11/DX12_Initialize() compares the version argument
//       against the immediate value 19 (0x13) and returns FAIL_VersionMismatch
//       otherwise  -> GFSDK_Aftermath_Version_API == 0x13.
//     * Feature flags: the DLL only ever tests bit 0 (markers) and bit 1
//       (resource tracking).  See GFSDK_Aftermath_FeatureFlags below.
// =============================================================================

#ifndef GFSDK_AFTERMATH_DEFINES_H
#define GFSDK_AFTERMATH_DEFINES_H

#include <stdint.h>

// -----------------------------------------------------------------------------
// DLL import/export decoration.
//   The library exports plain, undecorated names (see src/exports.def).  Under
//   x64 there is only one calling convention, so the decoration is a no-op
//   there; it is kept for completeness for a hypothetical Win32 build.
// -----------------------------------------------------------------------------
#if defined(__cplusplus)
#  define GFSDK_AFTERMATH_EXTERN_C extern "C"
#else
#  define GFSDK_AFTERMATH_EXTERN_C
#endif

// Calling convention for the private NVAPI entry points.  x64 has a single
// convention so this is a no-op in practice; it is spelled out because the
// driver side interfaces are C linkage.  GCC/Clang do not have the keyword.
#if defined(_MSC_VER)
#  define AFTERMATH_CDECL __cdecl
#else
#  define AFTERMATH_CDECL
#endif

#if defined(_MSC_VER)
#  define GFSDK_AFTERMATH_CALL __stdcall
#  if defined(GFSDK_AFTERMATH_EXPORTS)
#    define GFSDK_AFTERMATH_API GFSDK_AFTERMATH_EXTERN_C __declspec(dllexport) GFSDK_AFTERMATH_CALL
#  elif defined(GFSDK_AFTERMATH_STATIC)
#    define GFSDK_AFTERMATH_API GFSDK_AFTERMATH_EXTERN_C
#  else
#    define GFSDK_AFTERMATH_API GFSDK_AFTERMATH_EXTERN_C __declspec(dllimport) GFSDK_AFTERMATH_CALL
#  endif
#else
#  define GFSDK_AFTERMATH_CALL
#  define GFSDK_AFTERMATH_API GFSDK_AFTERMATH_EXTERN_C
#endif

// -----------------------------------------------------------------------------
// API version.
//
//   GFSDK_Aftermath_Initialize() rejects any other value with
//   GFSDK_Aftermath_Result_FAIL_VersionMismatch -- NOT FAIL_ApiError, which is
//   reserved for an unknown API index.  Recovered from sub_180004690, the
//   shared implementation behind both the DX11 and the DX12 export:
//
//     if ( version != 19 )      // 19 == 0x13
//         return 0xBAD00001;    // GFSDK_Aftermath_Result_FAIL_VersionMismatch
// -----------------------------------------------------------------------------
#define GFSDK_Aftermath_Version_API 0x13

typedef uint32_t GFSDK_Aftermath_Version;

// -----------------------------------------------------------------------------
// GFSDK_Aftermath_Result
//
//   Result codes returned by every public entry point.
//
//   0xBAD00000 is the base of the failure range and doubles as the generic
//   "GFSDK_Aftermath_Result_Fail" value.  The exact revision of the SDK below
//   stops at 0xBAD00017 (FAIL_NotSupportedOnContext; observed as the constant
//   3134193687 in the modern header and consistent with the range used here).
//
//   Only the values marked with "// used by this DLL" are ever produced by the
//   binary.  The remaining enumerators are declared so that client code written
//   against the real SDK still compiles and so that the reconstructed source
//   can be compared 1:1 with the disassembly.
// -----------------------------------------------------------------------------
typedef enum GFSDK_Aftermath_Result
{
    GFSDK_Aftermath_Result_Success      = 0x1,          // returned as 1 by every API on success
    GFSDK_Aftermath_Result_NotAvailable = 0x2,

    GFSDK_Aftermath_Result_Fail                        = 0xBAD00000, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_VersionMismatch        = 0xBAD00001,
    GFSDK_Aftermath_Result_FAIL_NotInitialized         = 0xBAD00002, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_InvalidAdapter         = 0xBAD00003,
    GFSDK_Aftermath_Result_FAIL_InvalidParameter       = 0xBAD00004, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_Unknown                = 0xBAD00005, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_ApiError               = 0xBAD00006, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_NvApiIncompatible      = 0xBAD00007, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_GettingContextDataWithNewCommandList =
                                                         0xBAD00008, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_AlreadyInitialized     = 0xBAD00009,
    GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible =
                                                         0xBAD0000A, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_DriverInitFailed       = 0xBAD0000B, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported =
                                                         0xBAD0000C, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_OutOfMemory            = 0xBAD0000D, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_GetDataOnBundle        = 0xBAD0000E, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_GetDataOnDeferredContext =
                                                         0xBAD0000F, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_FeatureNotEnabled      = 0xBAD00010, // used by this DLL
    GFSDK_Aftermath_Result_FAIL_NoResourcesRegistered  = 0xBAD00011,
    GFSDK_Aftermath_Result_FAIL_ThisResourceNeverRegistered = 0xBAD00012,
    GFSDK_Aftermath_Result_FAIL_NotSupportedInUWP      = 0xBAD00013,
    GFSDK_Aftermath_Result_FAIL_D3dDllNotSupported     = 0xBAD00014,
    GFSDK_Aftermath_Result_FAIL_D3dDllInterceptionNotSupported = 0xBAD00015,
    GFSDK_Aftermath_Result_FAIL_Disabled               = 0xBAD00016,
    GFSDK_Aftermath_Result_FAIL_NotSupportedOnContext  = 0xBAD00017,

} GFSDK_Aftermath_Result;

// -----------------------------------------------------------------------------
// GFSDK_Aftermath_FeatureFlags
//
//   Passed to GFSDK_Aftermath_DX11_Initialize() / _DX12_Initialize().
//
//   Evidence:
//     * GFSDK_Aftermath_SetEventMarker() and GFSDK_Aftermath_GetData() both
//       return FAIL_FeatureNotEnabled when (flags & 0x1) == 0.
//     * GFSDK_Aftermath_GetPageFaultInformation() returns FAIL_FeatureNotEnabled
//       when (flags & 0x2) == 0.
//     * Nothing else is ever inspected by this revision of the library.
// -----------------------------------------------------------------------------
//   Deliberately NOT a C++ enum: the published SDK lets callers build the mask
//   as a plain integer, as in the documented
//
//       const uint32_t aftermathFlags =
//           GFSDK_Aftermath_FeatureFlags_EnableMarkers |
//           GFSDK_Aftermath_FeatureFlags_EnableResourceTracking;
//       GFSDK_Aftermath_DX12_Initialize(..., aftermathFlags, device);
//
//   A strict enum would reject that at the call site, so the type is a typedef
//   and the values are ordinary constants.
typedef uint32_t GFSDK_Aftermath_FeatureFlags;

enum
{
    GFSDK_Aftermath_FeatureFlags_Minimum                = 0x00000000,
    GFSDK_Aftermath_FeatureFlags_EnableMarkers          = 0x00000001,
    GFSDK_Aftermath_FeatureFlags_EnableResourceTracking = 0x00000002,

    // Not referenced by this binary; kept so that sources written against a
    // later SDK revision still build.  Values follow the published header.
    GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo       = 0x00000008,
    GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting    = 0x00000010,
    GFSDK_Aftermath_FeatureFlags_CallStackCapturing            = 0x40000000,
};

// -----------------------------------------------------------------------------
// GFSDK_Aftermath_Context_Status
//
//   Status word written into GFSDK_Aftermath_ContextData::status.  A context is
//   reported as 3 (Invalid) whenever the driver call fails, which is exactly
//   what the disassembly does:
//       *(uint32_t*)(msg + 0x0C) = 3;
//
//   Verified against the published header (3134193687 / 0xBAD00017 sits in the
//   same result range enumerated above, and the same revision defines
//   Context_Status_NotStarted == 0 ... Context_Status_Invalid == 3).
// -----------------------------------------------------------------------------
typedef enum GFSDK_Aftermath_Context_Status
{
    GFSDK_Aftermath_Context_Status_NotStarted = 0,
    GFSDK_Aftermath_Context_Status_Executing  = 1,
    GFSDK_Aftermath_Context_Status_Finished   = 2,
    GFSDK_Aftermath_Context_Status_Invalid    = 3,

} GFSDK_Aftermath_Context_Status;

// -----------------------------------------------------------------------------
// GFSDK_Aftermath_Device_Status
//
//   GFSDK_Aftermath_GetDeviceStatus() translates the raw *driver* status into
//   one of the values below.  The translation table is exactly recoverable from
//   the disassembly of the translation function:
//
//      driver status 1        ->  0        Active
//      driver status 2, 3, 4  ->  1        Timeout
//      driver status 5        ->  2        OutOfMemory
//      driver status 6, 7     ->  3        PageFault
//      anything else          ->  4        Unknown
//
//   Two details pin the numbering down:
//
//     * the output slot is pre-set to 4 before the driver is called, so a
//       driver failure leaves 4 behind -- 4 is therefore the "no information"
//       value, not a documented status;
//     * driver status 1 means "the device is running normally", which maps onto
//       the first enumerator.
//
//   So this revision is 0-based and only defines 0..3.  The longer enumeration
//   found in later SDK revisions (Stopped/Reset/DmaFault/DeviceRemovedNoGpuFault
//   and so on) post-dates this binary; the driver statuses 8 and above that it
//   would need are not produced by the driver interface this library talks to.
// -----------------------------------------------------------------------------
typedef enum GFSDK_Aftermath_Device_Status
{
    GFSDK_Aftermath_Device_Status_Active       = 0,
    GFSDK_Aftermath_Device_Status_Timeout      = 1,
    GFSDK_Aftermath_Device_Status_OutOfMemory  = 2,
    GFSDK_Aftermath_Device_Status_PageFault    = 3,

    // "No information": pre-written before the driver call and left in place
    // when the driver cannot be queried or reports an unmapped status.
    GFSDK_Aftermath_Device_Status_Unknown      = 4,

} GFSDK_Aftermath_Device_Status;

// -----------------------------------------------------------------------------
// GFSDK_Aftermath_ContextData
//
//   Output element of GFSDK_Aftermath_GetData().  Layout recovered from the
//   disassembly (sub_180004E40):
//
//       +0x00  8 bytes   marker data pointer, OR an error code when status is
//                        Invalid (the library stores the 32-bit
//                        GFSDK_Aftermath_Result zero/sign extended into the
//                        pointer sized slot)
//       +0x08  4 bytes   marker size
//       +0x0C  4 bytes   GFSDK_Aftermath_Context_Status
//       sizeof == 16, stride in GetData() is exactly 16 bytes.
// -----------------------------------------------------------------------------
typedef struct GFSDK_Aftermath_ContextData
{
    void*                            markerData;
    uint32_t                         markerSize;
    GFSDK_Aftermath_Context_Status   status;

} GFSDK_Aftermath_ContextData;

// -----------------------------------------------------------------------------
// GFSDK_Aftermath_PageFaultInformation
//
//   Opaque from the point of view of this library: the pointer supplied by the
//   caller is handed straight to the driver (nvapi entry points 0x0BBA25D7 and
//   0x6446BEB8) without ever being dereferenced.  The concrete layout lives in
//   the driver / in the SDK header and cannot be recovered from this binary, so
//   it is left as an incomplete type on purpose.
// -----------------------------------------------------------------------------
typedef struct GFSDK_Aftermath_PageFaultInformation GFSDK_Aftermath_PageFaultInformation;

// -----------------------------------------------------------------------------
// GFSDK_Aftermath_ContextHandle
//
//   Publicly opaque.  The real object is created by
//   GFSDK_Aftermath_DX11/DX12_CreateContextHandle() and released with
//   GFSDK_Aftermath_ReleaseContextHandle(); its internal layout is documented
//   in src/aftermath_internal.h.
// -----------------------------------------------------------------------------
typedef struct GFSDK_Aftermath_ContextHandleImpl* GFSDK_Aftermath_ContextHandle;

#endif // GFSDK_AFTERMATH_DEFINES_H
