#pragma once
#include "CorrectionBrokerProcess.h"
#include <array>
#include <mutex>
#include <thread>

namespace correction {
enum class BrokerProcessStage { Stopped, Starting, Running, Backoff, Exhausted };
struct BrokerProcessStatus {
  BrokerProcessStage stage = BrokerProcessStage::Stopped;
  DWORD processId = 0, attempts = 0, consecutiveFailures = 0;
  DWORD lastExitCode = 0;
  bool launchFailed = false;
};
struct BrokerRestartPolicy {
  std::array<DWORD, 3> delays{250, 1000, 3000};
  DWORD stableMilliseconds = 60000;
};
// Start/Stop belong to the backend owner thread. Only the worker touches its
// BrokerProcess/handles; Snapshot is safe from any thread. No host COM, text,
// settings, UI callbacks or failed requests are retained/replayed here.
class BrokerSupervisor {
 public:
  BrokerSupervisor() = default;
  BrokerSupervisor(const BrokerSupervisor&) = delete;
  BrokerSupervisor& operator=(const BrokerSupervisor&) = delete;
  ~BrokerSupervisor() { Stop(); }
  bool Start(const std::wstring& executable, BrokerRestartPolicy policy = {}) {
    if (worker_.joinable()) return false;
    if (executable.size() < 3 || executable[1] != L':' ||
        (executable[2] != L'\\' && executable[2] != L'/') ||
        !policy.stableMilliseconds || policy.stableMilliseconds > 3600000) return false;
    for (auto delay : policy.delays) if (!delay || delay > 60000) return false;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) return false;
    Publish({BrokerProcessStage::Starting});
    try {
      worker_ = std::thread([this, executable, policy]() {
        try { Run(executable, policy); }
        catch (...) {
          auto status = Snapshot(); status.stage = BrokerProcessStage::Exhausted;
          status.processId = 0; status.launchFailed = true; Publish(status);
        }
      });
    } catch (...) {
      CloseHandle(stop_); stop_ = nullptr; Publish({}); return false;
    }
    return true;  // Supervision started, not proof that the endpoint is ready.
  }
  void Stop() {
    if (stop_) SetEvent(stop_);
    if (worker_.joinable()) worker_.join();
    if (stop_) { CloseHandle(stop_); stop_ = nullptr; }
    auto status = Snapshot(); status.stage = BrokerProcessStage::Stopped;
    status.processId = 0; Publish(status);
  }
  BrokerProcessStatus Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_); return status_;
  }
 private:
  void Publish(const BrokerProcessStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_); status_ = status;
  }
  void Run(const std::wstring& executable, const BrokerRestartPolicy& policy) {
    BrokerProcessStatus status;
    while (WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT) {
      status.stage = BrokerProcessStage::Starting; status.processId = 0;
      if (status.attempts != MAXDWORD) ++status.attempts;
      Publish(status);
      BrokerProcess child;
      const bool launched = child.Start(executable);
      status.launchFailed = !launched;
      if (launched) {
        const auto started = GetTickCount64();
        status.stage = BrokerProcessStage::Running;
        status.processId = GetProcessId(child.Process()); Publish(status);
        HANDLE events[]{stop_, child.Process()};
        const auto result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) { child.Stop(); break; }
        if (result != WAIT_OBJECT_0 + 1) {
          child.Stop(); status.stage = BrokerProcessStage::Exhausted;
          status.processId = 0; Publish(status); return;
        }
        GetExitCodeProcess(child.Process(), &status.lastExitCode);
        if (GetTickCount64() - started >= policy.stableMilliseconds) status.consecutiveFailures = 0;
      }
      child.Stop(); status.processId = 0;
      ++status.consecutiveFailures;
      if (status.consecutiveFailures > policy.delays.size()) {
        status.stage = BrokerProcessStage::Exhausted; Publish(status); return;
      }
      status.stage = BrokerProcessStage::Backoff; Publish(status);
      if (WaitForSingleObject(stop_, policy.delays[status.consecutiveFailures - 1]) != WAIT_TIMEOUT) break;
    }
  }
  HANDLE stop_ = nullptr;
  std::thread worker_;
  mutable std::mutex mutex_;
  BrokerProcessStatus status_;
};
}
