#pragma once
#include "CorrectionAutomatic.h"
#include "LocalTestIdentity.h"
#include "PipeSecurity.h"
#include <string>

namespace correction {
enum class BrokerOperation : DWORD { Settings = 1, Check, Metadata };
enum class BrokerStatus : DWORD {
  Ok, Unavailable, Disabled, SettingsChanged, Busy, InvalidRequest, ModelFailure
};
enum class BrokerMetadata : DWORD { Key, Commit, Gate, Capture, Automatic, Scope, ScopeTrace };
#pragma pack(push, 1)
struct BrokerSettings {
  ULONGLONG revision = 0;
  DWORD enabled = 0, timing = 0, tab = 1, side = 0, gap = 8, width = 320,
      font = 14, debounce = 250, timeoutSeconds = 30;
  double threshold = .90;
};
struct BrokerRequest {
  DWORD magic = 0x32424357, version = 2;
  BrokerOperation operation = BrokerOperation::Settings;
  ULONGLONG requestId = 0, revision = 0;
  AutomaticRequest input;
  BrokerMetadata metadata = BrokerMetadata::Key;
  DWORD metadataBytes = 0;
  BYTE data[40]{};
};
struct BrokerReply {
  DWORD magic = 0x32524357, version = 2;
  BrokerStatus status = BrokerStatus::Unavailable;
  ULONGLONG requestId = 0;
  BrokerSettings settings;
  DWORD modelState = 0xffffffff, nativeError = 0, httpStatus = 0, elapsedMs = 0;
  AutomaticReply result;
};
struct BrokerDiagnostic {
  DWORD version = 1, status = 0, units = 0, state = 0, error = 0, http = 0, elapsed = 0;
  ULONGLONG requestId = 0, revision = 0;
  FILETIME time{};
};
#pragma pack(pop)
static_assert(sizeof(BrokerDiagnostic) == 52, "cross-architecture diagnostic format");
static_assert(sizeof(BrokerRequest) < 4096 && sizeof(BrokerReply) < 4096, "bounded correction IPC");
inline std::wstring CorrectionPipeName() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  TOKEN_STATISTICS statistics{}; DWORD size = 0, session = 0;
  const bool valid = GetTokenInformation(token, TokenStatistics, &statistics, sizeof(statistics), &size) &&
      GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &size);
  CloseHandle(token);
  if (!valid) return {};
  return std::wstring(L"\\\\.\\pipe\\") + WEASEL_PIPE_NAME + L".correction.v2." +
      std::to_wstring(session) + L"." + std::to_wstring(statistics.AuthenticationId.HighPart) +
      L"." + std::to_wstring(statistics.AuthenticationId.LowPart);
}
inline DWORD MetadataSize(BrokerMetadata kind) {
  switch (kind) {
    case BrokerMetadata::Key: case BrokerMetadata::Commit: case BrokerMetadata::Gate: return 32;
    case BrokerMetadata::Capture: case BrokerMetadata::Automatic: return 24;
    case BrokerMetadata::Scope: return 20;
    case BrokerMetadata::ScopeTrace: return 40;
    default: return 0;
  }
}
// Defined only by the TSF module. Metadata has a closed schema; no text or paths.
void RelayCorrectionMetadata(BrokerMetadata, const void*, DWORD) noexcept;
}
