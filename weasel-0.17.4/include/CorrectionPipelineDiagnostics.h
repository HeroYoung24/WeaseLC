#pragma once
#include <windows.h>
#include "LocalTestIdentity.h"

// Metadata only, compiled into the independent local test build. Never store
// text, model payloads, credentials, window titles or application identities.
namespace correction {
struct CaptureDiagnostic {
  DWORD version = 1, totalUnits = 0, addedUnits = 0, retainedUnits = 0;
  FILETIME time{};
};
enum class WorkerStage : DWORD { Received = 1, RequestStarted, Returned, Written, Rejected };
struct WorkerDiagnostic {
  DWORD version = 1;
  WorkerStage stage = WorkerStage::Received;
  DWORD units = 0, provider = 0;  // 0: HTTP, 1: compiled fixed-response fixture
  DWORD state = 0xffffffff, nativeError = 0, httpStatus = 0, elapsedMs = 0;
  FILETIME time{};
};
static_assert(sizeof(CaptureDiagnostic) == 24, "capture metadata format");
static_assert(sizeof(WorkerDiagnostic) == 40, "worker metadata format");
#ifdef WEASELC_LOCAL_TEST_BUILD
inline constexpr wchar_t kPipelineDiagnosticKey[] = WEASEL_CORRECTION_REG_KEY L"\\Diagnostics";
template<class T> inline void SavePipelineDiagnostic(const wchar_t* name, T result) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kPipelineDiagnosticKey, 0, nullptr, 0,
      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
  GetSystemTimeAsFileTime(&result.time);
  RegSetValueExW(key, name, 0, REG_BINARY, reinterpret_cast<const BYTE*>(&result), sizeof(result));
  RegCloseKey(key);
}
inline bool UseFixedAutomaticReply() {
  DWORD value = 0, bytes = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, kPipelineDiagnosticKey, L"UseFixedReplyV1",
      RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS && value == 1;
}
#endif
}  // namespace correction
