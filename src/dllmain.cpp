/*
 * dllmain.cpp -- reconstruction of sub_18000639C.
 *
 * The original DLL's entry point is the standard CRT one (_DllMainCRTStartup ->
 * dllmain_dispatch -> DllMain); everything between those two is compiler supplied code
 * that must NOT be reimplemented -- linking the CRT brings it back verbatim:
 *
 *      __scrt_common_main / __scrt_dllmain_*      CRT startup
 *      _initterm / _initterm_e                    static initialisers
 *      __acrt_* / __vcrt_*                        UCRT internals
 *      dllmain_raw / dllmain_crt_dispatch         CRT hooks around DllMain
 *
 * The library's own DllMain is a single "return TRUE": the DLL has no per-process or
 * per-thread state to set up, and all of the Aftermath state is created lazily by
 * GFSDK_Aftermath_DX1x_Initialize.  Note in particular that it does *not* call
 * DisableThreadLibraryCalls, so the stub above still runs for every thread.
 */

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(reasonForCall);
    UNREFERENCED_PARAMETER(reserved);

    switch (reasonForCall)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    default:
        break;
    }

    return TRUE;
}
