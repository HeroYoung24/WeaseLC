#pragma once
#include <windows.h>
#include "LocalTestIdentity.h"
#if defined(WEASELTSF_EXPORTS) && !defined(WEASEL_CORRECTION_PROBE_BUILD)
#include "CorrectionBrokerWire.h"
#endif

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
// LastKey: callback observed (no keycode); LastCommit: production commit delivery;
// LastGate: Capture entry/settings/target validation. Fixed-size metadata only.
struct FrontendDiagnostic {
  DWORD version = 1, stage = 0, units = 0, flags = 0;
  HRESULT error = S_OK;
  DWORD source = 0;
  FILETIME time{};
};
static_assert(sizeof(CaptureDiagnostic) == 24, "capture metadata format");
static_assert(sizeof(WorkerDiagnostic) == 40, "worker metadata format");
static_assert(sizeof(FrontendDiagnostic) == 32, "frontend metadata format");
#ifdef WEASELC_LOCAL_TEST_BUILD
inline constexpr wchar_t kPipelineDiagnosticKey[] = WEASEL_CORRECTION_REG_KEY L"\\Diagnostics";
template<class T> inline void SavePipelineDiagnostic(const wchar_t* name, T result) {
#if defined(WEASELTSF_EXPORTS) && !defined(WEASEL_CORRECTION_PROBE_BUILD)
  if (!wcscmp(name, L"LastCapture") && sizeof(T) == 24) {
    GetSystemTimeAsFileTime(&result.time);
    RelayCorrectionMetadata(BrokerMetadata::Capture, &result, sizeof(result)); return;
  }
#endif
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
inline void SaveFrontendDiagnostic(const wchar_t* name, FrontendDiagnostic result,
                                   const wchar_t* path = kPipelineDiagnosticKey) {
#if defined(WEASELTSF_EXPORTS) && !defined(WEASEL_CORRECTION_PROBE_BUILD)
  if (!wcscmp(path, kPipelineDiagnosticKey)) {
    const auto kind = !wcscmp(name, L"LastKey") ? BrokerMetadata::Key :
        !wcscmp(name, L"LastCommit") ? BrokerMetadata::Commit : BrokerMetadata::Gate;
    GetSystemTimeAsFileTime(&result.time);
    RelayCorrectionMetadata(kind, &result, sizeof(result)); return;
  }
#endif
  // A loaded, retired TIP may still receive callbacks during teardown. Never
  // recreate removed test settings after Purge; the test controller provisions
  // the diagnostics key before enabling the acceptance test.
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  GetSystemTimeAsFileTime(&result.time);
  RegSetValueExW(key, name, 0, REG_BINARY, reinterpret_cast<const BYTE*>(&result), sizeof(result));
  RegCloseKey(key);
}
#endif
}  // namespace correction
