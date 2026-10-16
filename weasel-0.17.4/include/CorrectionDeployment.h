#pragma once
#include <windows.h>
#include <string>
#include <algorithm>

namespace correction {
inline bool LocalAbsoluteDirectory(const std::wstring& path) {
  if (path.size() < 3 || path.size() > 32000 ||
      !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
      path[1] != L':' || path[2] != L'\\') return false;
  if (std::any_of(path.begin(), path.end(), [](wchar_t c) {
        return c < 32 || c == L'"' || c == L'/' || c == L'|' || c == L'<' || c == L'>' || c == L'*' || c == L'?';
      }) || path.find(L':', 2) != std::wstring::npos) return false;
  for (size_t start = 3; start < path.size();) {
    const auto end = path.find(L'\\', start);
    const auto part = path.substr(start, end - start);
    if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ') return false;
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return true;
}
inline std::wstring TrimDirectory(std::wstring path) {
  while (path.size() > 3 && path.back() == L'\\') path.pop_back();
  return path;
}
inline bool SystemCorrectionModule(const std::wstring& module, const std::wstring& windows) {
  const auto slash = module.find_last_of(L'\\');
  if (slash == std::wstring::npos || !LocalAbsoluteDirectory(windows)) return false;
  const auto parent = module.substr(0, slash);
  const auto root = TrimDirectory(windows);
  return !_wcsicmp(parent.c_str(), (root + L"\\System32").c_str()) ||
      !_wcsicmp(parent.c_str(), (root + L"\\SysWOW64").c_str());
}
// Installed TSF DLLs are copied into system directories. Locate their worker
// only through the machine installer key, never PATH or the user's settings.
inline std::wstring CorrectionWorkerPath(const std::wstring& module,
    const std::wstring& windows, const std::wstring& machineRoot) {
  const auto slash = module.find_last_of(L'\\');
  if (slash == std::wstring::npos) return {};
  const auto parent = module.substr(0, slash);
  const auto root = SystemCorrectionModule(module, windows) ? machineRoot : parent;
  if (!LocalAbsoluteDirectory(root)) return {};
  return TrimDirectory(root) + L"\\WeaselDeployer.exe";
}
inline std::wstring InstalledCorrectionRoot() {
  for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
    HKEY key = nullptr;
    const auto opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Rime\\Weasel", 0, KEY_QUERY_VALUE | view, &key);
    if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) continue;
    if (opened != ERROR_SUCCESS) return {};
    WCHAR root[32768]{}; DWORD size = sizeof(root), type = 0;
    const auto result = RegQueryValueExW(key, L"WeaselRoot", nullptr, &type, reinterpret_cast<BYTE*>(root), &size);
    RegCloseKey(key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) continue;
    if (result != ERROR_SUCCESS || type != REG_SZ || size < sizeof(WCHAR) || size > sizeof(root) || size % sizeof(WCHAR)) return {};
    const size_t units = size / sizeof(WCHAR) - 1;
    if (root[units] || wcslen(root) != units) return {};
    return LocalAbsoluteDirectory(root) ? root : L"";
  }
  return {};
}
}  // namespace correction
