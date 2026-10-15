// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "Globals.h"

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
  switch (dwReason) {
    case DLL_PROCESS_ATTACH:
      g_hInst = hInstance;
      // The text service runs inside other applications. Their process-wide
      // exception handler and crash-reporting policy belong to the host.
      if (!InitializeCriticalSectionAndSpinCount(&g_cs, 0))
        return FALSE;
      break;
    case DLL_PROCESS_DETACH:
      DeleteCriticalSection(&g_cs);
      break;
  }
  return TRUE;
}
