#pragma once
#include <cstddef>
#include <utility>

namespace weasel {
// Tracks one complete response body. Empty/scalar responses cannot replay an
// earlier commit; consuming a body twice cannot enqueue the same commit twice.
class ResponseSlot {
 public:
  void Reset() { bytes_ = 0; }
  bool Complete(std::size_t bytes, std::size_t capacity) {
    bytes_ = bytes && bytes <= capacity && bytes % sizeof(wchar_t) == 0 ? bytes : 0;
    return bytes_ != 0;
  }
  std::size_t TakeUnits() { return std::exchange(bytes_, 0) / sizeof(wchar_t); }
 private:
  std::size_t bytes_ = 0;
};
}  // namespace weasel
