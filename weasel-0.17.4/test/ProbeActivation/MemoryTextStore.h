#pragma once
#include <textstor.h>
#include <olectl.h>
#include <initguid.h>
#include <inputscope.h>
#include <algorithm>

// Scratch document owned solely by this test. Key calls go through TSF, never
// SendInput. Candidate UI is captured by the host instead of shown on screen.
class MemoryTextStore final : public ITextStoreACP, public ITfUIElementSink,
                              public ITfInputScope {
 public:
  explicit MemoryTextStore(InputScope scope = IS_TEXT, bool provideScope = true)
      : scope_(scope), provideScope_(provideScope) {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"WeaselScratchInputTest";
    const ATOM registered = RegisterClassW(&windowClass);
    const DWORD classError = registered ? ERROR_SUCCESS : GetLastError();
    window_ = CreateWindowExW(0, windowClass.lpszClassName, L"Weasel 输入法隔离测试",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             560, 240, nullptr, nullptr, windowClass.hInstance, nullptr);
    const DWORD windowError = window_ ? ERROR_SUCCESS : GetLastError();
    std::wcout << L"Scratch window created = " << (window_ != nullptr)
               << L", class error = " << classError
               << L", window error = " << windowError << L'\n' << std::flush;
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                             20, 20, 500, 150, window_, nullptr,
                             windowClass.hInstance, nullptr);
    SendMessageW(status_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  }
  ~MemoryTextStore() { if (window_) DestroyWindow(window_); }
  STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITextStoreACP)
      *object = static_cast<ITextStoreACP*>(this);
    else if (iid == IID_ITfUIElementSink)
      *object = static_cast<ITfUIElementSink*>(this);
    else if (iid == IID_ITfInputScope && provideScope_)
      *object = static_cast<ITfInputScope*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG result = --references_;
    if (!result) delete this;
    return result;
  }
  STDMETHODIMP BeginUIElement(DWORD, BOOL* show) override {
    if (!show) return E_POINTER;
    *show = FALSE;
    ++candidateStarts;
    return S_OK;
  }
  STDMETHODIMP UpdateUIElement(DWORD) override { return S_OK; }
  STDMETHODIMP EndUIElement(DWORD) override { ++candidateEnds; return S_OK; }
  STDMETHODIMP AdviseSink(REFIID iid, IUnknown* object, DWORD) override {
    if (iid != IID_ITextStoreACPSink || !object) return E_INVALIDARG;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    return object->QueryInterface(IID_ITextStoreACPSink,
        reinterpret_cast<void**>(sink_.GetAddressOf()));
  }
  STDMETHODIMP UnadviseSink(IUnknown* object) override {
    if (!object || !sink_) return CONNECT_E_NOCONNECTION;
    ComPtr<IUnknown> incoming, current;
    object->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(incoming.GetAddressOf()));
    sink_.As(&current);
    if (incoming != current) return CONNECT_E_NOCONNECTION;
    sink_.Reset();
    return S_OK;
  }
  STDMETHODIMP RequestLock(DWORD flags, HRESULT* result) override {
    if (!result) return E_POINTER;
    if (!sink_) return E_FAIL;
    if (refuseWrites && (flags & TS_LF_READWRITE) == TS_LF_READWRITE) { *result = E_FAIL; return S_OK; }
    if (lock_) {
      if (flags & TS_LF_SYNC) *result = TS_E_SYNCHRONOUS;
      else { pendingLock_ |= flags; *result = TS_S_ASYNC; }
      return S_OK;
    }
    lock_ = flags;
    *result = sink_->OnLockGranted(flags);
    lock_ = 0;
    while (pendingLock_) {
      DWORD pending = pendingLock_;
      pendingLock_ = 0;
      lock_ = pending;
      sink_->OnLockGranted(pending);
      lock_ = 0;
    }
    return S_OK;
  }
  STDMETHODIMP GetStatus(TS_STATUS* status) override {
    if (!status) return E_POINTER;
    *status = {};
    return S_OK;
  }
  STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG, LONG* outStart,
                            LONG* outEnd) override {
    if (!RangeValid(start, end) || !outStart || !outEnd) return E_INVALIDARG;
    *outStart = start; *outEnd = end;
    return S_OK;
  }
  STDMETHODIMP GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection,
                             ULONG* fetched) override {
    if (!ReadLocked()) return TS_E_NOLOCK;
    if (!selection || !fetched || !count ||
        (index != 0 && index != TS_DEFAULT_SELECTION)) return E_INVALIDARG;
    selection[0] = selection_; *fetched = 1;
    return S_OK;
  }
  STDMETHODIMP SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override {
    if (!WriteLocked()) return TS_E_NOLOCK;
    if (count != 1 || !selection ||
        !RangeValid(selection[0].acpStart, selection[0].acpEnd)) return E_INVALIDARG;
    selection_ = selection[0];
    return S_OK;
  }
  STDMETHODIMP GetText(LONG start, LONG end, WCHAR* plain, ULONG requested,
      ULONG* returned, TS_RUNINFO* runs, ULONG runsRequested,
      ULONG* runsReturned, LONG* next) override {
    if (!ReadLocked()) return TS_E_NOLOCK;
    if (end == -1) end = static_cast<LONG>(text.size());
    if (!RangeValid(start, end) || !returned || !runsReturned || !next ||
        (requested && !plain) || (runsRequested && !runs)) return E_INVALIDARG;
    ULONG length = static_cast<ULONG>(end - start);
    if (requested) length = (std::min)(length, requested);
    else if (!runsRequested) length = 0;
    *returned = requested ? length : 0;
    if (*returned) text.copy(plain, length, start);
    *runsReturned = runsRequested && length ? 1 : 0;
    if (*runsReturned) { runs[0].uCount = length; runs[0].type = TS_RT_PLAIN; }
    *next = start + length;
    return S_OK;
  }
  STDMETHODIMP SetText(DWORD, LONG start, LONG end, const WCHAR* value,
                       ULONG length, TS_TEXTCHANGE* change) override {
    if (!WriteLocked()) return TS_E_NOLOCK;
    if (!RangeValid(start, end) || (!value && length) || !change) return E_INVALIDARG;
    text.replace(start, end - start, value ? value : L"", length);
    *change = {start, end, start + static_cast<LONG>(length)};
    selection_.acpStart = selection_.acpEnd = change->acpNewEnd;
    return S_OK;
  }
  STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR* value, ULONG length,
      LONG* start, LONG* end, TS_TEXTCHANGE* change) override {
    if (!ReadLocked()) return TS_E_NOLOCK;
    if (start) *start = selection_.acpStart;
    if (end) *end = selection_.acpEnd;
    if (flags & TS_IAS_QUERYONLY) return S_OK;
    HRESULT hr = SetText(0, selection_.acpStart, selection_.acpEnd, value, length, change);
    if (SUCCEEDED(hr) && end) *end = change->acpNewEnd;
    return hr;
  }
  STDMETHODIMP GetEndACP(LONG* end) override {
    if (!ReadLocked()) return TS_E_NOLOCK;
    if (!end) return E_POINTER;
    *end = static_cast<LONG>(text.size()); return S_OK;
  }
  STDMETHODIMP GetActiveView(TsViewCookie* view) override {
    if (!view) return E_POINTER; *view = 1; return S_OK;
  }
  STDMETHODIMP GetTextExt(TsViewCookie, LONG, LONG, RECT* rect, BOOL* clipped) override {
    if (!rect || !clipped) return E_POINTER;
    *rect = {0, 0, 10, 20}; *clipped = FALSE; return S_OK;
  }
  STDMETHODIMP GetScreenExt(TsViewCookie, RECT* rect) override {
    if (!rect) return E_POINTER; *rect = {0, 0, 200, 80}; return S_OK;
  }
  STDMETHODIMP GetWnd(TsViewCookie, HWND* window) override {
    if (!window) return E_POINTER; *window = window_; return S_OK;
  }
  STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG count, const TS_ATTRID* attributes) override {
    scopeRequested_ = false;
    for (ULONG i = 0; i < count; ++i)
      if (provideScope_ && IsEqualGUID(attributes[i], GUID_PROP_INPUTSCOPE))
        scopeRequested_ = true;
    return S_OK;
  }
  STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG count, const TS_ATTRID* attributes,
                                      DWORD flags) override {
    return RequestSupportedAttrs(flags, count, attributes);
  }
  STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override {
    return S_OK;
  }
  STDMETHODIMP RetrieveRequestedAttrs(ULONG count, TS_ATTRVAL* values, ULONG* fetched) override {
    if (!fetched || (count && !values)) return E_POINTER;
    *fetched = 0;
    if (count && scopeRequested_) {
      values[0].idAttr = GUID_PROP_INPUTSCOPE;
      values[0].dwOverlapId = 0;
      VariantInit(&values[0].varValue);
      values[0].varValue.vt = VT_UNKNOWN;
      values[0].varValue.punkVal = static_cast<ITfInputScope*>(this);
      AddRef();
      *fetched = 1;
      scopeRequested_ = false;
    }
    return S_OK;
  }
  STDMETHODIMP GetInputScopes(InputScope** scopes, UINT* count) override {
    if (!scopes || !count) return E_POINTER;
    *count = 0;
    *scopes = static_cast<InputScope*>(CoTaskMemAlloc(sizeof(InputScope)));
    if (!*scopes) return E_OUTOFMEMORY;
    **scopes = scope_; *count = 1; return S_OK;
  }
  STDMETHODIMP GetPhrase(BSTR**, UINT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetRegularExpression(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP GetSRGS(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP GetXML(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*,
      DWORD, LONG* next, BOOL* found, LONG* offset) override {
    if (!next || !found || !offset) return E_POINTER;
    *next = halt; *found = FALSE; *offset = 0; return S_OK;
  }
  STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject**) override { return E_NOTIMPL; }
  STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
  STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* insertable) override {
    if (!insertable) return E_POINTER; *insertable = FALSE; return S_OK;
  }
  STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override { return E_NOTIMPL; }
  std::wstring text;
  HWND Window() const { return window_; }
  void ShowMessage(const std::wstring& message) {
    SetWindowTextW(status_, message.c_str());
  }
  unsigned candidateStarts = 0;
  unsigned candidateEnds = 0;
  bool refuseWrites = false;  // synthetic refusal; never affects a real host

 private:
  bool RangeValid(LONG start, LONG end) const {
    return start >= 0 && end >= start && end <= static_cast<LONG>(text.size());
  }
  bool ReadLocked() const { return (lock_ & TS_LF_READ) != 0; }
  bool WriteLocked() const { return (lock_ & TS_LF_READWRITE) == TS_LF_READWRITE; }
  ULONG references_ = 1;
  DWORD lock_ = 0, pendingLock_ = 0;
  HWND window_ = nullptr;
  HWND status_ = nullptr;
  TS_SELECTION_ACP selection_ = {0, 0, {TS_AE_END, FALSE}};
  ComPtr<ITextStoreACPSink> sink_;
  InputScope scope_;
  bool provideScope_, scopeRequested_ = false;
};

class MenuCapture final : public ITfMenu {
 public:
  STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfMenu) return E_NOINTERFACE;
    *object = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG result = --references_; if (!result) delete this; return result;
  }
  STDMETHODIMP AddMenuItem(UINT id, DWORD, HBITMAP, HBITMAP, const WCHAR*,
                            ULONG, ITfMenu**) override {
    correction = correction || id == ID_WEASELTRAY_CORRECTION;
    probe = probe || id == ID_WEASELTRAY_CORRECTION_PROBE;
    return S_OK;
  }
  bool correction = false, probe = false;
 private:
  ULONG references_ = 1;
};
