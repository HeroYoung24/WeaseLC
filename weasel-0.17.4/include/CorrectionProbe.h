#pragma once

#include <windows.h>

namespace correction {
constexpr wchar_t kRegistryKey[] = L"Software\\Rime\\Weasel\\Correction";
constexpr LONG kProbeCharactersEachSide = 64;

enum class ProbeStatus : DWORD {
  NotRun = 0,
  AwaitingEdit = 1,
  Readable = 2,
  NoContext = 3,
  FocusChanged = 4,
  ScopeUnavailable = 5,
  ProtectedField = 6,
  SelectionUnavailable = 7,
  ReadFailed = 8,
};

struct ProbeResult {
  DWORD version = 1;
  ProbeStatus status = ProbeStatus::NotRun;
  DWORD before = 0;
  DWORD after = 0;
};

#ifdef WEASEL_CORRECTION_PROBE_BUILD
extern bool g_captureProbeResults;
extern ProbeResult g_capturedProbeResult;
#endif

inline void SaveProbeResult(const ProbeResult& result) {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (g_captureProbeResults) {
    g_capturedProbeResult = result;
    return;
  }
#endif
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    return;
  RegSetValueExW(key, L"LastProbe", 0, REG_BINARY,
                 reinterpret_cast<const BYTE*>(&result), sizeof(result));
  RegCloseKey(key);
}

inline ProbeResult LoadProbeResult() {
  ProbeResult result;
  DWORD size = sizeof(result);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, L"LastProbe",
                   RRF_RT_REG_BINARY, nullptr, &result, &size) != ERROR_SUCCESS ||
      size != sizeof(result) || result.version != 1 ||
      static_cast<DWORD>(result.status) >
          static_cast<DWORD>(ProbeStatus::ReadFailed))
    return ProbeResult{};
  return result;
}
}  // namespace correction
