#pragma once
#include <windows.h>

// Only the probe DLL exports this bounded in-memory trace. No document text,
// registry values, or persistent logs are collected.
struct ProbeLanguageBarEvent {
  ULONG_PTR service;
  DWORD operation;  // 1: query manager; 2: add; 3: remove;
                    // 4: ownership before removal; 5: ownership after addition
  HRESULT result;
  DWORD existingOwner;  // 0: unknown; 1: absent; 2: this object;
                        // 3: another probe object; 4: another service
  GUID existingService;
};
