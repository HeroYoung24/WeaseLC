#pragma once

// Early activation failure injection only. No system TSF manager or IPC session.
class FailingThreadMgr : public ITfThreadMgr,
                               public ITfSource,
                               public ITfKeystrokeMgr {
 public:
  enum class Failure { ThreadSink, KeyInterface, KeySink };
  explicit FailingThreadMgr(Failure failure) : failure_(failure) {}
  virtual ~FailingThreadMgr() = default;

  STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfThreadMgr)
      *object = static_cast<ITfThreadMgr*>(this);
    else if (iid == IID_ITfSource)
      *object = static_cast<ITfSource*>(this);
    else if (iid == IID_ITfKeystrokeMgr && failure_ != Failure::KeyInterface)
      *object = static_cast<ITfKeystrokeMgr*>(this);
    else
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = --references_;
    if (!remaining) delete this;
    return remaining;
  }

  STDMETHODIMP AdviseSink(REFIID iid, IUnknown* sink, DWORD* cookie) override {
    ++threadAdvise;
    if (iid != IID_ITfThreadMgrEventSink || !sink || !cookie)
      return E_INVALIDARG;
    if (failure_ == Failure::ThreadSink) return E_FAIL;
    threadSink_ = sink;
    *cookie = 1;
    return S_OK;
  }
  STDMETHODIMP UnadviseSink(DWORD cookie) override {
    ++threadUnadvise;
    if (cookie != 1 || !threadSink_) return E_INVALIDARG;
    threadSink_.Reset();
    return S_OK;
  }
  STDMETHODIMP AdviseKeyEventSink(TfClientId, ITfKeyEventSink*, BOOL) override {
    ++keyAdvise;
    return E_INVALIDARG;
  }
  STDMETHODIMP UnadviseKeyEventSink(TfClientId) override {
    ++keyUnadvise;
    return E_UNEXPECTED;
  }
  STDMETHODIMP GetFocus(ITfDocumentMgr** document) override {
    if (!document) return E_POINTER;
    *document = nullptr;
    return S_OK;
  }

  STDMETHODIMP Activate(TfClientId*) override { return E_NOTIMPL; }
  STDMETHODIMP Deactivate() override { return E_NOTIMPL; }
  STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
  STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
  STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP GetForeground(CLSID*) override { return E_NOTIMPL; }
  STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP KeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP KeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP PreserveKey(TfClientId, REFGUID, const TF_PRESERVEDKEY*,
                          const WCHAR*, ULONG) override { return E_NOTIMPL; }
  STDMETHODIMP UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override {
    return E_NOTIMPL;
  }

  ULONG References() const { return references_; }
  bool HasThreadSink() const { return threadSink_ != nullptr; }
  unsigned threadAdvise = 0;
  unsigned threadUnadvise = 0;
  unsigned keyAdvise = 0;
  unsigned keyUnadvise = 0;

 private:
  ULONG references_ = 1;
  Failure failure_;
  Microsoft::WRL::ComPtr<IUnknown> threadSink_;
};

static bool TestFailedActivation(ITfTextInputProcessorEx* processor) {
  bool valid = processor->Deactivate() == S_OK;
  valid = (processor->ActivateEx(nullptr, 123, 0) == E_INVALIDARG) && valid;
  using Failure = FailingThreadMgr::Failure;
  for (Failure failure : {Failure::ThreadSink, Failure::KeyInterface,
                          Failure::KeySink}) {
    Microsoft::WRL::ComPtr<FailingThreadMgr> manager;
    manager.Attach(new FailingThreadMgr(failure));
    const bool badIdRejected =
        processor->ActivateEx(manager.Get(), TF_CLIENTID_NULL, 0) == E_INVALIDARG &&
        manager->threadAdvise == 0 && manager->References() == 1;
    bool scenarioValid = badIdRejected;
    // Reusing the processor after failure must not leave references or cookies.
    for (unsigned attempt = 1; attempt <= 2; ++attempt) {
      const HRESULT result = processor->ActivateEx(manager.Get(), 123, 0);
      scenarioValid = result == E_FAIL && scenarioValid;
      scenarioValid = processor->Deactivate() == S_OK && scenarioValid;
      scenarioValid = processor->Deactivate() == S_OK && scenarioValid;
      scenarioValid = manager->threadAdvise == attempt &&
          manager->threadUnadvise ==
              (failure == Failure::ThreadSink ? 0 : attempt) &&
          manager->keyAdvise == (failure == Failure::KeySink ? attempt : 0) &&
          manager->keyUnadvise == 0 && !manager->HasThreadSink() &&
          manager->References() == 1 && scenarioValid;
    }
    std::wcout << L"Failed activation scenario " << static_cast<int>(failure)
               << L" cleanup valid = " << scenarioValid << L'\n';
    valid = scenarioValid && valid;
  }
  std::wcout << L"Activation lifecycle checks = " << valid << L'\n';
  return valid;
}
