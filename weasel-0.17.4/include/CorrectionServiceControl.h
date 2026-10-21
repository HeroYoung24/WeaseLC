#pragma once
#include "CorrectionBrokerSupervisor.h"
#include "CorrectionBrokerWire.h"
#include <memory>

namespace correction {
// A separate backend-owned endpoint survives coordinator crashes. Closed schema:
// no text, paths, settings changes, arbitrary commands, or process termination.
struct ServiceRequest {
  DWORD magic = 0x31534357, version = 1, operation = 0; // 0 snapshot, 1 retry exhausted budget
};
struct ServiceReply {
  DWORD magic = 0x31524357, version = 1, accepted = 0;
  DWORD stage = 0, processId = 0, attempts = 0, failures = 0, exitCode = 0;
  DWORD launchFailed = 0, retryAvailable = 0;
};
static_assert(sizeof(ServiceRequest) == 12 && sizeof(ServiceReply) == 40, "fixed cross-architecture control");
inline std::wstring ServicePipeName() {
  const auto base = CorrectionPipeName();
  return base.empty() ? base : base + L".supervisor.v1";
}
namespace service_detail {
struct Handle {
  HANDLE value;
  explicit Handle(HANDLE v = nullptr) : value(v) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};
inline bool TrustedProcess(DWORD pid, const std::wstring& expected, const weasel::PipeAccessPolicy& access) {
  Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)), token;
  WCHAR path[32768]{}; DWORD size = _countof(path);
  return process.value && !expected.empty() &&
      QueryFullProcessImageNameW(process.value, 0, path, &size) && !_wcsicmp(path, expected.c_str()) &&
      OpenProcessToken(process.value, TOKEN_QUERY, &token.value) && access.MatchesToken(token.value);
}
inline bool Transfer(HANDLE pipe, void* data, DWORD size, bool read, HANDLE cancel, DWORD budget = 120) {
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!event.value) return false;
  OVERLAPPED io{}; io.hEvent = event.value; DWORD bytes = 0;
  const BOOL ok = read ? ReadFile(pipe, data, size, &bytes, &io) : WriteFile(pipe, data, size, &bytes, &io);
  if (ok) return bytes == size;
  if (GetLastError() != ERROR_IO_PENDING) return false;
  HANDLE events[]{cancel, event.value};
  const DWORD result = cancel ? WaitForMultipleObjects(2, events, FALSE, budget) : WaitForSingleObject(event.value, budget);
  if (result != (cancel ? WAIT_OBJECT_0 + 1 : WAIT_OBJECT_0)) {
    CancelIoEx(pipe, &io); GetOverlappedResult(pipe, &io, &bytes, TRUE); return false;
  }
  return GetOverlappedResult(pipe, &io, &bytes, FALSE) && bytes == size;
}
}
// Used on a management worker, never on the input or UI thread. Failure also
// covers older backends without this endpoint: do not try another installation.
inline bool QueryCorrectionService(const std::wstring& server, bool retry, ServiceReply& reply,
                                   HANDLE cancel = nullptr, const std::wstring& name = ServicePipeName()) {
  using namespace service_detail;
  if (name.empty() || (cancel && WaitForSingleObject(cancel, 0) != WAIT_TIMEOUT)) return false;
  Handle pipe(CreateFileW(name.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
  if (pipe.value == INVALID_HANDLE_VALUE) return false;
  ULONG pid = 0; weasel::PipeAccessPolicy access;
  if (!GetNamedPipeServerProcessId(pipe.value, &pid) || !TrustedProcess(pid, server, access)) return false;
  ServiceRequest request; request.operation = retry ? 1 : 0;
  ServiceReply result;
  if (!Transfer(pipe.value, &request, sizeof(request), false, cancel) ||
      !Transfer(pipe.value, &result, sizeof(result), true, cancel) ||
      result.magic != 0x31524357 || result.version != 1 || result.accepted > 1 ||
      result.stage > static_cast<DWORD>(BrokerProcessStage::Exhausted) ||
      result.launchFailed > 1 || result.retryAvailable > 1) return false;
  reply = result; return true;
}
class CorrectionServiceControl {
 public:
  explicit CorrectionServiceControl(BrokerSupervisor& owner) : owner_(owner) {}
  ~CorrectionServiceControl() { Stop(); }
  // Start/Stop: backend owner only. Stop this listener before stopping its owner.
  bool Start(const std::wstring& trustedClient, const std::wstring& name = ServicePipeName()) {
    if (thread_.joinable() || name.empty() || trustedClient.empty()) return false;
    stop_.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_.value) return false;
    try {
      access_ = std::make_unique<weasel::PipeAccessPolicy>();
      HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
          1, sizeof(ServiceReply), sizeof(ServiceRequest), 0, access_->get_attr());
      if (pipe == INVALID_HANDLE_VALUE) { Stop(); return false; }
      service_detail::Handle initial(pipe);
      thread_ = std::thread([this, pipe, trustedClient]() { Listen(pipe, trustedClient); });
      initial.value = nullptr;
    } catch (...) { Stop(); return false; }
    return true;
  }
  void Stop() {
    if (stop_.value) SetEvent(stop_.value);
    if (thread_.joinable()) thread_.join();
    if (stop_.value) CloseHandle(stop_.value);
    stop_.value = nullptr; access_.reset();
  }
 private:
  void Listen(HANDLE handle, const std::wstring& client) noexcept {
    using namespace service_detail;
    Handle pipe(handle), event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.value) return;
    while (WaitForSingleObject(stop_.value, 0) == WAIT_TIMEOUT) {
      ResetEvent(event.value);
      OVERLAPPED io{}; io.hEvent = event.value;
      const BOOL connected = ConnectNamedPipe(pipe.value, &io);
      const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
      if (error == ERROR_IO_PENDING) {
        HANDLE events[]{stop_.value, event.value}; DWORD bytes = 0;
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
          CancelIoEx(pipe.value, &io); GetOverlappedResult(pipe.value, &io, &bytes, TRUE); break;
        }
        if (!GetOverlappedResult(pipe.value, &io, &bytes, FALSE)) { DisconnectNamedPipe(pipe.value); continue; }
      } else if (error != ERROR_SUCCESS && error != ERROR_PIPE_CONNECTED) {
        DisconnectNamedPipe(pipe.value); WaitForSingleObject(stop_.value, 10); continue;
      }
      try {
        ServiceRequest request; ServiceReply reply; ULONG pid = 0;
        if (Transfer(pipe.value, &request, sizeof(request), true, stop_.value) &&
            request.magic == 0x31534357 && request.version == 1 && request.operation <= 1 &&
            access_->AllowsClient(pipe.value) && GetNamedPipeClientProcessId(pipe.value, &pid) &&
            TrustedProcess(pid, client, *access_)) {
          reply.accepted = request.operation == 1 && owner_.RequestRetry();
          const auto status = owner_.Snapshot();
          reply.stage = static_cast<DWORD>(status.stage); reply.processId = status.processId;
          reply.attempts = status.attempts; reply.failures = status.consecutiveFailures;
          reply.exitCode = status.lastExitCode; reply.launchFailed = status.launchFailed;
          reply.retryAvailable = status.retryAvailable;
          Transfer(pipe.value, &reply, sizeof(reply), false, stop_.value);
        }
      } catch (...) {} // Failure of management must never affect normal input.
      DisconnectNamedPipe(pipe.value);
    }
  }
  BrokerSupervisor& owner_;
  service_detail::Handle stop_;
  std::unique_ptr<weasel::PipeAccessPolicy> access_;
  std::thread thread_;
};
}
