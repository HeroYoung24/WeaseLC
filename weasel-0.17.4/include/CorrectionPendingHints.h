#pragma once
#include "CorrectionFlowIdentity.h"
#include "CorrectionHintLifetime.h"
#include <CorrectionModel.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

namespace correction {
namespace input_flow {
// Metadata only: the existing controller continues to own raw text, TSF ranges,
// spelling validation, model thresholds and the sole write/verification path.
struct PendingHint {
  HintIdentity identity;
  std::uint64_t session = 0, modelRevision = 0, sourceVersion = 0;
  std::uint32_t start = 0, originalUnits = 0, replacementUnits = 0;
};

class PendingHints {
 public:
  static constexpr std::size_t kCapacity = 5;
  static constexpr std::size_t kProcessedCapacity = 64;
  enum class Admission { Added, ReplacedLater, Duplicate, Conflict, Full, Processed, Closed, Busy, Invalid };
  enum class Outcome { Accepted, Rejected, Expired, Invalidated, CapacityEvicted };

  // Begin on a fresh instance only. An epoch cannot be recycled within a live
  // context just to evade processed marks; genuine scope/config change resets it.
  bool Begin(std::uint64_t session, std::uint64_t epoch, std::uint64_t modelRevision,
             std::uint64_t sourceVersion, std::size_t sourceUnits, const HintTimingOptions& options) {
    if (begun_ || !session || !epoch || !modelRevision || !sourceVersion ||
        !sourceUnits || sourceUnits > kMaxTargetUnits || !ValidHintTiming(options)) return false;
    session_ = session; epoch_ = epoch; modelRevision_ = modelRevision;
    sourceVersion_ = sourceVersion; sourceUnits_ = sourceUnits; options_ = options;
    begun_ = sourceReady_ = true;
    return true;
  }

  Admission Add(const PendingHint& hint, std::uint64_t nowMs) {
    if (applying_) return Admission::Busy;
    Sweep(nowMs);
    if (!Valid(hint) || !sourceReady_) return Admission::Invalid;
    if (Processed(hint.identity)) return Admission::Processed;
    if (closed_) return Admission::Closed;
    if (const auto existing = Find(hint.identity)) {
      const auto& entry = entries_[*existing];
      return entry.validated && SameLocation(entry.hint, hint) ? Admission::Duplicate : Admission::Conflict;
    }
    if (OverlapsOther(hint)) return Admission::Conflict;
    Entry next;
    next.hint = hint; next.validated = true;
    if (!next.lifetime.Begin(nowMs, options_)) return Admission::Invalid;
    bool replaced = false;
    if (count_ == kCapacity) {
      // Keep a visible/current hint stable and the earliest waiting ranges.
      std::optional<std::size_t> farthest;
      for (std::size_t i = 0; i < count_; ++i)
        if (!IsActive(entries_[i].hint.identity) &&
            (!farthest || entries_[i].hint.start > entries_[*farthest].hint.start)) farthest = i;
      if (!farthest || hint.start >= entries_[*farthest].hint.start) return Admission::Full;
      // Remember dropped identity too: repeated answers must not recreate it
      // with a fresh lifetime. Eviction is explicitly not negative feedback.
      Remove(*farthest, Outcome::CapacityEvicted, nowMs);
      if (closed_) return Admission::Closed;
      replaced = true;
    }
    entries_[count_++] = next;
    Sort();
    return replaced ? Admission::ReplacedLater : Admission::Added;
  }

  // Must be called before any presentation/action after a source change. Caller
  // supplies a strictly newer LOCAL snapshot version, not host pointer arithmetic.
  bool SourceChanged(std::uint64_t version, std::size_t units, std::uint64_t nowMs) {
    if (applying_) { sourceReady_ = false; return false; }
    Suspend(nowMs);
    if (!begun_ || version <= sourceVersion_ || !units || units > kMaxTargetUnits) return false;
    sourceVersion_ = version; sourceUnits_ = units; sourceReady_ = true;
    return true;
  }

  // The caller has just re-read spelling + relevant context under a valid host
  // lock. No automatic coordinate shifting or phrase search is done here.
  bool Revalidate(const PendingHint& hint, std::uint64_t nowMs) {
    if (applying_) return false;
    Sweep(nowMs);
    const auto at = Find(hint.identity);
    if (!sourceReady_ || !Valid(hint) || !at || OverlapsOther(hint)) return false;
    entries_[*at].hint = hint; entries_[*at].validated = true;
    Sort();  // Age/accumulated visibility and stable active identity are retained.
    return true;
  }

  // Call only when key-cycle/transaction gates permit promotion. Returned value
  // is a proposal, NOT authorization to consume Tab or write host text.
  std::optional<PendingHint> Select(std::uint64_t nowMs, bool advanceAllowed) {
    if (applying_) return std::nullopt;
    Sweep(nowMs);
    if (!sourceReady_) return std::nullopt;
    if (active_) {
      const auto at = Find(activeIdentity_);
      if (at && entries_[*at].validated) return entries_[*at].hint;
      return std::nullopt;  // Revalidate/drop the old active item first.
    }
    if (!advanceAllowed) return std::nullopt;
    for (std::size_t i = 0; i < count_;) {
      if (!entries_[i].validated) { ++i; continue; }
      if (!entries_[i].lifetime.CanReveal(nowMs)) { Remove(i, Outcome::Expired, nowMs); continue; }
      active_ = true; activeIdentity_ = entries_[i].hint.identity;
      return entries_[i].hint;
    }
    return std::nullopt;
  }

  // Report actual window visibility only after the existing host checks/anchor
  // selection. Hiding for layout does not finish a still-safe suggestion.
  bool Visibility(const HintIdentity& identity, bool visible, std::uint64_t nowMs) {
    if (applying_) return false;
    Sweep(nowMs);
    const auto at = Find(identity);
    if (!at || !IsActive(identity) || (visible && (!sourceReady_ || !entries_[*at].validated))) return false;
    if (entries_[*at].lifetime.SetVisible(visible, nowMs)) return true;
    Remove(*at, Outcome::Expired, nowMs);
    return false;
  }

  // Freeze expiry/promotion while the existing host executes its multi-lock
  // write/confirmation. This is bookkeeping, not permission to write TSF text.
  bool BeginWrite(const HintIdentity& identity, std::uint64_t nowMs) {
    Sweep(nowMs);
    const auto at = Find(identity);
    if (applying_ || !at || !IsActive(identity) || !sourceReady_ || !entries_[*at].validated) return false;
    applying_ = true;
    return true;
  }

  // Accepted is allowed only AFTER the existing independent write-confirmation
  // step. Uncertain writes must suspend/invalidate, never call Accepted/retry.
  bool Finish(const HintIdentity& identity, Outcome outcome, std::uint64_t nowMs) {
    const auto at = Find(identity);
    if (!at || ((outcome == Outcome::Accepted || outcome == Outcome::Rejected) && !IsActive(identity))) return false;
    if (applying_ && !IsActive(identity)) return false;
    if (outcome == Outcome::Accepted && !applying_) return false;
    if (applying_ && outcome != Outcome::Accepted && outcome != Outcome::Invalidated) return false;
    const bool wrote = applying_;
    applying_ = false;
    Remove(*at, outcome, nowMs);
    if (wrote) Suspend(nowMs);
    return true;
  }

  void Sweep(std::uint64_t nowMs) {
    if (applying_) return;
    for (std::size_t i = 0; i < count_;)
      if (entries_[i].lifetime.Expired(nowMs)) Remove(i, Outcome::Expired, nowMs); else ++i;
  }
  std::size_t Size() const { return count_; }
  bool CanShow(const HintIdentity& identity,std::uint64_t nowMs) const {
    const auto at=Find(identity);
    return !applying_ && sourceReady_ && at && IsActive(identity) && entries_[*at].validated &&
        entries_[*at].lifetime.CanReveal(nowMs);
  }
  bool Contains(const HintIdentity& identity) const { return Find(identity).has_value(); }
  bool AdmissionClosed() const { return closed_; }
  bool WritePending() const { return applying_; }
  void ResetForNewScope() { *this = PendingHints{}; }

 private:
  struct Entry { PendingHint hint; HintLifetime lifetime; bool validated = false; };
  struct Mark { HintIdentity identity; Outcome outcome = Outcome::Invalidated; };
  bool Valid(const PendingHint& hint) const {
    return begun_ && hint.identity.Valid() && hint.identity.editingEpoch == epoch_ &&
        hint.session == session_ && hint.modelRevision == modelRevision_ && hint.sourceVersion == sourceVersion_ &&
        hint.originalUnits && hint.originalUnits <= 32 && hint.replacementUnits && hint.replacementUnits <= 32 &&
        hint.start <= sourceUnits_ && hint.originalUnits <= sourceUnits_ - hint.start;
  }
  static bool SameLocation(const PendingHint& a, const PendingHint& b) {
    return a.sourceVersion == b.sourceVersion && a.start == b.start &&
        a.originalUnits == b.originalUnits && a.replacementUnits == b.replacementUnits;
  }
  bool IsActive(const HintIdentity& identity) const { return active_ && activeIdentity_ == identity; }
  std::optional<std::size_t> Find(const HintIdentity& identity) const {
    for (std::size_t i = 0; i < count_; ++i) if (entries_[i].hint.identity == identity) return i;
    return std::nullopt;
  }
  bool Processed(const HintIdentity& identity) const {
    for (std::size_t i = 0; i < processedCount_; ++i) if (processed_[i].identity == identity) return true;
    return false;
  }
  bool OverlapsOther(const PendingHint& hint) const {
    for (std::size_t i = 0; i < count_; ++i) {
      const auto& other = entries_[i];
      if (!other.validated || other.hint.identity == hint.identity) continue;
      if (hint.start < other.hint.start + other.hint.originalUnits &&
          other.hint.start < hint.start + hint.originalUnits) return true;
    }
    return false;
  }
  void Suspend(std::uint64_t nowMs) {
    sourceReady_ = false;
    for (std::size_t i = 0; i < count_; ++i) {
      entries_[i].lifetime.SetVisible(false, nowMs);
      entries_[i].validated = false;
    }
  }
  void Remove(std::size_t index, Outcome outcome, std::uint64_t nowMs) {
    const auto identity = entries_[index].hint.identity;
    entries_[index].lifetime.End(nowMs);
    if (!Processed(identity)) {
      if (processedCount_ < kProcessedCapacity) processed_[processedCount_++] = Mark{identity, outcome};
      // Do not evict a rejection mark and silently resurrect it. Saturation
      // closes new admissions for this scope; existing items can still finish.
      if (processedCount_ == kProcessedCapacity) closed_ = true;
    }
    Erase(index);
  }
  void Erase(std::size_t index) {
    if (IsActive(entries_[index].hint.identity)) { active_ = false; activeIdentity_ = {}; }
    for (std::size_t i = index + 1; i < count_; ++i) entries_[i - 1] = entries_[i];
    entries_[--count_] = Entry{};
  }
  void Sort() {
    std::sort(entries_.begin(), entries_.begin() + count_, [](const Entry& a, const Entry& b) {
      return a.hint.start < b.hint.start;
    });
  }
  std::array<Entry, kCapacity> entries_{};
  std::array<Mark, kProcessedCapacity> processed_{};
  std::size_t count_ = 0, processedCount_ = 0, sourceUnits_ = 0;
  std::uint64_t session_ = 0, epoch_ = 0, modelRevision_ = 0, sourceVersion_ = 0;
  HintTimingOptions options_;
  HintIdentity activeIdentity_;
  bool begun_ = false, sourceReady_ = false, active_ = false, closed_ = false, applying_ = false;
};
}  // namespace input_flow
}  // namespace correction
