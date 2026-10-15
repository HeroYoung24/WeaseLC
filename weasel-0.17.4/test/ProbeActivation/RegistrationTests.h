#pragma once
#include "../../WeaselTSF/RegistrationWorkflow.h"
#include <array>
#include <iostream>
#include <vector>

namespace RegistrationTests {
inline LSTATUS deleteStatus = ERROR_SUCCESS;
inline unsigned deleteCalls = 0;
inline bool deleteIdentityValid = false;
inline constexpr char testKey[] =
    "Software\\Microsoft\\CTF\\TIP\\{B749B45E-7DCE-4496-8414-9EE10970260A}";

inline LSTATUS WINAPI FakeDelete(HKEY hive, LPCSTR path) {
  ++deleteCalls;
  deleteIdentityValid = hive == HKEY_LOCAL_MACHINE && path &&
      std::string(path) == testKey;
  return deleteStatus;
}

inline bool Run() {
  unsigned checked = 0, failed = 0;
  const auto check = [&](const WCHAR* name, bool valid) {
    ++checked;
    if (!valid) ++failed;
    std::wcout << L"Registration simulation " << name << L" = " << valid << L'\n';
  };

  unsigned applied = 0, rolledBack = 0;
  const auto success = ApplyRegistration(3, [&](unsigned) { ++applied; return S_OK; },
      [&] { ++rolledBack; return E_FAIL; });
  check(L"success without rollback", success.result == S_OK && success.rollback == S_OK &&
      success.failedStep == 3 && applied == 3 && !rolledBack);

  const HRESULT errors[] = {E_ACCESSDENIED, E_OUTOFMEMORY, E_UNEXPECTED};
  for (unsigned failing = 0; failing < 3; ++failing) {
    std::array<bool, 3> installed = {};
    std::vector<unsigned> events;
    const auto result = ApplyRegistration(3, [&](unsigned index) {
      events.push_back(index);
      installed[index] = true;  // Failure can occur after a partial write.
      return index == failing ? errors[index] : S_OK;
    }, [&] {
      return RemoveRegistrationItems(3, [&](unsigned index) {
        events.push_back(10 + index);
        installed[index] = false;
        return S_OK;
      });
    });
    std::vector<unsigned> expected;
    for (unsigned index = 0; index <= failing; ++index) expected.push_back(index);
    for (unsigned index = 0; index < 3; ++index) expected.push_back(10 + index);
    check(L"partial registration rollback", result.result == errors[failing] &&
        result.rollback == S_OK && result.failedStep == failing && events == expected &&
        installed == std::array<bool, 3>{});
  }

  std::array<bool, 3> installed = {};
  unsigned cleanupCalls = 0;
  const auto doubleFailure = ApplyRegistration(3, [&](unsigned index) {
    installed[index] = true;
    return index == 1 ? E_OUTOFMEMORY : S_OK;
  }, [&] {
    return RemoveRegistrationItems(3, [&](unsigned index) {
      ++cleanupCalls;
      if (index == 0) return E_ACCESSDENIED;
      installed[index] = false;
      return S_OK;
    });
  });
  check(L"registration and rollback failures distinguished",
      doubleFailure.result == E_OUTOFMEMORY && doubleFailure.rollback == E_ACCESSDENIED &&
      doubleFailure.failedStep == 1 && cleanupCalls == 3 && installed[0] &&
      !installed[1] && !installed[2]);
  cleanupCalls = 0;
  const auto retry = RemoveRegistrationItems(3, [&](unsigned index) {
    ++cleanupCalls;
    installed[index] = false;
    return S_OK;
  });
  check(L"retry clears partial state", retry == S_OK && cleanupCalls == 3 &&
      installed == std::array<bool, 3>{});

  // Exercise every failure position of the top-level, profile and category
  // cleanup loops. Later errors must not replace the first or stop traversal.
  for (unsigned count : {3u, 5u, 16u}) {
    for (unsigned failing = 0; failing < count; ++failing) {
      std::vector<unsigned> visited;
      const auto result = RemoveRegistrationItems(count, [&](unsigned index) {
        visited.push_back(index);
        if (index == failing) return E_ACCESSDENIED;
        return index > failing ? E_UNEXPECTED : S_OK;
      });
      bool ordered = visited.size() == count;
      for (unsigned index = 0; index < visited.size(); ++index)
        ordered = visited[index] == index && ordered;
      check(L"cleanup continues and preserves first error", ordered && result == E_ACCESSDENIED);
    }
  }

  const LSTATUS statuses[] = {ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND,
      ERROR_ACCESS_DENIED, ERROR_BUSY, ERROR_INVALID_HANDLE};
  for (const LSTATUS status : statuses) {
    deleteStatus = status;
    deleteCalls = 0;
    deleteIdentityValid = false;
    const HRESULT expected = status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND
        ? S_OK : HRESULT_FROM_WIN32(status);
    const HRESULT result = DeleteRegistrationKey(HKEY_LOCAL_MACHINE, testKey, FakeDelete);
    check(L"registry delete status", result == expected && deleteCalls == 1 && deleteIdentityValid);
  }
  deleteCalls = 0;
  check(L"invalid delete target rejected",
      DeleteRegistrationKey(nullptr, testKey, FakeDelete) == E_INVALIDARG &&
      DeleteRegistrationKey(HKEY_LOCAL_MACHINE, nullptr, FakeDelete) == E_INVALIDARG &&
      DeleteRegistrationKey(HKEY_LOCAL_MACHINE, "", FakeDelete) == E_INVALIDARG &&
      DeleteRegistrationKey(HKEY_LOCAL_MACHINE, testKey, nullptr) == E_INVALIDARG && !deleteCalls);
  HRESULT genericFailure = S_OK;
  KeepRegistrationFailure(genericFailure, E_FAIL);
  KeepRegistrationFailure(genericFailure, S_OK);
  check(L"generic COM error remains failure", genericFailure == E_FAIL);

  std::wcout << L"Registration simulations checked = " << checked
             << L", failures = " << failed << L'\n';
  return !failed;
}
}  // namespace RegistrationTests
