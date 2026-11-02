#pragma once
#include <algorithm>
#include <cstdint>

// REVIEW COPY ONLY. Not referenced by production projects or settings.
// Pure timing policy: no HWND, TSF, keyboard, model, registry or background work.
namespace correction {
namespace input_flow {

struct HintTimingOptions {
  std::uint32_t minimumVisibleMs = 3000;
  std::uint32_t maximumAgeMs = 15000;  // Includes time waiting in the queue.
};

inline bool ValidHintTiming(const HintTimingOptions& options) {
  return options.minimumVisibleMs >= 1000 && options.minimumVisibleMs <= 10000 &&
      options.maximumAgeMs >= 5000 && options.maximumAgeMs <= 60000 &&
      options.minimumVisibleMs <= options.maximumAgeMs;
}

enum class InputConfirmationStyle { ShortPhrases, LongPassages };
inline HintTimingOptions TimingReference(InputConfirmationStyle style) {
  HintTimingOptions options;
  if (style == InputConfirmationStyle::LongPassages) options.minimumVisibleMs = 5000;
  return options;  // A selectable reference, never an inferred user setting.
}

// A controller owns one instance per stable suggestion identity. Repeated
// responses must reuse that instance, not construct another to renew its age.
// nowMs is supplied by a monotonic clock in one apartment; tick zero is valid.
class HintLifetime {
 public:
  bool Begin(std::uint64_t nowMs, const HintTimingOptions& options) {
    if (begun_ || ended_ || !ValidHintTiming(options)) return false;
    options_ = options;
    born_ = last_ = nowMs;
    begun_ = true;
    return true;
  }

  bool Expired(std::uint64_t nowMs) const {
    return !begun_ || ended_ || nowMs < last_ || nowMs - born_ >= options_.maximumAgeMs;
  }

  std::uint64_t RemainingMs(std::uint64_t nowMs) const {
    return Expired(nowMs) ? 0 : options_.maximumAgeMs - (nowMs - born_);
  }

  std::uint64_t VisibleMs(std::uint64_t nowMs) const {
    if (!begun_ || nowMs < last_) return visibleMs_;
    const auto age = nowMs - born_;
    const auto boundedAge = (std::min)(age, std::uint64_t(options_.maximumAgeMs));
    const auto lastAge = last_ - born_;
    const auto extra = visible_ && boundedAge > lastAge ? boundedAge - lastAge : 0;
    return (std::min)(std::uint64_t(options_.maximumAgeMs), visibleMs_ + extra);
  }

  bool MinimumSatisfied(std::uint64_t nowMs) const {
    return begun_ && VisibleMs(nowMs) >= options_.minimumVisibleMs;
  }

  // Suppress a near-expiry first reveal/resume rather than flashing a hint.
  // A hidden interval does not consume the remaining visible-time minimum.
  bool CanReveal(std::uint64_t nowMs) const {
    if (Expired(nowMs)) return false;
    const auto visible = VisibleMs(nowMs);
    const auto needed = visible >= options_.minimumVisibleMs ? 0 : options_.minimumVisibleMs - visible;
    return RemainingMs(nowMs) >= needed;
  }

  // Call after the host confirms actual visibility. Hide on lost layout; source
  // invalidation is terminal and must instead call End(). False never grants
  // permission to show, consume Tab or write text. The host owns those checks.
  bool SetVisible(bool visible, std::uint64_t nowMs) {
    if (Expired(nowMs)) { End(nowMs); return false; }
    if (visible && !visible_ && !CanReveal(nowMs)) { End(nowMs); return false; }
    visibleMs_ = VisibleMs(nowMs);
    last_ = nowMs;
    visible_ = visible;
    return true;
  }

  // Explicit acceptance/rejection or unsafe/stale source overrides any minimum.
  // The controller must remove expired entries and remember processed identities;
  // this clock deliberately contains neither a queue nor a rejection database.
  void End(std::uint64_t nowMs) {
    if (begun_ && !ended_ && nowMs >= last_) {
      visibleMs_ = VisibleMs(nowMs);
      last_ = nowMs;
    }
    visible_ = false;
    ended_ = true;
  }

 private:
  HintTimingOptions options_;
  std::uint64_t born_ = 0, last_ = 0, visibleMs_ = 0;
  bool begun_ = false, ended_ = false, visible_ = false;
};

}  // namespace input_flow
}  // namespace correction
