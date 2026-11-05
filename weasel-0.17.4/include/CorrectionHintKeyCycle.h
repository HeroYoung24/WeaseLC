#pragma once
#include "CorrectionFlowIdentity.h"

namespace correction {
namespace input_flow {
// One instance per shortcut (Tab / explicit non-composition Esc), owned by the
// apartment controller. This class never dispatches a key or edits host text.
class HintKeyCycle {
 public:
  enum class Decision { Pass, Consume, Attempt };

  // stroke is allocated by the input adapter for a physical down/up cycle,
  // shared by query/actual callbacks; auto-repeat MUST retain that stroke.
  Decision Query(std::uint64_t stroke, const HintIdentity& hint, bool eligible, bool repeat) {
    if (!stroke) return Decision::Pass;
    if (open_ && claimed_) return Decision::Consume;
    if (!open_ || stroke != stroke_) {
      if (repeat) return Decision::Pass;
      open_ = true; stroke_ = stroke; actual_ = false;
      offered_ = eligible && hint.Valid();
      hint_ = offered_ ? hint : HintIdentity{};
      claimed_ = offered_;
    }
    // An unoffered query cannot acquire a hint that appeared mid-cycle.
    return claimed_ ? Decision::Consume : Decision::Pass;
  }

  Decision Actual(std::uint64_t stroke, const HintIdentity& hint, bool stillEligible, bool repeat) {
    if (!open_) return Decision::Pass;
    if (stroke != stroke_) return claimed_ ? Decision::Consume : Decision::Pass;
    if (!claimed_) { actual_ = true; return Decision::Pass; }
    if (actual_ || repeat) return Decision::Consume;
    actual_ = true;  // Exactly one attempt even when the eventual write fails.
    if (!offered_ || !stillEligible || hint != hint_) return Decision::Consume;
    offered_ = false;
    return Decision::Attempt;
  }

  // Hosts may send an actual key without a preceding query. A matching
  // query, including one that declined the key, still owns its decision.
  Decision ActualWithOptionalQuery(std::uint64_t stroke, const HintIdentity& hint, bool eligible, bool repeat) {
    if (open_ && (stroke == stroke_ || claimed_ || repeat))
      return Actual(stroke, hint, eligible, repeat);
    if (open_) ResetAfterFocusLoss();  // An unclaimed earlier stroke has ended.
    if (!eligible) return Decision::Pass;
    if (Query(stroke, hint, eligible, repeat) == Decision::Pass) return Decision::Pass;
    return Actual(stroke, hint, eligible, repeat);
  }

  Decision Release(std::uint64_t stroke, bool query) {
    if (!open_) return Decision::Pass;
    if (stroke != stroke_) return claimed_ ? Decision::Consume : Decision::Pass;
    const auto decision = claimed_ ? Decision::Consume : Decision::Pass;
    if (!query) ResetAfterFocusLoss();
    return decision;
  }

  // Unsafe source / removed hint cancels the offer, not ownership of an already
  // claimed physical key. Its repeats and key-up must not leak to the host.
  void InvalidateOffer() { offered_ = false; hint_ = {}; }
  bool BlocksQueueAdvance() const { return open_ && claimed_; }
  void ResetAfterFocusLoss() {
    stroke_ = 0; hint_ = {}; open_ = offered_ = claimed_ = actual_ = false;
  }

 private:
  std::uint64_t stroke_ = 0;
  HintIdentity hint_;
  bool open_ = false, offered_ = false, claimed_ = false, actual_ = false;
};
}  // namespace input_flow
}  // namespace correction
