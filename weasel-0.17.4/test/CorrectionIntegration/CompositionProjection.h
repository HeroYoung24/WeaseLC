#pragma once

// Independent behavioral model, not a Chromium implementation or a real app.
// While a composition keeps the same start across a lock, only its visible
// preedit is synchronized. Committed-prefix changes in the TSF cache can vanish.
// Every discarded cache change is reported back through normal host events.
class CompositionProjection {
 public:
  CompositionProjection(MemoryTextStore& store, ITfContext* context)
      : store_(store), context_(context), visible_(store.text) {
    valid_ = Composition(start_, length_);
    Arm();
  }
  ~CompositionProjection() { store_.onAfterLock = {}; }
  const std::wstring& Visible() const { return visible_; }
  bool Valid() const { return valid_; }
 private:
  bool Composition(LONG& start, LONG& length) {
    start = -1; length = 0;
    ComPtr<ITfContextComposition> compositions;
    ComPtr<IEnumITfCompositionView> items;
    ComPtr<ITfCompositionView> item;
    if (context_->QueryInterface(IID_PPV_ARGS(&compositions)) != S_OK ||
        compositions->EnumCompositions(&items) != S_OK || !items) return false;
    const auto next = items->Next(1, &item, nullptr);
    if (next == S_FALSE) return true;
    ComPtr<ITfRange> range;
    ComPtr<ITfRangeACP> acp;
    return next == S_OK && item && item->GetRange(&range) == S_OK && range &&
        range.As(&acp) == S_OK && acp->GetExtent(&start, &length) == S_OK;
  }
  void Arm() { store_.onAfterLock = [this]() { Synchronize(); Arm(); }; }
  void Synchronize() {
    LONG start = -1, length = 0;
    if (!Composition(start, length)) { valid_ = false; return; }
    if (start >= 0 && start == start_ && start_ + length_ <= static_cast<LONG>(visible_.size()) &&
        start + length <= static_cast<LONG>(store_.text.size()))
      visible_.replace(start_, length_, store_.text.substr(start, length));
    else visible_ = store_.text;
    start_ = start; length_ = length;
    if (visible_ == store_.text) return;
    // Notify the smallest changed span, retaining selection and TSF anchors.
    const auto caret = store_.SelectionEnd();
    size_t first = 0, oldEnd = store_.text.size(), newEnd = visible_.size();
    while (first < oldEnd && first < newEnd && store_.text[first] == visible_[first]) ++first;
    while (oldEnd > first && newEnd > first && store_.text[oldEnd-1] == visible_[newEnd-1]) { --oldEnd; --newEnd; }
    valid_ = store_.UserReplace(static_cast<LONG>(first), static_cast<LONG>(oldEnd),
        visible_.substr(first, newEnd-first)) == S_OK && valid_;
    if (caret <= static_cast<LONG>(store_.text.size()))
      valid_ = store_.UserSelect(caret, caret) == S_OK && valid_;
  }
  MemoryTextStore& store_;
  ITfContext* context_;  // borrowed; this model is destroyed before its document
  std::wstring visible_;
  LONG start_ = -1, length_ = 0;
  bool valid_ = true;
};
