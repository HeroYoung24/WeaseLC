#pragma once
#include "CorrectionBrokerWire.h"
#include <cstring>

namespace correction {
std::wstring CorrectionBrokerServerPath();
// One bounded request per connection. There is no background thread or COM
// reference in the host; cancellation drains overlapped IO before freeing it.
class BrokerCall {
 public:
  BrokerCall() = default;
  BrokerCall(const BrokerCall&) = delete;
  BrokerCall& operator=(const BrokerCall&) = delete;
  ~BrokerCall() { Reset(); }
  bool Active() const { return pipe_ != INVALID_HANDLE_VALUE; }
  void Reset() {
    if (Active()) {
      if (pending_) { CancelIoEx(pipe_, &io_); DWORD bytes = 0; GetOverlappedResult(pipe_, &io_, &bytes, TRUE); }
      CloseHandle(pipe_);
    }
    if (io_.hEvent) CloseHandle(io_.hEvent);
    pipe_ = INVALID_HANDLE_VALUE; io_ = {}; pending_ = false; phase_ = 0;
    SecureZeroMemory(&request_, sizeof(request_)); SecureZeroMemory(&reply_, sizeof(reply_));
  }
  bool Begin(const std::wstring& trustedServer, const BrokerRequest& request) {
    Reset();
    const auto name = CorrectionPipeName();
    if (name.empty() || trustedServer.empty()) return false;
    pipe_ = CreateFileW(name.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (!Active() && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeW(name.c_str(), 5))
      pipe_ = CreateFileW(name.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING,
          FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (!Active()) return false;
    ULONG pid = 0;
    HANDLE process = nullptr, token = nullptr;
    bool trusted = false;
    if (GetNamedPipeServerProcessId(pipe_, &pid) &&
        (process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))) {
      WCHAR path[32768]{}; DWORD size = _countof(path);
      if (QueryFullProcessImageNameW(process, 0, path, &size) && !_wcsicmp(path, trustedServer.c_str()) &&
          OpenProcessToken(process, TOKEN_QUERY, &token)) {
        try { weasel::PipeAccessPolicy access; trusted = access.MatchesToken(token); } catch (...) {}
      }
    }
    if (token) CloseHandle(token);
    if (process) CloseHandle(process);
    if (!trusted || !(io_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr))) { Reset(); return false; }
    request_ = request; phase_ = 1;
    return Issue(false);
  }
  // 0 pending, 1 complete, -1 failed. Model requests use wait=0 from a timer.
  int Poll(BrokerReply& result, DWORD wait = 0) {
    if (!Active()) return -1;
    if (pending_) {
      if (WaitForSingleObject(io_.hEvent, wait) != WAIT_OBJECT_0) return 0;
      DWORD bytes = 0;
      if (!GetOverlappedResult(pipe_, &io_, &bytes, FALSE) ||
          bytes != (phase_ == 1 ? sizeof(request_) : sizeof(reply_))) { Reset(); return -1; }
      pending_ = false;
    }
    if (phase_ == 1) { phase_ = 2; if (!Issue(true)) return -1; if (pending_) return Poll(result, 0); }
    if (reply_.magic != 0x32524357 || reply_.version != 4 || reply_.requestId != request_.requestId ||
        reply_.status > BrokerStatus::ModelFailure) { Reset(); return -1; }
    result = reply_; Reset(); return 1;
  }
  bool Exchange(const std::wstring& server, const BrokerRequest& request, BrokerReply& result, DWORD budget = 20) {
    if (!Begin(server, request)) return false;
    const auto deadline = GetTickCount64() + budget;
    for (;;) {
      const auto now = GetTickCount64();
      const int state = Poll(result, now < deadline ? static_cast<DWORD>(deadline - now) : 0);
      if (state) return state == 1;
      if (GetTickCount64() >= deadline) { Reset(); return false; }
    }
  }
  bool SendMetadata(const std::wstring& server, const BrokerRequest& request) {
    if (!Begin(server, request)) return false;
    if (pending_) {
      if (WaitForSingleObject(io_.hEvent, 1) != WAIT_OBJECT_0) { Reset(); return false; }
      DWORD bytes = 0;
      if (!GetOverlappedResult(pipe_, &io_, &bytes, FALSE) || bytes != sizeof(request_)) { Reset(); return false; }
      pending_ = false;
    }
    Reset(); return true;  // Metadata is one way; the backend owns persistence.
  }
 private:
  bool Issue(bool read) {
    ResetEvent(io_.hEvent); DWORD bytes = 0;
    const BOOL ok = read ? ReadFile(pipe_, &reply_, sizeof(reply_), &bytes, &io_) :
        WriteFile(pipe_, &request_, sizeof(request_), &bytes, &io_);
    if (!ok && GetLastError() == ERROR_IO_PENDING) { pending_ = true; return true; }
    if (!ok || bytes != (read ? sizeof(reply_) : sizeof(request_))) { Reset(); return false; }
    return true;
  }
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  OVERLAPPED io_{};
  bool pending_ = false;
  unsigned phase_ = 0;
  BrokerRequest request_;
  BrokerReply reply_;
};
}
