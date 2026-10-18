#pragma once
#include <functional>

// Stack-owned mocks retain an owner reference. They report every borrowed COM
// reference so a leaked GetRange/GetSelection output cannot hide in TSF caches.
#define COMPOSITION_MOCK_UNKNOWN(Interface) \
  STDMETHODIMP QueryInterface(REFIID iid, void** object) override { \
    if (!object) return E_POINTER; \
    *object = nullptr; \
    if (iid != IID_IUnknown && iid != __uuidof(Interface)) return E_NOINTERFACE; \
    *object = static_cast<Interface*>(this); AddRef(); return S_OK; \
  } \
  STDMETHODIMP_(ULONG) AddRef() override { return ++references; } \
  STDMETHODIMP_(ULONG) Release() override { return --references; } \
  ULONG references = 1

class CompositionRange final : public ITfRange {
 public:
  COMPOSITION_MOCK_UNKNOWN(ITfRange);
  HRESULT textResult = S_OK;
  HRESULT collapseResult = S_OK;
  unsigned textWrites = 0;
  unsigned collapses = 0;
  LONG start = 0, end = 0;
  STDMETHODIMP SetText(TfEditCookie, DWORD, const WCHAR*, LONG) override {
    ++textWrites; return textResult;
  }
  STDMETHODIMP Collapse(TfEditCookie, TfAnchor) override {
    ++collapses; return collapseResult;
  }
  STDMETHODIMP GetText(TfEditCookie, DWORD, WCHAR*, ULONG, ULONG*) override { return E_NOTIMPL; }
  STDMETHODIMP GetFormattedText(TfEditCookie, IDataObject**) override { return E_NOTIMPL; }
  STDMETHODIMP GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
  STDMETHODIMP InsertEmbedded(TfEditCookie, DWORD, IDataObject*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStart(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEnd(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP IsEmpty(TfEditCookie, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP CompareStart(TfEditCookie, ITfRange* target, TfAnchor anchor, LONG* result) override {
    auto* other = static_cast<CompositionRange*>(target);
    const LONG position = anchor == TF_ANCHOR_START ? other->start : other->end;
    *result = start < position ? -1 : start > position ? 1 : 0; return S_OK;
  }
  STDMETHODIMP CompareEnd(TfEditCookie, ITfRange* target, TfAnchor anchor, LONG* result) override {
    auto* other = static_cast<CompositionRange*>(target);
    const LONG position = anchor == TF_ANCHOR_START ? other->start : other->end;
    *result = end < position ? -1 : end > position ? 1 : 0; return S_OK;
  }
  STDMETHODIMP AdjustForInsert(TfEditCookie, ULONG, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetGravity(TfGravity*, TfGravity*) override { return E_NOTIMPL; }
  STDMETHODIMP SetGravity(TfEditCookie, TfGravity, TfGravity) override { return E_NOTIMPL; }
  STDMETHODIMP Clone(ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }
};

class TestComposition final : public ITfComposition {
 public:
  explicit TestComposition(CompositionRange& range) : range_(range) {}
  COMPOSITION_MOCK_UNKNOWN(ITfComposition);
  HRESULT rangeResult = S_OK;
  HRESULT endResult = S_OK;
  bool nullRange = false;
  unsigned ends = 0;
  unsigned rangeQueries = 0;
  std::function<void()> onEnd;
  STDMETHODIMP GetRange(ITfRange** range) override {
    ++rangeQueries;
    if (!range) return E_POINTER;
    *range = nullptr;
    if (rangeResult == S_OK && !nullRange) { *range = &range_; range_.AddRef(); }
    return rangeResult;
  }
  STDMETHODIMP EndComposition(TfEditCookie) override {
    ++ends; if (onEnd) onEnd(); return endResult;
  }
  STDMETHODIMP ShiftStart(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEnd(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
 private:
  CompositionRange& range_;
};

class CompositionContext : public ITfContext {
 public:
  explicit CompositionContext(CompositionRange& range) : range_(range) {}
  COMPOSITION_MOCK_UNKNOWN(ITfContext);
  HRESULT selectionResult = S_OK;
  bool emptySelection = false;
  bool nullSelection = false;
  HRESULT setSelectionResult = S_OK;
  HRESULT requestResult = S_OK;
  HRESULT forcedSessionResult = S_OK;
  bool runSynchronously = true;
  bool defer = false;
  unsigned selectionSets = 0;
  TfEditCookie lastSelectionCookie = 0;
  unsigned requests = 0;
  CompositionRange* selectionRange = nullptr;
  ITfContextView* activeView = nullptr;
  ComPtr<ITfEditSession> deferredSession;
  HRESULT Drain() {
    ComPtr<ITfEditSession> session = deferredSession;
    deferredSession.Reset();
    return session ? session->DoEditSession(1) : E_UNEXPECTED;
  }
  STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION* selection,
                            ULONG* fetched) override {
    if (!selection || !fetched) return E_POINTER;
    *selection = {}; *fetched = 0;
    if (selectionResult == S_OK && !emptySelection) {
      *fetched = 1;
      if (!nullSelection) { selection->range = selectionRange ? selectionRange : &range_; selection->range->AddRef(); }
    }
    return selectionResult;
  }
  STDMETHODIMP GetProperty(REFGUID, ITfProperty** property) override {
    if (!property) return E_POINTER;
    *property = nullptr; return E_NOINTERFACE;
  }
  STDMETHODIMP RequestEditSession(TfClientId, ITfEditSession* session, DWORD,
                                 HRESULT* result) override {
    ++requests;
    if (FAILED(requestResult)) return requestResult;  // output intentionally untouched
    if (!result || !session) return E_INVALIDARG;
    if (defer) {
      deferredSession = session;
      *result = TF_S_ASYNC;
    } else {
      *result = runSynchronously ? session->DoEditSession(1) : forcedSessionResult;
    }
    return requestResult;
  }
  STDMETHODIMP InWriteSession(TfClientId, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP SetSelection(TfEditCookie cookie, ULONG, const TF_SELECTION*) override {
    ++selectionSets; lastSelectionCookie = cookie; return setSelectionResult;
  }
  STDMETHODIMP GetStart(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetEnd(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetActiveView(ITfContextView** view) override {
    if (!view) return E_POINTER;
    *view = activeView; if (*view) (*view)->AddRef();
    return activeView ? S_OK : E_NOTIMPL;
  }
  STDMETHODIMP EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }
  STDMETHODIMP GetStatus(TF_STATUS*) override { return E_NOTIMPL; }
  STDMETHODIMP GetAppProperty(REFGUID, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
  STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) override { return E_NOTIMPL; }
 private:
  CompositionRange& range_;
};

class StartCompositionContext final : public CompositionContext,
    public ITfInsertAtSelection, public ITfContextComposition {
 public:
  StartCompositionContext(CompositionRange& range, TestComposition& composition)
      : CompositionContext(range), range_(range), composition_(composition) {}
  HRESULT insertInterfaceResult = S_OK;
  HRESULT compositionInterfaceResult = S_OK;
  HRESULT insertResult = S_OK;
  HRESULT startResult = S_OK;
  bool nullInsertRange = false;
  bool rejectComposition = false;
  unsigned insertions = 0, starts = 0;
  TestComposition* nextComposition = nullptr;
  STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == IID_ITfInsertAtSelection) {
      if (FAILED(insertInterfaceResult)) return insertInterfaceResult;
      *object = static_cast<ITfInsertAtSelection*>(this);
    } else if (iid == IID_ITfContextComposition) {
      if (FAILED(compositionInterfaceResult)) return compositionInterfaceResult;
      *object = static_cast<ITfContextComposition*>(this);
    } else {
      return CompositionContext::QueryInterface(iid, object);
    }
    AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return CompositionContext::AddRef(); }
  STDMETHODIMP_(ULONG) Release() override { return CompositionContext::Release(); }
  STDMETHODIMP InsertTextAtSelection(TfEditCookie, DWORD flags, const WCHAR*,
                                    LONG length, ITfRange** range) override {
    if (!range) return E_POINTER;
    *range = nullptr; ++insertions;
    if (flags != TF_IAS_QUERYONLY || length != 0) return E_INVALIDARG;
    if (insertResult == S_OK && !nullInsertRange) { *range = &range_; range_.AddRef(); }
    return insertResult;
  }
  STDMETHODIMP StartComposition(TfEditCookie, ITfRange*, ITfCompositionSink*,
                               ITfComposition** composition) override {
    if (!composition) return E_POINTER;
    *composition = nullptr; ++starts;
    if (startResult == S_OK && !rejectComposition) {
      *composition = nextComposition ? nextComposition : &composition_; (*composition)->AddRef();
    }
    return startResult;
  }
  STDMETHODIMP InsertEmbeddedAtSelection(TfEditCookie, DWORD, IDataObject*, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumCompositions(IEnumITfCompositionView**) override { return E_NOTIMPL; }
  STDMETHODIMP FindComposition(TfEditCookie, ITfRange*, IEnumITfCompositionView**) override { return E_NOTIMPL; }
  STDMETHODIMP TakeOwnership(TfEditCookie, ITfCompositionView*, ITfCompositionSink*, ITfComposition**) override { return E_NOTIMPL; }
 private:
  CompositionRange& range_;
  TestComposition& composition_;
};

class CompositionView final : public ITfContextView {
 public:
  COMPOSITION_MOCK_UNKNOWN(ITfContextView);
  HRESULT extentResult = S_OK;
  ITfRange* lastRange = nullptr;  // observed only, never retained
  unsigned extentCalls = 0;
  STDMETHODIMP GetTextExt(TfEditCookie, ITfRange* range, RECT* rect, BOOL* clipped) override {
    ++extentCalls; lastRange = range;
    // Zero coordinates keep this unit check out of IPC/UI position updates.
    *rect = {}; *clipped = FALSE; return extentResult;
  }
  STDMETHODIMP GetRangeFromPoint(TfEditCookie, const POINT*, DWORD, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetScreenExt(RECT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetWnd(HWND*) override { return E_NOTIMPL; }
};

class SelectionEditRecord final : public ITfEditRecord {
 public:
  COMPOSITION_MOCK_UNKNOWN(ITfEditRecord);
  STDMETHODIMP GetSelectionStatus(BOOL* changed) override { *changed = TRUE; return S_OK; }
  STDMETHODIMP GetTextAndPropertyUpdates(DWORD, const GUID**, ULONG, IEnumTfRanges** ranges) override {
    *ranges = nullptr; return E_NOTIMPL;
  }
};
#undef COMPOSITION_MOCK_UNKNOWN

static bool TestCompositionReferences(HMODULE module, REFCLSID clsid) {
  using Exercise = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*,
      ITfContextView*, ITfComposition*, TfEditCookie, DWORD, BOOL);
  auto exercise = reinterpret_cast<Exercise>(GetProcAddress(module, "WeaselProbeExerciseComposition"));
  auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(
      GetProcAddress(module, "DllGetClassObject"));
  if (!exercise || !get) return false;
  ComPtr<IClassFactory> factory;
  ComPtr<ITfTextInputProcessorEx> service;
  HRESULT hr = get(clsid, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(service.GetAddressOf()));
  if (FAILED(hr) || !service) return false;
  unsigned scenarios = 0, failures = 0;
  auto scenario = [&](const WCHAR* name, bool ending, bool hasComposition, bool clear,
                      HRESULT rangeResult, HRESULT selectionResult, bool emptySelection,
                      bool nullSelection, bool nullRange, HRESULT extentResult, HRESULT textResult) {
    std::wcout << L"Running composition scenario: " << name << L'\n' << std::flush;
    CompositionRange selected, composed;
    CompositionContext context(selected);
    TestComposition composition(composed);
    CompositionView view;
    context.selectionResult = selectionResult;
    context.emptySelection = emptySelection;
    context.nullSelection = nullSelection;
    composition.rangeResult = rangeResult;
    composition.nullRange = nullRange;
    view.extentResult = extentResult;
    composed.textResult = textResult;
    bool valid = true;
    for (unsigned repeat = 0; repeat < 64; ++repeat) {
      const unsigned callsBefore = view.extentCalls;
      const HRESULT result = exercise(service.Get(), &context, &view,
          hasComposition ? &composition : nullptr, 1, ending ? 0 : 1, clear);
      const bool selectionValid = selectionResult == S_OK && !emptySelection && !nullSelection;
      const bool usesComposition = hasComposition && rangeResult == S_OK && !nullRange;
      valid = selected.references == 1 && composed.references == 1 &&
          context.references == 1 && composition.references == 1 && view.references == 1 && valid;
      if (ending) {
        const HRESULT expected = !hasComposition || !clear ? S_OK :
            FAILED(rangeResult) ? rangeResult : nullRange ? E_FAIL : textResult;
        valid = result == expected && composition.ends == (hasComposition ? repeat + 1 : 0) &&
            composed.textWrites == (hasComposition && clear && usesComposition ? repeat + 1 : 0) && valid;
      } else {
        valid = result == (selectionValid ? S_OK : E_FAIL) &&
            view.extentCalls == callsBefore + (selectionValid ? 1 : 0) && valid;
        if (selectionValid) valid = view.lastRange == (usesComposition ? &composed : &selected) && valid;
      }
    }
    ++scenarios;
    if (!valid) ++failures;
    std::wcout << L"Composition " << name << L" checks valid = " << valid << L'\n';
    // Repair leaked references only on these diagnostic-owned mocks so failed
    // checks cannot leave a dangling stack object or contaminate later cases.
    while (selected.references > 1) selected.Release();
    while (composed.references > 1) composed.Release();
  };
  scenario(L"end clear", true, true, true, S_OK, S_OK, false, false, false, S_OK, S_OK);
  scenario(L"end preserve", true, true, false, S_OK, S_OK, false, false, false, S_OK, S_OK);
  scenario(L"end no composition", true, false, true, S_OK, S_OK, false, false, false, S_OK, S_OK);
  scenario(L"end range rejected", true, true, true, E_UNEXPECTED, S_OK, false, false, false, S_OK, S_OK);
  scenario(L"end null range", true, true, true, S_OK, S_OK, false, false, true, S_OK, S_OK);
  scenario(L"end text rejected", true, true, true, S_OK, S_OK, false, false, false, S_OK, TF_E_NOLOCK);
  scenario(L"extent composition", false, true, false, S_OK, S_OK, false, false, false, S_OK, S_OK);
  scenario(L"extent selection", false, false, false, S_OK, S_OK, false, false, false, S_OK, S_OK);
  scenario(L"extent range fallback", false, true, false, E_UNEXPECTED, S_OK, false, false, false, S_OK, S_OK);
  scenario(L"extent null range fallback", false, true, false, S_OK, S_OK, false, false, true, S_OK, S_OK);
  scenario(L"extent rejected", false, true, false, S_OK, S_OK, false, false, false, TF_E_NOLAYOUT, S_OK);
  scenario(L"selection rejected", false, true, false, S_OK, TF_E_NOLOCK, false, false, false, S_OK, S_OK);
  scenario(L"selection empty", false, true, false, S_OK, S_OK, true, false, false, S_OK, S_OK);
  scenario(L"selection null", false, true, false, S_OK, S_OK, false, true, false, S_OK, S_OK);
  // No display property is supplied: test reference ownership and safe failure,
  // without claiming that actual host display-attribute rendering is covered.
  for (unsigned mode = 0; mode < 4; ++mode) {
    CompositionRange range, selected;
    CompositionContext context(selected);
    TestComposition composition(range);
    composition.rangeResult = mode == 1 ? E_UNEXPECTED : S_OK;
    composition.nullRange = mode == 2;
    bool valid = true;
    for (unsigned repeat = 0; repeat < 64; ++repeat) {
      valid = exercise(service.Get(), &context, nullptr, mode == 3 ? nullptr : &composition,
          1, 2, FALSE) == E_FAIL && valid;
      valid = range.references == 1 && context.references == 1 && composition.references == 1 && valid;
    }
    ++scenarios;
    if (!valid) ++failures;
    std::wcout << L"Composition display attribute mode " << mode
               << L" references balanced = " << valid << L'\n';
    while (range.references > 1) range.Release();
  }
  std::wcout << L"Composition scenarios = " << scenarios << L", repetitions each = 64, failures = "
             << failures << L'\n';
  return failures == 0;
}

static bool TestCompositionResults(HMODULE module, REFCLSID clsid) {
  using Exercise = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*,
      ITfContextView*, ITfComposition*, TfEditCookie, DWORD, BOOL);
  using Create = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*,
      ITfComposition*, DWORD, BOOL, ITfEditSession**);
  using Request = HRESULT(WINAPI*)(ITfContext*, ITfEditSession*, DWORD);
  auto exercise = reinterpret_cast<Exercise>(GetProcAddress(module, "WeaselProbeExerciseComposition"));
  auto create = reinterpret_cast<Create>(GetProcAddress(module, "WeaselProbeCreateCompositionSession"));
  auto request = reinterpret_cast<Request>(GetProcAddress(module, "WeaselProbeRequestCompositionSession"));
  auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(
      GetProcAddress(module, "DllGetClassObject"));
  if (!exercise || !create || !request || !get) return false;
  ComPtr<IClassFactory> factory;
  ComPtr<ITfTextInputProcessorEx> service;
  HRESULT hr = get(clsid, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(service.GetAddressOf()));
  if (FAILED(hr) || !service) return false;
  unsigned scenarios = 0, failures = 0;
  auto check = [&](const WCHAR* group, unsigned mode, bool valid) {
    ++scenarios; if (!valid) ++failures;
    std::wcout << L"Composition result " << group << L" mode " << mode
               << L" valid = " << valid << L'\n' << std::flush;
  };
  const DWORD flags = TF_ES_ASYNCDONTCARE | TF_ES_READWRITE;
  for (unsigned mode = 0; mode < 11; ++mode) {
    CompositionRange range;
    TestComposition composition(range);
    StartCompositionContext context(range, composition);
    HRESULT expected = S_OK;
    if (mode == 2) { context.insertInterfaceResult = E_NOINTERFACE; expected = E_NOINTERFACE; }
    if (mode == 3) { context.insertResult = TF_E_NOLOCK; expected = TF_E_NOLOCK; }
    if (mode == 4) { context.nullInsertRange = true; expected = E_FAIL; }
    if (mode == 5) { context.compositionInterfaceResult = E_NOINTERFACE; expected = E_NOINTERFACE; }
    if (mode == 6) { context.startResult = TF_E_DISCONNECTED; expected = TF_E_DISCONNECTED; }
    if (mode == 7) { context.rejectComposition = true; expected = E_FAIL; }
    if (mode == 8) { range.textResult = TF_E_NOLOCK; expected = TF_E_NOLOCK; }
    if (mode == 9) { range.collapseResult = TF_E_NOLOCK; expected = TF_E_NOLOCK; }
    if (mode == 10) { context.setSelectionResult = TF_E_NOLOCK; expected = TF_E_NOLOCK; }
    const bool created = mode < 2 || mode >= 8;
    ComPtr<ITfEditSession> session;
    hr = create(service.Get(), &context, nullptr, 3, mode == 0, &session);
    bool valid = hr == S_OK && session;
    if (session) {
      valid = request(&context, session.Get(), flags) == expected && valid;
      valid = exercise(service.Get(), &context, nullptr, created ? &composition : nullptr,
          1, 5, FALSE) == S_OK && valid;
      // Setup failure after StartComposition leaves the actual host-owned
      // composition tracked for cleanup; no unverified rollback is claimed.
      valid = composition.ends == 0 && context.starts == (mode <= 5 && mode >= 2 ? 0U : 1U) && valid;
      if (mode == 0) valid = range.textWrites == 0 && context.selectionSets == 1 && valid;
      if (mode == 1) valid = range.textWrites == 1 && context.selectionSets == 1 && valid;
    }
    exercise(service.Get(), &context, nullptr, nullptr, 1, 4, FALSE);
    session.Reset();
    valid = context.references == 1 && range.references == 1 && composition.references == 1 && valid;
    check(L"start", mode, valid);
  }
  for (unsigned mode = 0; mode < 5; ++mode) {
    CompositionRange range;
    TestComposition composition(range);
    CompositionContext context(range);
    if (mode == 0) composition.endResult = TF_E_NOLOCK;
    if (mode == 1) { range.textResult = TF_E_NOLOCK; composition.endResult = E_UNEXPECTED; }
    if (mode == 2) composition.rangeResult = E_UNEXPECTED;
    if (mode == 3) composition.nullRange = true;
    if (mode == 4) { range.textResult = TF_E_NOLOCK; }
    const HRESULT expected = mode <= 1 || mode == 4 ? TF_E_NOLOCK : mode == 2 ? E_UNEXPECTED : E_FAIL;
    const bool ended = mode >= 2;
    exercise(service.Get(), &context, nullptr, &composition, 1, 4, FALSE);
    ComPtr<ITfEditSession> session;
    hr = create(service.Get(), &context, &composition, 0, mode != 0, &session);
    bool valid = hr == S_OK && session;
    if (session) valid = request(&context, session.Get(), flags) == expected && valid;
    valid = composition.ends == 1 &&
        exercise(service.Get(), &context, nullptr, ended ? nullptr : &composition,
                 1, 5, FALSE) == S_OK && valid;
    exercise(service.Get(), &context, nullptr, nullptr, 1, 4, FALSE);
    session.Reset();
    valid = context.references == 1 && range.references == 1 && composition.references == 1 && valid;
    check(L"end failure", mode, valid);
  }
  for (unsigned mode = 0; mode < 6; ++mode) {
    CompositionRange range;
    TestComposition composition(range);
    CompositionContext context(range);
    HRESULT expected = S_OK;
    if (mode == 0) { context.requestResult = TF_E_LOCKED; expected = TF_E_LOCKED; }
    if (mode == 1) { context.requestResult = TF_E_DISCONNECTED; expected = TF_E_DISCONNECTED; }
    if (mode == 2) { context.runSynchronously = false; context.forcedSessionResult = TS_E_READONLY; expected = TS_E_READONLY; }
    if (mode == 3) { context.defer = true; expected = TF_S_ASYNC; }
    if (mode == 4) { composition.endResult = E_UNEXPECTED; expected = E_UNEXPECTED; }
    exercise(service.Get(), &context, nullptr, &composition, 1, 4, FALSE);
    ComPtr<ITfEditSession> session;
    hr = create(service.Get(), &context, &composition, 0, FALSE, &session);
    bool valid = hr == S_OK && session;
    if (session) valid = request(&context, session.Get(), flags) == expected && valid;
    valid = composition.ends == (mode >= 4 ? 1U : 0U) && valid;
    if (mode == 3) valid = context.Drain() == S_OK && composition.ends == 1 && valid;
    valid = request(nullptr, session.Get(), flags) == E_INVALIDARG &&
        request(&context, nullptr, flags) == E_INVALIDARG && valid;
    exercise(service.Get(), &context, nullptr, nullptr, 1, 4, FALSE);
    session.Reset();
    valid = context.references == 1 && range.references == 1 && composition.references == 1 && valid;
    check(L"request channels", mode, valid);
  }
  {
    CompositionRange oldRange, newRange;
    TestComposition oldComposition(oldRange), newComposition(newRange);
    CompositionContext context(oldRange);
    context.defer = true;
    exercise(service.Get(), &context, nullptr, &oldComposition, 1, 4, FALSE);
    ComPtr<ITfEditSession> session;
    hr = create(service.Get(), &context, &oldComposition, 0, TRUE, &session);
    bool valid = hr == S_OK && session;
    if (session) valid = request(&context, session.Get(), flags) == TF_S_ASYNC && valid;
    valid = oldComposition.ends == 0 && valid;
    exercise(service.Get(), &context, nullptr, &newComposition, 1, 4, FALSE);
    // Both a stale host notification and a queued end must preserve new state.
    valid = exercise(service.Get(), &context, nullptr, &oldComposition, 1, 6, FALSE) == S_OK && valid;
    valid = context.Drain() == S_OK && oldComposition.ends == 1 &&
        newComposition.ends == 0 && newComposition.rangeQueries == 0 &&
        oldRange.textWrites == 1 && newRange.textWrites == 0 && valid;
    valid = exercise(service.Get(), &context, nullptr, &newComposition, 1, 5, FALSE) == S_OK && valid;
    exercise(service.Get(), &context, nullptr, nullptr, 1, 4, FALSE);
    session.Reset();
    valid = context.references == 1 && oldRange.references == 1 && newRange.references == 1 &&
        oldComposition.references == 1 && newComposition.references == 1 && valid;
    check(L"deferred old composition", 0, valid);
  }
  std::wcout << L"Composition result scenarios = " << scenarios << L", failures = " << failures << L'\n';
  return failures == 0;
}

static HRESULT WINAPI ObserveBoundContext(ITfContext* context, TfEditCookie cookie) {
  // Diagnostic-owned mock only: record target/cookie, without text or IPC.
  return context->SetSelection(cookie, 0, nullptr);
}

static bool TestBoundEditSessions(HMODULE module, REFCLSID clsid) {
  using Bound = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*,
      HRESULT(WINAPI*)(ITfContext*, TfEditCookie), ITfEditSession**);
  using Invalidate = HRESULT(WINAPI*)(ITfTextInputProcessorEx*);
  using Child = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*, ITfComposition*,
      DWORD, BOOL, ITfEditSession**);
  using Exercise = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*,
      ITfContextView*, ITfComposition*, TfEditCookie, DWORD, BOOL);
  auto bound = reinterpret_cast<Bound>(GetProcAddress(module, "WeaselProbeCreateBoundEditSession"));
  auto invalidate = reinterpret_cast<Invalidate>(GetProcAddress(module, "WeaselProbeInvalidateEditSessions"));
  auto child = reinterpret_cast<Child>(GetProcAddress(module, "WeaselProbeCreateCompositionSession"));
  auto exercise = reinterpret_cast<Exercise>(GetProcAddress(module, "WeaselProbeExerciseComposition"));
  auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
  if (!bound || !invalidate || !child || !exercise || !get) return false;
  ComPtr<IClassFactory> factory;
  ComPtr<ITfTextInputProcessorEx> service;
  HRESULT hr = get(clsid, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(service.GetAddressOf()));
  if (FAILED(hr) || !service) return false;
  unsigned scenarios = 0, failures = 0;
  auto check = [&](const WCHAR* name, unsigned mode, bool valid) {
    ++scenarios; if (!valid) ++failures;
    std::wcout << L"Bound edit " << name << L" mode " << mode << L" valid = " << valid << L'\n';
  };
  for (unsigned mode = 0; mode < 7; ++mode) {
    CompositionRange aRange, bRange;
    CompositionContext a(aRange), b(bRange);
    ComPtr<ITfEditSession> old, middle, current;
    bool valid = bound(service.Get(), &a, ObserveBoundContext, &old) == S_OK && old;
    if (mode == 1 || mode == 2) {
      valid = bound(service.Get(), &b, ObserveBoundContext, &middle) == S_OK && middle && valid;
    }
    if (mode == 3) valid = invalidate(service.Get()) == S_OK && valid;
    if (mode == 4 || mode == 6) valid = service->Deactivate() == S_OK && valid;
    if (mode == 6) valid = service->Deactivate() == S_OK && valid;
    if (mode == 5) {
      ComPtr<ITfThreadMgrEventSink> sink;
      valid = service.As(&sink) == S_OK && sink && valid;
      if (sink) valid = sink->OnSetFocus(nullptr, nullptr) == S_OK && valid;
    }
    CompositionContext* target = mode == 1 ? &b : &a;
    valid = bound(service.Get(), target, ObserveBoundContext, &current) == S_OK && current && valid;
    if (old) valid = old->DoEditSession(11) == (mode == 0 ? S_OK : TF_E_DISCONNECTED) && valid;
    if (middle && mode == 2) valid = middle->DoEditSession(12) == TF_E_DISCONNECTED && valid;
    if (current) valid = current->DoEditSession(22) == S_OK && valid;
    valid = target->lastSelectionCookie == 22 &&
        a.selectionSets == (mode == 1 ? 0U : mode == 0 ? 2U : 1U) &&
        b.selectionSets == (mode == 1 ? 1U : 0U) && valid;
    invalidate(service.Get());
    old.Reset(); middle.Reset(); current.Reset();
    valid = a.references == 1 && b.references == 1 && valid;
    check(L"document/lifecycle", mode, valid);
  }
  for (unsigned mode = 0; mode < 2; ++mode) {
    CompositionRange range, otherRange;
    TestComposition composition(range);
    StartCompositionContext context(range, composition);
    CompositionContext other(otherRange);
    ComPtr<ITfEditSession> binding, pending, replacement;
    bool valid = bound(service.Get(), &context, ObserveBoundContext, &binding) == S_OK && binding;
    if (mode == 1) exercise(service.Get(), &context, nullptr, &composition, 1, 4, FALSE);
    valid = child(service.Get(), &context, mode == 0 ? nullptr : &composition,
                  mode == 0 ? 3 : 0, TRUE, &pending) == S_OK && pending && valid;
    valid = bound(service.Get(), &other, ObserveBoundContext, &replacement) == S_OK && replacement && valid;
    if (pending) valid = pending->DoEditSession(1) == TF_E_DISCONNECTED && valid;
    valid = context.starts == 0 && composition.ends == 0 && range.textWrites == 0 && valid;
    exercise(service.Get(), &context, nullptr, nullptr, 1, 4, FALSE);
    invalidate(service.Get());
    binding.Reset(); pending.Reset(); replacement.Reset();
    valid = context.references == 1 && other.references == 1 && composition.references == 1 &&
        range.references == 1 && valid;
    check(L"cancel child", mode, valid);
  }
  {
    CompositionRange range;
    CompositionContext context(range);
    context.setSelectionResult = TF_E_NOLOCK;
    ComPtr<ITfEditSession> session, unbound;
    bool valid = bound(service.Get(), &context, ObserveBoundContext, &session) == S_OK && session;
    if (session) valid = session->DoEditSession(77) == TF_E_NOLOCK && context.selectionSets == 1 && valid;
    valid = service.As(&unbound) == S_OK && unbound && valid;
    if (unbound) valid = unbound->DoEditSession(1) == E_UNEXPECTED && valid;
    invalidate(service.Get()); session.Reset(); unbound.Reset();
    valid = context.references == 1 && valid;
    check(L"body error/unbound call", 0, valid);
  }
  for (unsigned mode = 0; mode < 3; ++mode) {
    CompositionRange oldRange, newRange;
    CompositionContext a(oldRange), b(newRange);
    TestComposition oldComposition(oldRange), newComposition(newRange);
    ComPtr<ITfEditSession> oldBinding, replacement;
    bool valid = bound(service.Get(), &a, ObserveBoundContext, &oldBinding) == S_OK && oldBinding;
    exercise(service.Get(), &a, nullptr, &oldComposition, 1, 7, FALSE);
    if (mode == 0) a.defer = true;
    if (mode == 1) oldComposition.onEnd = [&]() {
      exercise(service.Get(), &b, nullptr, &newComposition, 1, 7, FALSE);
    };
    if (mode == 2) a.requestResult = TF_E_LOCKED;
    valid = bound(service.Get(), &b, ObserveBoundContext, &replacement) == S_OK && replacement && valid;
    if (mode == 0) valid = a.Drain() == S_OK && valid;
    valid = exercise(service.Get(), &b, nullptr, mode == 1 ? &newComposition : nullptr,
        1, 5, FALSE) == S_OK && oldComposition.ends == (mode == 2 ? 0U : 1U) &&
        oldRange.textWrites == 0 && newRange.textWrites == 0 && valid;
    if (oldBinding) valid = oldBinding->DoEditSession(1) == TF_E_DISCONNECTED && valid;
    exercise(service.Get(), &b, nullptr, nullptr, 1, 7, FALSE);
    invalidate(service.Get()); oldBinding.Reset(); replacement.Reset();
    valid = a.references == 1 && b.references == 1 && oldComposition.references == 1 &&
        newComposition.references == 1 && oldRange.references == 1 && newRange.references == 1 && valid;
    check(L"old document cleanup/reentry", mode, valid);
  }
  std::wcout << L"Bound edit scenarios = " << scenarios << L", failures = " << failures << L'\n';
  return failures == 0;
}

static bool TestTextEdits(HMODULE module, REFCLSID clsid) {
  using Create = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*, ITfComposition*,
      DWORD, const WCHAR*, ITfEditSession**);
  auto create = reinterpret_cast<Create>(GetProcAddress(module, "WeaselProbeCreateTextSession"));
  auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
  if (!create || !get) return false;
  ComPtr<IClassFactory> factory;
  ComPtr<ITfTextInputProcessorEx> service;
  if (get(clsid, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf())) != S_OK ||
      factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
          reinterpret_cast<void**>(service.GetAddressOf())) != S_OK) return false;
  unsigned failures = 0;
  for (DWORD operation = 0; operation < 2; ++operation) {
    for (unsigned mode = 0; mode < 6; ++mode) {
      CompositionRange range;
      TestComposition composition(range);
      CompositionContext context(range);
      HRESULT expected = S_OK;
      if (mode == 1) { composition.rangeResult = TF_E_NOLOCK; expected = TF_E_NOLOCK; }
      if (mode == 2) { composition.nullRange = true; expected = E_FAIL; }
      if (mode == 3) { range.textResult = TS_E_READONLY; expected = TS_E_READONLY; }
      if (mode == 4) { range.collapseResult = TF_E_NOLOCK; expected = TF_E_NOLOCK; }
      if (mode == 5) { context.setSelectionResult = TS_E_READONLY; expected = TS_E_READONLY; }
      ComPtr<ITfEditSession> session;
      bool valid = create(service.Get(), &context, &composition, operation, L"模拟", &session) == S_OK && session;
      if (session) valid = session->DoEditSession(123) == expected && valid;
      session.Reset();
      valid = range.references == 1 && composition.references == 1 && context.references == 1 && valid;
      if (!valid) ++failures;
      std::wcout << L"Text edit operation/mode = " << operation << L"/" << mode << L", valid = " << valid << L'\n';
    }
  }
  std::wcout << L"Text edit scenarios = 12, failures = " << failures << L'\n';
  return failures == 0;
}


static void WINAPI CountSyntheticKey(WPARAM, LPARAM, BOOL* eaten, void* state) {
  ++*static_cast<unsigned*>(state); *eaten = TRUE;
}
static bool TestInterruptedInput(HMODULE module, REFCLSID clsid) {
  using Exercise = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*, ITfContextView*, ITfComposition*, TfEditCookie, DWORD, BOOL);
  using Bound = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*, HRESULT(WINAPI*)(ITfContext*, TfEditCookie), ITfEditSession**);
  using Child = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*, ITfComposition*, DWORD, BOOL, ITfEditSession**);
  using SetKeys = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, void(WINAPI*)(WPARAM, LPARAM, BOOL*, void*), void*);
  auto exercise = reinterpret_cast<Exercise>(GetProcAddress(module, "WeaselProbeExerciseComposition"));
  auto bound = reinterpret_cast<Bound>(GetProcAddress(module, "WeaselProbeCreateBoundEditSession"));
  auto child = reinterpret_cast<Child>(GetProcAddress(module, "WeaselProbeCreateCompositionSession"));
  auto setKeys = reinterpret_cast<SetKeys>(GetProcAddress(module, "WeaselProbeSetKeySink"));
  auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(module,"DllGetClassObject"));
  if (!exercise || !bound || !child || !setKeys || !get) return false;
  unsigned checks = 0, failed = 0;
  auto check = [&](bool valid, const char* name) { ++checks; if (!valid) ++failed; std::cout << (valid ? "PASS " : "FAIL ") << name << '\n'; };
  for (unsigned mode = 0; mode != 3; ++mode) {
    ComPtr<IClassFactory> factory; ComPtr<ITfTextInputProcessorEx> service;
    if (get(clsid, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf())) != S_OK || factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf())) != S_OK) return false;
    CompositionRange range, newRange; TestComposition oldComposition(range), newComposition(newRange);
    StartCompositionContext context(range, oldComposition); context.defer = true;
    ComPtr<ITfEditSession> oldWork;
    bound(service.Get(), &context, ObserveBoundContext, &oldWork);
    exercise(service.Get(), &context, nullptr, &oldComposition, 1, 7, FALSE);
    if (mode == 0) { ComPtr<ITfThreadFocusSink> focus; service.As(&focus); focus->OnKillThreadFocus(); }
    if (mode == 1) service->Deactivate();
    if (mode == 2) exercise(service.Get(), &context, nullptr, &oldComposition, 1, 6, FALSE);
    check(exercise(service.Get(), &context, nullptr, nullptr, 1, 5, FALSE) == S_OK, "interruption detaches stale frontend composition immediately");
    check(oldWork->DoEditSession(1) == TF_E_DISCONNECTED, "interruption invalidates old queued text writes");
    context.nextComposition = &newComposition;
    ComPtr<ITfEditSession> next; child(service.Get(), &context, nullptr, 3, TRUE, &next);
    check(next && next->DoEditSession(1) == S_OK, "next input can start a fresh composition without switching IME");
    if (context.deferredSession) check(context.Drain() == S_OK && oldComposition.ends == 1 && range.textWrites == 0,
        "deferred interruption cleanup survives invalidation without erasing host text");
    else check(oldComposition.ends == 0, "host termination does not recursively end an already terminated composition");
    check(exercise(service.Get(), &context, nullptr, &newComposition, 1, 5, FALSE) == S_OK && newComposition.ends == 0,
        "old cleanup preserves the next composition");
    exercise(service.Get(), &context, nullptr, nullptr, 1, 7, FALSE);
  }
  {
    ComPtr<IClassFactory> factory; ComPtr<ITfTextInputProcessorEx> service; ComPtr<ITfKeyEventSink> keys;
    get(clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.GetAddressOf()));
    factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf())); service.As(&keys);
    CompositionRange aRange,bRange; CompositionContext a(aRange),b(bRange);
    unsigned calls=0; setKeys(service.Get(), CountSyntheticKey, &calls); BOOL eaten=FALSE;
    keys->OnTestKeyDown(&a,'A',1,&eaten); keys->OnTestKeyDown(&a,'A',1,&eaten); keys->OnKeyDown(&a,'A',1,&eaten);
    check(calls==1, "duplicate query and matching key-down dispatch exactly once with no correction hint");
    keys->OnKeyUp(&a,'A',1,&eaten); calls=0;
    keys->OnTestKeyDown(&a,'A',1,&eaten); keys->OnTestKeyDown(&a,VK_BACK,1,&eaten);
    check(calls==2, "orphaned query cannot swallow the next Backspace");
    keys->OnKeyUp(&a,VK_BACK,1,&eaten); calls=0;
    keys->OnTestKeyDown(&a,'A',1,&eaten); keys->OnKeyDown(&a,'B',1,&eaten);
    check(calls==2, "unmatched key-down is processed instead of swallowed");
    keys->OnKeyUp(&a,'B',1,&eaten); calls=0;
    keys->OnTestKeyDown(&a,'A',1,&eaten); keys->OnTestKeyDown(&b,'A',1,&eaten);
    check(calls==2, "query from another context cannot suppress new input");
    setKeys(service.Get(), nullptr, nullptr);
    service->Deactivate();
  }
  // Host mouse/selection events, with the real text-edit sink and no global
  // keyboard or mouse injection. The cleanup may run after new composition.
  for (unsigned mode = 0; mode != 6; ++mode) {
    ComPtr<IClassFactory> factory; ComPtr<ITfTextInputProcessorEx> service;
    get(clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.GetAddressOf()));
    factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf()));
    CompositionRange range, caret, otherRange; range.start=2; range.end=5;
    caret.start=caret.end=3;
    if (mode==1) caret.start=caret.end=1;
    if (mode==2 || mode==4) caret.start=caret.end=8;
    if (mode==3) { caret.start=0; caret.end=7; }
    TestComposition composition(range);
    CompositionContext context(range), other(otherRange); context.selectionRange=&caret; context.defer=true;
    if (mode==5) composition.nullRange=true;
    ComPtr<ITfEditSession> work; bound(service.Get(),&context,ObserveBoundContext,&work);
    exercise(service.Get(),&context,nullptr,&composition,1,7,FALSE);
    ComPtr<ITfTextEditSink> sink; service.As(&sink); SelectionEditRecord record;
    const bool retired=mode>=1 && mode<=3;
    check(sink->OnEndEdit(mode==4 ? &other : &context,1,&record)==S_OK &&
        exercise(service.Get(),&context,nullptr,retired ? nullptr : &composition,1,5,FALSE)==S_OK &&
        work->DoEditSession(1)==(retired ? TF_E_DISCONNECTED : S_OK),
        "mouse/selection checks retire only the active composition when selection leaves it");
    if(context.deferredSession) check(context.Drain()==S_OK && composition.ends==1 && range.textWrites==0,
        "mouse cleanup preserves existing text and ends the old host composition");
    exercise(service.Get(),&context,nullptr,nullptr,1,7,FALSE);
    service->Deactivate(); work.Reset();
    check(context.references==1 && caret.references==1 && composition.references==1,
        "mouse/selection retirement releases captured references");
  }
  {
    ComPtr<IClassFactory> factory; ComPtr<ITfTextInputProcessorEx> service;
    get(clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.GetAddressOf()));
    factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf()));
    CompositionRange range; TestComposition composition(range); CompositionContext context(range);
    ComPtr<ITfEditSession> work, ending;
    bound(service.Get(),&context,ObserveBoundContext,&work);
    exercise(service.Get(),&context,nullptr,&composition,1,7,FALSE);
    composition.onEnd=[&]() { exercise(service.Get(),&context,nullptr,&composition,1,6,FALSE); };
    child(service.Get(),&context,&composition,0,FALSE,&ending);
    check(ending->DoEditSession(1)==S_OK && composition.ends==1 && range.textWrites==0 &&
        work->DoEditSession(1)==S_OK && exercise(service.Get(),&context,nullptr,nullptr,1,5,FALSE)==S_OK,
        "normal End with inline termination notification preserves current response generation");
    service->Deactivate();
  }
  using Apply=HRESULT(WINAPI*)(ITfTextInputProcessorEx*,ITfContext*,TfEditCookie,const WCHAR*);
  auto apply=reinterpret_cast<Apply>(GetProcAddress(module,"WeaselProbeApplySyntheticResponse"));
  if(!apply) return false;
  for(unsigned mode=0; mode!=6; ++mode) {
    ComPtr<IClassFactory> factory; ComPtr<ITfTextInputProcessorEx> service;
    get(clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.GetAddressOf()));
    factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf()));
    CompositionRange range, newRange; TestComposition composition(range), nextComposition(newRange);
    StartCompositionContext context(range,composition); context.defer=true;
    HRESULT expected=TS_E_READONLY;
    if(mode==0) range.textResult=expected;
    if(mode==1) context.setSelectionResult=expected;
    if(mode==2) range.collapseResult=expected;
    if(mode==3) composition.endResult=expected;
    if(mode==4) { context.rejectComposition=true; expected=E_FAIL; }
    if(mode==5) { context.insertInterfaceResult=E_NOINTERFACE; expected=E_NOINTERFACE; }
    ComPtr<ITfEditSession> work; bound(service.Get(),&context,ObserveBoundContext,&work);
    check(apply(service.Get(),&context,1,L"你好")==expected &&
        exercise(service.Get(),&context,nullptr,nullptr,1,5,FALSE)==S_OK && work->DoEditSession(1)==TF_E_DISCONNECTED,
        "host start/write/selection/End failure detaches composition and invalidates old work");
    range.textResult=range.collapseResult=context.setSelectionResult=S_OK;
    context.rejectComposition=false; context.insertInterfaceResult=S_OK; context.nextComposition=&nextComposition;
    ComPtr<ITfEditSession> next; child(service.Get(),&context,nullptr,3,TRUE,&next);
    const unsigned writes=range.textWrites;
    check(next->DoEditSession(1)==S_OK && (!context.deferredSession || context.Drain()==(mode==3 ? TS_E_READONLY : S_OK)) &&
        range.textWrites==writes && nextComposition.ends==0 &&
        exercise(service.Get(),&context,nullptr,&nextComposition,1,5,FALSE)==S_OK,
        "input resumes after host failure even if old cleanup fails or arrives late");
    exercise(service.Get(),&context,nullptr,nullptr,1,7,FALSE);
    service->Deactivate(); work.Reset(); next.Reset();
    check(context.references==1 && composition.references==1 && nextComposition.references==1,
        "failure recovery releases old and new context/composition references");
  }
  for(unsigned mode=0; mode!=8; ++mode) {
    ComPtr<IClassFactory> factory; ComPtr<ITfTextInputProcessorEx> service; ComPtr<ITfKeyEventSink> keys;
    get(clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.GetAddressOf()));
    factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf())); service.As(&keys);
    CompositionRange aRange,bRange; CompositionContext a(aRange),b(bRange);
    unsigned calls=0; setKeys(service.Get(),CountSyntheticKey,&calls); BOOL eaten=FALSE;
    if(mode==0) { keys->OnTestKeyUp(&a,'A',1,&eaten); keys->OnTestKeyUp(&a,'A',1,&eaten); keys->OnKeyUp(&a,'A',1,&eaten); }
    if(mode==1) { keys->OnTestKeyUp(&a,'A',1,&eaten); keys->OnKeyUp(&a,'B',1,&eaten); }
    if(mode==2) { keys->OnTestKeyUp(&a,'A',1,&eaten); keys->OnTestKeyUp(&b,'A',1,&eaten); }
    if(mode==3) { keys->OnTestKeyDown(&a,'A',1,&eaten); keys->OnTestKeyDown(&a,'A',0x40000001,&eaten); }
    if(mode==4) { keys->OnKeyDown(&a,'A',1,&eaten); keys->OnKeyUp(&a,'A',1,&eaten); }
    if(mode==5 || mode==6) {
      keys->OnTestKeyDown(&a,'A',1,&eaten);
      if(mode==5) { ComPtr<ITfThreadFocusSink> focus; service.As(&focus); focus->OnKillThreadFocus(); }
      else keys->OnSetFocus(FALSE);
      keys->OnKeyDown(&a,'A',1,&eaten);
    }
    if(mode==7) { keys->OnTestKeyDown(&a,'A',1,&eaten); service->Deactivate(); keys->OnTestKeyDown(&a,'A',1,&eaten); }
    check(calls==(mode==0 ? 1U : 2U) && eaten,
        "release pairing, auto-repeat, actual-only events and lifecycle resets do not swallow input");
    service->Deactivate(); setKeys(service.Get(),nullptr,nullptr);
    check(a.references==1 && b.references==1,"pending key context references released on reset");
  }
  std::cout << "Interrupted input checks = " << checks << ", failures = " << failed << '\n';
  return failed==0;
}

#include "FocusChecks.h"

static int RunCompositionChecks(const WCHAR* dll, REFCLSID clsid) {
  HMODULE module = LoadLibraryW(dll);
  if (!module) return 1;
  bool valid = TestCompositionReferences(module, clsid);
  valid = TestCompositionResults(module, clsid) && valid;
  valid = TestBoundEditSessions(module, clsid) && valid;
  valid = TestTextEdits(module, clsid) && valid;
  valid = TestInterruptedInput(module, clsid) && valid;
  valid = TestCorrectionFocus(module, clsid) && valid;
  auto canUnload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(module, "DllCanUnloadNow"));
  if (canUnload && canUnload() == S_OK && FreeLibrary(module)) {
    HMODULE resident = nullptr;
    const BOOL found = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(module), &resident);
    const DWORD error = found ? ERROR_SUCCESS : GetLastError();
    const bool unloaded = !found && error == ERROR_MOD_NOT_FOUND;
    std::wcout << L"Composition DLL actually unloaded = " << unloaded << L'\n';
    valid = unloaded && valid;
  } else {
    valid = false;
  }
  return valid ? 0 : 1;
}
