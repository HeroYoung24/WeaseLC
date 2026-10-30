#pragma once

// A focused model of Chromium's no-live-composition SetText commit branch:
// CommitTextAndEndCompositionIfAny deletes from the renderer caret back to the
// first replacement, then inserts the supplied replacement. It is NOT Edge.
// Keep visible text independent of the TSF cache to expose a false readback.
// Reference: chromium/src/ui/base/ime/win/tsf_text_store.cc, RequestLock and
// CommitTextAndEndCompositionIfAny (source inspected 2026-10-30).
class CaretReplacementProjection {
 public:
  CaretReplacementProjection(MemoryTextStore& store, ITfContext* context)
      : store_(store), context_(context), visible_(store.text) { Arm(); }
  ~CaretReplacementProjection() { store_.onBeforeLock = {}; store_.onTextWritten = {}; store_.onAfterLock = {}; }
  const std::wstring& Visible() const { return visible_; }
  bool Valid() const { return valid_; }
 private:
  bool Composing() {
    ComPtr<ITfContextComposition> compositions; ComPtr<IEnumITfCompositionView> items;
    ComPtr<ITfCompositionView> item;
    if (context_->QueryInterface(IID_PPV_ARGS(&compositions)) != S_OK ||
        compositions->EnumCompositions(&items) != S_OK) { valid_ = false; return false; }
    return items->Next(1,&item,nullptr) == S_OK;
  }
  void Arm() {
    store_.onBeforeLock = [this] {
      composing_ = Composing(); caret_ = store_.SelectionEnd(); wrote_ = false;
    };
    store_.onTextWritten = [this](LONG start, LONG, const std::wstring& text) {
      if (wrote_) { valid_ = false; return; }
      start_ = start; replacement_ = text; wrote_ = true;
    };
    store_.onAfterLock = [this] {
      const bool composing = Composing();
      if (wrote_ && !composing_ && !composing) {
        if (start_ < 0 || caret_ < start_ || caret_ > static_cast<LONG>(visible_.size())) valid_ = false;
        else visible_.replace(start_,caret_-start_,replacement_);
      } else if (visible_ == before_) visible_ = store_.text;
      before_ = store_.text;
      Arm();
    };
    before_ = store_.text;
  }
  MemoryTextStore& store_;
  ITfContext* context_;
  std::wstring visible_, before_, replacement_;
  LONG caret_ = 0, start_ = 0;
  bool composing_ = false, wrote_ = false, valid_ = true;
};
