#pragma once
#include <CorrectionModel.h>
#include <optional>
#include <cwctype>

namespace correction {
// One event-thread owner. At most one running request and one latest pending
// snapshot. Never concatenate commits: their host positions may be unrelated.
class RequestQueue {
 public:
  struct Work {
    CheckRequest request;
    std::shared_ptr<std::atomic_bool> cancelled;
  };
  explicit RequestQueue(std::uint64_t debounceMs = 250, std::uint64_t maxAgeMs = 10000)
      : debounce_(debounceMs), maxAge_(maxAgeMs) {}
  ~RequestQueue() { Cancel(); }
  RequestQueue(const RequestQueue&) = delete;
  RequestQueue& operator=(const RequestQueue&) = delete;
  void Cancel() {
    ++revision_; pending_.reset();
    if (running_) running_->work.cancelled->store(true);
  }
  void Submit(CheckRequest request, std::uint64_t now) {
    Cancel();
    if (!request.safeText || request.text.empty() || request.text.size() > kMaxTargetUnits ||
        request.before.size() > kMaxNearbyUnits || request.after.size() > kMaxNearbyUnits ||
        request.text.find(L'\0') != std::wstring::npos) return;
    bool meaningful = false;
    for (wchar_t c : request.text) if (!iswspace(c)) { meaningful = true; break; }
    if (!meaningful) return;
    pending_ = Pending{std::move(request), now};
  }
  std::optional<Work> TakeReady(std::uint64_t now) {
    if (running_ || !pending_) return {};
    if (now < pending_->time || now - pending_->time >= maxAge_) { pending_.reset(); return {}; }
    if (now - pending_->time < debounce_) return {};
    Work work{std::move(pending_->request), std::make_shared<std::atomic_bool>(false)};
    running_ = Running{work, pending_->time, revision_}; pending_.reset();
    return work;
  }
  // Completion is consumed once, even when stale, before the next work starts.
  bool Finish(const Work& work, const CheckResult& result, std::uint64_t now) {
    if (!running_ || work.cancelled != running_->work.cancelled) return false;
    const bool current = running_->revision == revision_ &&
        !running_->work.cancelled->load() && now >= running_->time &&
        now - running_->time < maxAge_ && result.session == running_->work.request.session &&
        result.version == running_->work.request.version;
    running_.reset(); return current;
  }
 private:
  struct Pending { CheckRequest request; std::uint64_t time; };
  struct Running { Work work; std::uint64_t time, revision; };
  std::uint64_t debounce_, maxAge_, revision_ = 0;
  std::optional<Pending> pending_;
  std::optional<Running> running_;
};
}  // namespace correction
