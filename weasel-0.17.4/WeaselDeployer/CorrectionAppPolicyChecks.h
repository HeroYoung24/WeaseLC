#pragma once
#include <CorrectionAppPolicy.h>
#include <functional>

inline void CheckAppPolicyRegistry(const std::function<void(bool, const char*)>& check) {
  using namespace correction;
  const auto path = L"Software\\WeaseLC-AppPolicy-Check-" + std::to_wstring(GetCurrentProcessId());
  HKEY key = nullptr; DWORD disposition = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition) != ERROR_SUCCESS) {
    check(false, "temporary native app policy key created"); return;
  }
  if (disposition != REG_CREATED_NEW_KEY) { RegCloseKey(key); check(false, "existing app policy fixture is never overwritten"); return; }
  struct Cleanup { HKEY key; std::wstring path; ~Cleanup() { RegCloseKey(key); RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); } } cleanup{key, path};
  auto write = [&](const std::vector<wchar_t>& data, DWORD type = REG_MULTI_SZ, bool odd = false) {
    return RegSetValueExW(key, L"PausedAppsV1", 0, type, reinterpret_cast<const BYTE*>(data.data()),
        static_cast<DWORD>(data.size() * sizeof(wchar_t) - (odd ? 1 : 0))) == ERROR_SUCCESS;
  };
  auto encode = [](const std::vector<std::wstring>& names) {
    std::vector<wchar_t> data;
    for (const auto& name : names) { data.insert(data.end(), name.begin(), name.end()); data.push_back(0); }
    data.push_back(0); if (names.empty()) data.push_back(0); return data;
  };
  std::vector<std::wstring> names{L"sentinel.exe"};
  check(ReadPausedApps(key, names) && names.empty(), "missing native pause value means no user pause");
  const auto single = encode({L"chatgpt.exe"});
  check(write(single) && ReadPausedApps(key, names) && names == std::vector<std::wstring>{L"chatgpt.exe"},
      "native single-app pause survives query capacity and actual byte length differences");
  check(std::find(names.begin(), names.end(), L"msedge.exe") == names.end(),
      "pausing ChatGPT does not implicitly pause Edge");
  check(write(encode({})) && ReadPausedApps(key, names) && names.empty(), "native pause can be removed and saved as an empty list");
  check(write(encode({L"msedge.exe", L"chatgpt.exe"})) && ReadPausedApps(key, names) && names.size() == 2,
      "native multi-app pause preserves both exact app identities");
  std::vector<std::wstring> maximum;
  for (unsigned i = 0; i < kMaxPausedApps; ++i) {
    std::wstring name(kMaxAppName - 4, L'a'); const auto number = std::to_wstring(i);
    name.replace(0, number.size(), number); maximum.push_back(name + L".exe");
  }
  check(write(encode(maximum)) && ReadPausedApps(key, names) && names == maximum,
      "native maximum-length pause list stays within the decoder limit");
  for (const auto& invalid : {std::vector<wchar_t>{0}, std::vector<wchar_t>{L'a', L'.', L'e', L'x', L'e', 0},
      std::vector<wchar_t>{L'a', L'.', L'e', L'x', L'e', 0, 0, L'x', 0, 0}, encode({L"a.exe", L"a.exe"})}) {
    names = {L"sentinel.exe"};
    check(write(invalid) && !ReadPausedApps(key, names) && names == std::vector<std::wstring>{L"sentinel.exe"},
        "native malformed policy is rejected without accepting a prefix or silently repairing terminators");
  }
  check(write(single, REG_SZ) && !ReadPausedApps(key, names), "native wrong-type pause value remains fail-closed");
  check(write(single, REG_MULTI_SZ, true) && !ReadPausedApps(key, names), "native odd byte length remains fail-closed");
}
