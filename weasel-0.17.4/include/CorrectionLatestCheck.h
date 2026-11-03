#pragma once
#include "CorrectionInferenceView.h"
#include <optional>
#include <utility>

namespace correction {
namespace input_flow {
struct CheckSchedulingOptions {
  std::uint32_t debounceMs = 250;
  std::uint32_t maximumWaitMs = 1000;
};
inline bool ValidCheckScheduling(const CheckSchedulingOptions& options) {
  return options.debounceMs >= 100 && options.debounceMs <= 2000 &&
      options.maximumWaitMs >= options.debounceMs && options.maximumWaitMs <= 5000;
}
struct CheckWork {
  CheckRequest snapshot;
  std::uint64_t serial = 0, firstQueuedMs = 0, lastSemanticChangeMs = 0;
};

// Scheduling policy, not another model executor. One in-flight snapshot plus one
// latest pending snapshot, bounded using existing limits; no thread, timer,
// cancellation, transport, result cache, persistence or TSF object is created.
class LatestCheck {
 public:
  enum class Submission { Latest, EquivalentPending, EquivalentInFlight, Invalid };
  struct Completion { bool matched = false; std::optional<CheckWork> remapRecipient; };

  bool Begin(std::uint64_t session, std::uint64_t editingEpoch, std::uint64_t modelRevision,
             const CheckSchedulingOptions& options) {
    if (begun_ || !session || !editingEpoch || !modelRevision || !ValidCheckScheduling(options)) return false;
    session_ = session; epoch_ = editingEpoch; revision_ = modelRevision;
    options_ = options; begun_ = true;
    return true;
  }

  // Use only after existing UTF-16/request/safe-scope validation. A new serial
  // identifies a real new snapshot, not an idle timer retry. Source/config changes
  // require canceling the old transport and rebuilding the scope externally.
  Submission Submit(const CheckRequest& snapshot, std::uint64_t serial, std::uint64_t nowMs) {
    if (!Observe(nowMs) || !Valid(snapshot) || !serial || serial <= lastSerial_) return Submission::Invalid;
    lastSerial_ = serial;
    const bool samePending = pending_ && Equivalent(pending_->snapshot, snapshot);
    const bool sameInFlight = inFlight_ && Equivalent(inFlight_->snapshot, snapshot);
    CheckWork next;
    next.snapshot = snapshot; next.serial = serial;
    next.firstQueuedMs = pending_ ? pending_->firstQueuedMs : nowMs;
    next.lastSemanticChangeMs = samePending ? pending_->lastSemanticChangeMs : nowMs;
    // Replacing only the latest payload preserves a finite scheduling deadline.
    // Equivalent punctuation does not restart the semantic debounce clock.
    pending_ = std::move(next);
    return sameInFlight ? Submission::EquivalentInFlight :
        samePending ? Submission::EquivalentPending : Submission::Latest;
  }

  std::optional<CheckWork> Take(std::uint64_t nowMs) {
    if (!Observe(nowMs) || inFlight_ || !pending_) return std::nullopt;
    if (nowMs - pending_->lastSemanticChangeMs < options_.debounceMs &&
        nowMs - pending_->firstQueuedMs < options_.maximumWaitMs) return std::nullopt;
    inFlight_ = std::move(pending_); pending_.reset();
    return inFlight_;
  }

  // allowRemap is true ONLY for an on-time, successful raw answer belonging to
  // this exact job/config. It does not mean old parsed offsets may be reused.
  // The adapter must parse/protect/map the raw answer afresh for remapRecipient,
  // preserving its own session/version, and validate the current host source.
  Completion Complete(std::uint64_t serial, bool allowRemap, std::uint64_t nowMs) {
    Completion completion;
    if (!Observe(nowMs) || !inFlight_ || inFlight_->serial != serial) return completion;
    completion.matched = true;
    if (allowRemap && pending_ && Equivalent(inFlight_->snapshot, pending_->snapshot)) {
      completion.remapRecipient = std::move(pending_);
      pending_.reset();
    }
    inFlight_.reset();
    // On failure, pending work remains only if a genuine Submit created it;
    // there is no automatic retry insertion and failure is never no_change.
    return completion;
  }

  bool HasInFlight() const { return inFlight_.has_value(); }
  bool HasPending() const { return pending_.has_value(); }
  bool Closed() const { return closed_; }
  // The adapter MUST cancel/drain any old transport and reject late replies;
  // destroying these snapshots does not itself cancel model execution.
  void ResetForNewScope() { *this = LatestCheck{}; }

 private:
  bool Valid(const CheckRequest& request) const {
    return request.safeText && request.source == Source::CommittedText &&
        request.session == session_ && request.version && !request.text.empty() &&
        request.text.size() <= kMaxTargetUnits && request.before.size() <= kMaxNearbyUnits &&
        request.after.size() <= kMaxNearbyUnits;
  }
  bool Equivalent(const CheckRequest& a, const CheckRequest& b) const {
    return EquivalentAutomaticInputs(a, epoch_, revision_, b, epoch_, revision_);
  }
  bool Observe(std::uint64_t nowMs) {
    if (!begun_ || closed_) return false;
    if (observed_ && nowMs < lastNow_) {
      closed_ = true; pending_.reset(); inFlight_.reset();
      return false;  // Adapter also cancels/drains transport; fail closed.
    }
    observed_ = true; lastNow_ = nowMs;
    return true;
  }
  std::optional<CheckWork> pending_, inFlight_;
  CheckSchedulingOptions options_;
  std::uint64_t session_ = 0, epoch_ = 0, revision_ = 0, lastSerial_ = 0, lastNow_ = 0;
  bool begun_ = false, observed_ = false, closed_ = false;
};
}  // namespace input_flow
}  // namespace correction
