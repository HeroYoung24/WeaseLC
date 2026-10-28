#pragma once
#include <windows.h>
#include "LocalTestIdentity.h"
#include <string>
#include <vector>
#include <algorithm>

namespace correction {
constexpr size_t kMaxPausedApps = 64, kMaxAppName = 128;
inline bool AppName(const std::wstring& input, std::wstring& normalized) {
  if (input.size() < 5 || input.size() > kMaxAppName || input.front() == L' ' || input.back() == L' ' ||
      _wcsicmp(input.c_str() + input.size() - 4, L".exe")) return false;
  for (const auto c : input) if (c < 32 || wcschr(L"\\/:*?\"<>|", c) ||
      (c >= 0xd800 && c <= 0xdfff)) return false;
  normalized.resize(input.size());
  const bool valid = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, input.data(), static_cast<int>(input.size()),
      normalized.data(), static_cast<int>(normalized.size()), nullptr, nullptr, 0) != 0;
  if (!valid) normalized.clear(); return valid;
}
inline const std::wstring& CurrentAppName() {
  static const std::wstring name = [] {
    wchar_t path[32768]{}; const auto length = GetModuleFileNameW(nullptr, path, _countof(path));
    std::wstring result;
    if (length && length < _countof(path)) {
      const auto slash = wcsrchr(path, L'\\');
      AppName(slash ? slash + 1 : path, result);
    }
    return result;
  }();
  return name;
}
inline bool DecodePausedApps(const std::vector<wchar_t>& data, std::vector<std::wstring>& apps) {
  if (data.size() < 2 || data.size() > (kMaxAppName + 1) * kMaxPausedApps + 1 ||
      data.back() || data[data.size() - 2]) return false;
  std::vector<std::wstring> result;
  size_t at = 0;
  while (at + 1 < data.size() && data[at]) {
    size_t end = at; while (end < data.size() && data[end]) ++end;
    std::wstring value;
    if (!AppName(std::wstring(data.data() + at, end - at), value) || result.size() >= kMaxPausedApps ||
        std::find(result.begin(), result.end(), value) != result.end()) return false;
    result.push_back(std::move(value)); at = end + 1;
  }
  // Reject embedded empty entries/trailing junk instead of accepting a prefix.
  if ((result.empty() && data.size() != 2) || (!result.empty() && at != data.size() - 1)) return false;
  apps = std::move(result); return true;
}
inline bool ReadPausedApps(HKEY key, std::vector<std::wstring>& apps) {
  DWORD bytes = 0;
  auto status = RegGetValueW(key, nullptr, L"PausedAppsV1", RRF_RT_REG_MULTI_SZ | RRF_NOEXPAND, nullptr, nullptr, &bytes);
  if (status == ERROR_FILE_NOT_FOUND) { apps.clear(); return true; }
  if (status != ERROR_SUCCESS || bytes % sizeof(wchar_t) || bytes < 4 ||
      bytes > ((kMaxAppName + 1) * kMaxPausedApps + 1) * sizeof(wchar_t)) return false;
  std::vector<wchar_t> data(bytes / sizeof(wchar_t));
  const auto expected = bytes;
  status = RegGetValueW(key, nullptr, L"PausedAppsV1", RRF_RT_REG_MULTI_SZ | RRF_NOEXPAND, nullptr, data.data(), &bytes);
  return status == ERROR_SUCCESS && bytes == expected && DecodePausedApps(data, apps);
}
inline bool LoadPausedApps(std::vector<std::wstring>& apps) {
  HKEY key = nullptr;
  const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, WEASEL_CORRECTION_REG_KEY, 0, KEY_QUERY_VALUE, &key);
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) { apps.clear(); return true; }
  if (status != ERROR_SUCCESS) return false;
  bool valid = false;
  try { valid = ReadPausedApps(key, apps); } catch (...) {}
  RegCloseKey(key); return valid;
}
inline bool SavePausedApps(const std::vector<std::wstring>& apps) {
  try {
    if (apps.size() > kMaxPausedApps) return false;
    std::vector<wchar_t> data;
    for (const auto& app : apps) { data.insert(data.end(), app.begin(), app.end()); data.push_back(0); }
    data.push_back(0); if (apps.empty()) data.push_back(0);
    std::vector<std::wstring> checked;
    if (!DecodePausedApps(data, checked)) return false;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, WEASEL_CORRECTION_REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const auto status = RegSetValueExW(key, L"PausedAppsV1", 0, REG_MULTI_SZ,
        reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size() * sizeof(wchar_t)));
    RegCloseKey(key); return status == ERROR_SUCCESS;
  } catch (...) { return false; }
}
struct AppPolicyCache { ULONGLONG checked = 0; bool paused = true; };
inline AppPolicyCache& HostAppPolicyCache() { static thread_local AppPolicyCache cache; return cache; }
inline bool UserPausedHost(bool force = false) noexcept {
  auto& cache = HostAppPolicyCache(); const auto now = GetTickCount64();
  if (!force && cache.checked && now - cache.checked < 500) return cache.paused;
  cache.checked = now; cache.paused = true;
  try {
    const auto& app = CurrentAppName(); std::vector<std::wstring> apps;
    if (!app.empty() && LoadPausedApps(apps)) cache.paused = std::find(apps.begin(), apps.end(), app) != apps.end();
  } catch (...) {}
  return cache.paused;  // Malformed/unreadable policy disables only correction.
}
}  // namespace correction
