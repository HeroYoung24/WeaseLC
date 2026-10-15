#pragma once
#include <windows.h>

inline void KeepRegistrationFailure(HRESULT& result, HRESULT next) {
  if (SUCCEEDED(result) && FAILED(next)) result = next;
}

template <typename Remove>
HRESULT RemoveRegistrationItems(unsigned count, Remove remove) {
  HRESULT result = S_OK;
  for (unsigned index = 0; index < count; ++index)
    KeepRegistrationFailure(result, remove(index));
  return result;
}

struct RegistrationResult {
  HRESULT result;
  HRESULT rollback;
  unsigned failedStep;
};

template <typename Apply, typename Rollback>
RegistrationResult ApplyRegistration(unsigned count, Apply apply, Rollback undo) {
  for (unsigned index = 0; index < count; ++index) {
    const HRESULT result = apply(index);
    if (FAILED(result)) return {result, undo(), index};
  }
  return {S_OK, S_OK, count};
}

// Only a missing registry key is already clean. Permission and other errors
// must remain visible to the caller. Never normalize generic COM E_FAIL.
inline HRESULT RegistrationDeleteResult(LSTATUS status) {
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
    return S_OK;
  return HRESULT_FROM_WIN32(status);
}

inline HRESULT DeleteRegistrationKey(HKEY hive, LPCSTR path,
    decltype(&RegDeleteTreeA) remove = &RegDeleteTreeA) {
  if (!hive || !path || !*path || !remove) return E_INVALIDARG;
  return RegistrationDeleteResult(remove(hive, path));
}
