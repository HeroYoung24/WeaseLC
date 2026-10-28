#pragma once
#include <CorrectionProbe.h>
#include <limits>

namespace correction {
// Explicit false-positive reports are aggregate local counts only. Neither
// original text, replacement, app path nor document identity is persisted.
inline bool ReadFalsePositiveCount(DWORD& count) {
  DWORD size = sizeof(count); count = 0;
  const auto result = RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, L"FalsePositiveCountV1",
      RRF_RT_REG_DWORD, nullptr, &count, &size);
  return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
}
inline bool RecordFalsePositive() {
  DWORD count = 0; if (!ReadFalsePositiveCount(count)) return false;
  if (count != (std::numeric_limits<DWORD>::max)()) ++count;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const auto result = RegSetValueExW(key, L"FalsePositiveCountV1", 0, REG_DWORD,
      reinterpret_cast<const BYTE*>(&count), sizeof(count));
  RegCloseKey(key); return result == ERROR_SUCCESS;
}
inline bool ClearFalsePositiveCount() {
  HKEY key = nullptr;
  const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_SET_VALUE, &key);
  if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) return true;
  if (opened != ERROR_SUCCESS) return false;
  const auto result = RegDeleteValueW(key, L"FalsePositiveCountV1"); RegCloseKey(key);
  return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}
}  // namespace correction
