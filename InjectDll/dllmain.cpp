// dllmain.cpp — Injected DLL entry point
#include "pch.h"

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD  ul_reason_for_call,
                      LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(lpReserved);

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        OutputDebugStringW(L"[InjectDll] DLL_PROCESS_ATTACH");
        break;

    case DLL_THREAD_ATTACH:
        break;

    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        OutputDebugStringW(L"[InjectDll] DLL_PROCESS_DETACH");
        break;
    }
    return TRUE;
}
