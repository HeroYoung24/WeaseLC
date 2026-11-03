#pragma once
#include <CorrectionModel.h>

// Shared product rule. Extraction does not extend normalization semantics.
namespace correction {
namespace input_flow {

inline std::wstring InferenceTarget(const CheckRequest& request) {
  if (request.source != Source::CommittedText || request.text.empty()) return request.text;
  const auto lastWord = request.text.find_last_not_of(L"。，.,");
  // Preserve prefix offsets and the last boundary mark. Manual input, internal
  // punctuation and all-punctuation input are unchanged, as in the baseline.
  if (lastWord != std::wstring::npos && request.text.size() - lastWord - 1 > 1)
    return request.text.substr(0, lastWord + 1) + request.text.back();
  return request.text;
}

// Only an equivalence policy, NOT a cache, request subscription or write grant.
// Both inputs must already pass the existing request/scope checks. The caller
// supplies an apartment-local editing epoch and full model/settings revision;
// they must change on scope/session/config changes. A request sequence number
// must not be used as the editing epoch: equivalent appends may have new versions.
inline bool EquivalentAutomaticInputs(
    const CheckRequest& left, std::uint64_t leftEditingEpoch, std::uint64_t leftModelRevision,
    const CheckRequest& right, std::uint64_t rightEditingEpoch, std::uint64_t rightModelRevision) {
  if (!leftEditingEpoch || leftEditingEpoch != rightEditingEpoch ||
      !leftModelRevision || leftModelRevision != rightModelRevision ||
      !left.session || left.session != right.session ||
      left.source != Source::CommittedText || right.source != Source::CommittedText ||
      !left.safeText || !right.safeText || left.text.empty() || right.text.empty() ||
      left.text.size() > kMaxTargetUnits || right.text.size() > kMaxTargetUnits ||
      left.before.size() > kMaxNearbyUnits || right.before.size() > kMaxNearbyUnits ||
      left.after.size() > kMaxNearbyUnits || right.after.size() > kMaxNearbyUnits ||
      left.before != right.before || left.after != right.after) return false;
  return InferenceTarget(left) == InferenceTarget(right);
}

// Even when equivalent, a result retains its ORIGINAL session/version. Each
// recipient must be validated and mapped against its own raw source through
// the existing parser/protection/TSF path. Never copy original suggestion offsets
// to a different raw punctuation run or relabel an old result as a new version.

}  // namespace input_flow
}  // namespace correction
