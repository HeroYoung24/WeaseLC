#pragma once

class ForeignLanguageBarItem final : public ITfLangBarItem {
 public:
  STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfLangBarItem) return E_NOINTERFACE;
    *object = static_cast<ITfLangBarItem*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = --references_; if (!remaining) delete this; return remaining;
  }
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override {
    if (!info) return E_POINTER;
    *info = {}; info->guidItem = kInputModeItem; return S_OK;
  }
  STDMETHODIMP GetStatus(DWORD* status) override {
    if (!status) return E_POINTER; *status = 0; return S_OK;
  }
  STDMETHODIMP Show(BOOL) override { return S_OK; }
  STDMETHODIMP GetTooltipString(BSTR* text) override {
    if (!text) return E_POINTER; *text = SysAllocString(L"Foreign test item");
    return *text ? S_OK : E_OUTOFMEMORY;
  }
 private:
  ULONG references_ = 1;
};

class OwnershipThreadMgr final : public FailingThreadMgr, public ITfLangBarItemMgr {
 public:
  enum class Mode { Normal, AddFailure, IgnoredSuccess, ReplacedBeforeRemoval, RemoveFailure };
  explicit OwnershipThreadMgr(Mode mode) : FailingThreadMgr(Failure::ThreadSink), mode_(mode) {
    foreign_.Attach(new ForeignLanguageBarItem());
    if (mode == Mode::IgnoredSuccess) item_ = foreign_;
  }
  STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    if (iid != IID_ITfLangBarItemMgr) return FailingThreadMgr::QueryInterface(iid, object);
    *object = static_cast<ITfLangBarItemMgr*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return FailingThreadMgr::AddRef(); }
  STDMETHODIMP_(ULONG) Release() override { return FailingThreadMgr::Release(); }
  STDMETHODIMP GetItem(REFGUID guid, ITfLangBarItem** item) override {
    if (!item) return E_POINTER;
    *item = nullptr;
    ++queries;
    if (mode_ == Mode::ReplacedBeforeRemoval && queries >= 2) item_ = foreign_;
    if (!IsEqualGUID(guid, kInputModeItem) || !item_) return S_FALSE;
    return item_.CopyTo(item);
  }
  STDMETHODIMP AddItem(ITfLangBarItem* item) override {
    ++adds;
    if (mode_ == Mode::AddFailure) return E_FAIL;
    if (mode_ != Mode::IgnoredSuccess) item_ = item;
    return S_OK;
  }
  STDMETHODIMP RemoveItem(ITfLangBarItem*) override {
    ++removes;
    if (item_ == foreign_) ++foreignRemoves;
    if (mode_ == Mode::RemoveFailure) return E_FAIL;
    item_.Reset(); return S_OK;
  }
  bool ForeignStillPresent() const { return item_ == foreign_; }
  STDMETHODIMP EnumItems(IEnumTfLangBarItems**) override { return E_NOTIMPL; }
  STDMETHODIMP AdviseItemSink(ITfLangBarItemSink*, DWORD*, REFGUID) override { return E_NOTIMPL; }
  STDMETHODIMP UnadviseItemSink(DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP GetItemFloatingRect(DWORD, REFGUID, RECT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetItemsStatus(ULONG, const GUID*, DWORD*) override { return E_NOTIMPL; }
  STDMETHODIMP GetItemNum(ULONG*) override { return E_NOTIMPL; }
  STDMETHODIMP GetItems(ULONG, ITfLangBarItem**, TF_LANGBARITEMINFO*, DWORD*, ULONG*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP AdviseItemsSink(ULONG, ITfLangBarItemSink**, const GUID*, DWORD*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP UnadviseItemsSink(ULONG, DWORD*) override { return E_NOTIMPL; }
  unsigned queries = 0, adds = 0, removes = 0, foreignRemoves = 0;
 private:
  Mode mode_;
  ComPtr<ITfLangBarItem> foreign_, item_;
};

static bool TestLanguageBarOwnership(HMODULE module, ITfTextInputProcessorEx* service) {
  auto exercise = reinterpret_cast<HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfThreadMgr*)>(
      GetProcAddress(module, "WeaselProbeExerciseLanguageBar"));
  if (!exercise) return true;
  bool valid = true;
  using Mode = OwnershipThreadMgr::Mode;
  for (Mode mode : {Mode::Normal, Mode::AddFailure, Mode::IgnoredSuccess,
                    Mode::ReplacedBeforeRemoval, Mode::RemoveFailure}) {
    ComPtr<OwnershipThreadMgr> manager;
    manager.Attach(new OwnershipThreadMgr(mode));
    const HRESULT hr = exercise(service, manager.Get());
    const bool shouldInitialize = mode != Mode::AddFailure && mode != Mode::IgnoredSuccess;
    const bool shouldRemove = mode == Mode::Normal || mode == Mode::RemoveFailure;
    const bool foreignPreserved = (mode != Mode::IgnoredSuccess &&
        mode != Mode::ReplacedBeforeRemoval) || manager->ForeignStillPresent();
    const bool scenarioValid = hr == (shouldInitialize ? S_OK : E_FAIL) &&
        manager->adds == 1 && manager->removes == (shouldRemove ? 1U : 0U) &&
        manager->foreignRemoves == 0 && foreignPreserved && manager->References() == 1;
    std::wcout << L"Language bar ownership scenario " << static_cast<int>(mode)
               << L" valid = " << scenarioValid << L'\n';
    valid = valid && scenarioValid;
  }
  return valid;
}
