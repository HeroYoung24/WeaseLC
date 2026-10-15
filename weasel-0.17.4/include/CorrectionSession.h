#pragma once
#include <CorrectionCapture.h>
#include <CorrectionModel.h>
#include <optional>

namespace correction {
// Owned by the UI/input event thread. Workers receive immutable request copies;
// completed results must return through this gate before any presentation.
class SessionGate {
 public:
  void SetEnabled(bool enabled) { enabled_ = enabled; active_ = false; }
  std::optional<CheckRequest> Observe(const CommitCapture& event) {
    if (event.abi != kCommitCaptureVersion || event.sequence <= sequence_) return {};
    sequence_ = event.sequence; generation_ = event.generation; active_ = false;
    if (!enabled_ || event.kind != CaptureKind::Commit || !event.units ||
        event.units > kMaxTargetUnits || event.text[event.units] != 0) return {};
    CheckRequest request;
    request.session = generation_; request.version = sequence_;
    request.source = Source::CommittedText; request.safeText = true;
    request.text.assign(event.text, event.units);
    if (request.text.find(L'\0') != std::wstring::npos) return {};
    active_ = true;
    return request;
  }
  bool IsCurrent(const CheckResult& result) const {
    return enabled_ && active_ && result.session == generation_ && result.version == sequence_;
  }
 private:
  bool enabled_ = false, active_ = false;
  ULONGLONG sequence_ = 0, generation_ = 0;
};
}  // namespace correction
