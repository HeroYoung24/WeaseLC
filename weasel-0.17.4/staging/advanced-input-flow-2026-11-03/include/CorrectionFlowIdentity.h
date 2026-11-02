#pragma once
#include <cstdint>

// Inactive review code. Local identity, never an IPC pointer or text hash.
namespace correction {
namespace input_flow {
struct HintIdentity {
  std::uint64_t editingEpoch = 0;
  std::uint64_t occurrence = 0;  // Stable occurrence within the bounded source.
  std::uint64_t meaning = 0;     // Changes when pair/relevant semantics change.
  bool Valid() const { return editingEpoch && occurrence && meaning; }
};
inline bool operator==(const HintIdentity& a, const HintIdentity& b) {
  return a.editingEpoch == b.editingEpoch && a.occurrence == b.occurrence && a.meaning == b.meaning;
}
inline bool operator!=(const HintIdentity& a, const HintIdentity& b) { return !(a == b); }
}  // namespace input_flow
}  // namespace correction
