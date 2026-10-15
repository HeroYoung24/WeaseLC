#pragma once
#include <CorrectionModel.h>

namespace correction {
// The controller owns this on its event thread. An action always checks the
// exact snapshot and version again; no offsets are reused after an edit.
class SuggestionReview {
 public:
  void Clear() { request_ = {}; items_.clear(); }
  bool Load(const CheckRequest& request, const CheckResult& result) {
    Clear();
    if (!request.safeText || request.text.empty() || request.text.size() > kMaxTargetUnits ||
        result.state != State::Suggestions || result.session != request.session ||
        result.version != request.version || result.suggestions.empty() || result.suggestions.size() > kMaxSuggestions) return false;
    std::vector<Suggestion> checked;
    for (const auto& item : result.suggestions) {
      if (item.original.empty() || item.original == item.replacement || item.replacement.size() > 256 ||
          item.start > request.text.size() || item.original.size() > request.text.size() - item.start ||
          request.text.compare(item.start, item.original.size(), item.original) != 0) return false;
      for (const auto& other : checked)
        if (item.start < other.start + other.original.size() && other.start < item.start + item.original.size()) {
          Clear(); return false;
        }
      checked.push_back(item);
    }
    request_ = request; items_ = std::move(checked); return true;
  }
  const std::vector<Suggestion>& Items() const { return items_; }
  bool Current(const std::wstring& text, std::uint64_t version) const {
    return !items_.empty() && request_.version == version && request_.text == text;
  }
  const Suggestion* Selected(std::size_t index, const std::wstring& text, std::uint64_t version) const {
    return Current(text, version) && index < items_.size() ? &items_[index] : nullptr;
  }
  bool Apply(std::size_t index, const std::wstring& text, std::uint64_t version, std::wstring& output) const {
    const auto* item = Selected(index, text, version);
    if (!item || text.size() - item->original.size() + item->replacement.size() > kMaxTargetUnits) return false;
    output = text; output.replace(item->start, item->original.size(), item->replacement); return true;
  }
  bool Ignore(std::size_t index, const std::wstring& text, std::uint64_t version) {
    if (!Selected(index, text, version)) return false;
    items_.erase(items_.begin() + index); return true;
  }
 private:
  CheckRequest request_;
  std::vector<Suggestion> items_;
};
}  // namespace correction
