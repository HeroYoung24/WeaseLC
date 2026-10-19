#include <windows.h>
#include <CorrectionPipelineDiagnostics.h>
#include <iostream>
#include <string>

int wmain() {
  using namespace correction;
  const auto root = L"Software\\WeaseLC-Frontend-Test-" + std::to_wstring(GetCurrentProcessId());
  const auto path = root + L"\\Diagnostics";
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks; if (!ok) ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
  };
  auto absent = [&]() {
    HKEY key = nullptr;
    const auto error = RegOpenKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, KEY_READ, &key);
    if (key) RegCloseKey(key);
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  };
  if (!absent()) return 2;  // Never modify an existing key, even a test key.
  FrontendDiagnostic entry;
  entry.stage = 6; entry.units = 6; entry.flags = 0x10103; entry.source = 6;
  SaveFrontendDiagnostic(L"LastGate", entry, path.c_str());
  check(absent(), "callback cannot provision missing test settings");
  HKEY key = nullptr; DWORD disposition = 0;
  const auto created = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
      KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition);
  check(created == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY, "controller provisions a fresh diagnostic key");
  if (created == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY) {
    SaveFrontendDiagnostic(L"LastGate", entry, path.c_str());
    FrontendDiagnostic read{}; DWORD bytes = sizeof(read);
    check(RegGetValueW(key, nullptr, L"LastGate", RRF_RT_REG_BINARY, nullptr, &read, &bytes) == ERROR_SUCCESS &&
        bytes == 32 && read.version == 1 && read.stage == entry.stage && read.units == entry.units &&
        read.flags == entry.flags && read.source == entry.source && read.error == S_OK &&
        (read.time.dwHighDateTime || read.time.dwLowDateTime), "fixed frontend metadata round trips without text or keycodes");
  }
  if (key) RegCloseKey(key);
  // The root was absent on entry and only this test can create it.
  check(RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str()) == ERROR_SUCCESS, "temporary frontend settings removed");
  SaveFrontendDiagnostic(L"LastKey", entry, path.c_str());
  SaveFrontendDiagnostic(L"LastCommit", entry, path.c_str());
  SaveFrontendDiagnostic(L"LastGate", entry, path.c_str());
  check(absent(), "late callbacks never recreate purged test settings");
  std::cout << "Frontend metadata checks = " << checks << ", failures = " << failures << '\n';
  return failures ? 1 : 0;
}
