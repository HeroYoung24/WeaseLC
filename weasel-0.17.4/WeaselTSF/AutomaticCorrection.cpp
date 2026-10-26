#include "stdafx.h"
#include "AutomaticCorrection.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "EditSession.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <functional>
#include <CorrectionProbe.h>
#include <CorrectionHintView.h>
#include <CorrectionDeployment.h>
#include <CorrectionPipelineDiagnostics.h>

namespace {
constexpr ULONGLONG kLifetime = 10000;
bool Same(ITfRange* a, ITfRange* b, TfEditCookie cookie, TfAnchor aa, TfAnchor bb) {
  LONG difference = 1;
  return (aa == TF_ANCHOR_START ? a->CompareStart(cookie, b, bb, &difference)
                                : a->CompareEnd(cookie, b, bb, &difference)) == S_OK && !difference;
}
bool ReadExact(ITfRange* range, TfEditCookie cookie, const std::wstring& expected) {
  WCHAR buffer[1025] = {};
  ULONG units = 0;
  const HRESULT hr = range->GetText(cookie, 0, buffer, _countof(buffer), &units);
  const bool equal = hr == S_OK && units == expected.size() &&
      expected.compare(0, expected.size(), buffer, units) == 0;
  SecureZeroMemory(buffer, sizeof(buffer));
  return equal;
}
class AutomaticEdit : public CEditSession {
 public:
  AutomaticEdit(WeaselTSF* owner, ITfContext* context, bool apply)
      : CEditSession(owner, context), apply_(apply) {}
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    if (!_IsCurrent()) return TF_E_DISCONNECTED;
    try { return _pTextService->Automatic().Edit(cookie, _pContext, apply_, &applied, &verification); }
    catch (...) { _pTextService->Automatic().Cancel(); return applied ? S_OK : E_FAIL; }
  }
  bool applied = false;
  AutomaticCorrection::WriteVerification verification;
 private:
  bool apply_;
};
class AutomaticVerify : public CEditSession {
 public:
  AutomaticVerify(WeaselTSF* owner, ITfContext* context, const AutomaticCorrection::WriteVerification& verification)
      : CEditSession(owner, context), verification_(verification) {}
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    if (!_IsCurrent()) return TF_E_DISCONNECTED;
    try { return _pTextService->Automatic().VerifyWrite(cookie, _pContext, verification_); }
    catch (...) { return E_FAIL; }
  }
 private:
  AutomaticCorrection::WriteVerification verification_;
};
class AutomaticStep : public CEditSession {
 public:
  AutomaticStep(WeaselTSF* owner, ITfContext* context, std::function<HRESULT(TfEditCookie)> body)
      : CEditSession(owner, context), body_(std::move(body)) {}
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    if (!_IsCurrent() || !body_) return TF_E_DISCONNECTED;
    return body_(cookie);
  }
  // Do not retain stack captures if a nonconforming host keeps this session.
  // A late callback must be inert even if it still has the same generation.
  void Retire() { body_ = {}; }
 private:
  std::function<HRESULT(TfEditCookie)> body_;
};
void Close(HANDLE& handle) { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = nullptr; }
}  // namespace

AutomaticCorrection::~AutomaticCorrection() {
  Cancel();
  if (window_) DestroyWindow(window_);
  // The last window must retire the WNDPROC before COM can unload this DLL.
  UnregisterClassW(L"WeaseLC.AutomaticCorrection.V1", g_hInst);
}
TfClientId AutomaticCorrection::Client() const {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_) return testClient_;
#endif
  return owner_->_tfClientId;
}
bool AutomaticCorrection::Focused(ITfContext* target) const {
  if (!target) target = context_;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_) return testFocused_ && target != nullptr;
#endif
  const auto result = correction::CheckContextFocus(owner_->_pThreadMgr, target, true, GetForegroundWindow());
  if (!result) Report(correction::AutomaticStage::FocusBlocked, result);
  return static_cast<bool>(result) && Client() != TF_CLIENTID_NULL;
}
void AutomaticCorrection::Report(correction::AutomaticStage stage, correction::FocusResult focus) const {
  if (diagnostic_.stage == stage && diagnostic_.focus == focus.stage && diagnostic_.error == focus.error) return;
  diagnostic_.stage = stage; diagnostic_.focus = focus.stage; diagnostic_.error = focus.error;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_) return;
#endif
  correction::SaveAutomaticDiagnostic(diagnostic_);
}
void AutomaticCorrection::Hide() { if (window_) ShowWindow(window_, SW_HIDE); }
void AutomaticCorrection::StopWorker() {
  brokerCall_.Reset();
  // Closing this job terminates only the child we created, including on teardown.
  Close(job_); Close(process_); Close(output_);
}
void AutomaticCorrection::Cancel() {
#ifdef WEASELC_LOCAL_TEST_BUILD
  scopeReadTraceContext_.Release();
#endif
  ++revision_; ready_ = false; checked_ = false;
  StopWorker(); Hide();
  context_.Release(); range_.Release();
  expectedSelection_.Release(); expectedPreedit_.clear(); expectedComposing_ = false;
  positionValid_ = false;
  measuredDpi_ = 0; measuredSize_ = {};
  if (!text_.empty()) SecureZeroMemory(text_.data(), text_.size() * sizeof(WCHAR));
  text_.clear(); label_.clear(); item_ = {};
  caret_ = {};
  if (window_) KillTimer(window_, 1);
}
#ifdef WEASELC_LOCAL_TEST_BUILD
bool AutomaticCorrection::ConsumeScopeReadTrace(ITfContext* context) {
  if(!correction::SameIdentity(scopeReadTraceContext_,context))return false;
  scopeReadTraceContext_.Release();return true;
}
#endif
bool AutomaticCorrection::EnsureWindow() {
  if (window_) return true;
  WNDCLASSW wc{};
  wc.lpfnWndProc = WindowProc; wc.hInstance = g_hInst;
  wc.lpszClassName = L"WeaseLC.AutomaticCorrection.V1";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
      wc.lpszClassName, L"WeaseLC correction hint", WS_POPUP, 0, 0, 0, 0,
      nullptr, nullptr, g_hInst, this);
  if (window_ && !SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA)) {
    DestroyWindow(window_); window_ = nullptr;
  }
  return window_ != nullptr;
}
LRESULT CALLBACK AutomaticCorrection::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  auto* self = reinterpret_cast<AutomaticCorrection*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<AutomaticCorrection*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(window, message, w, l);
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  if (message == WM_NCHITTEST) return HTTRANSPARENT;
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_SYSCOLORCHANGE || message == WM_SETTINGCHANGE || message == WM_THEMECHANGED) {
    InvalidateRect(window, nullptr, TRUE);
  }
  if (message == WM_TIMER) {
    // Host callbacks can release the last external service reference.
    auto* owner = self->owner_; owner->AddRef();
    try { self->Poll(); } catch (...) { self->Cancel(); }
    owner->Release(); return 0;
  }
  if (message == WM_PAINT || message == WM_PRINTCLIENT) {
    PAINTSTRUCT ps{};
    HDC dc = message == WM_PAINT ? BeginPaint(window, &ps) : reinterpret_cast<HDC>(w);
    RECT bounds{}; GetClientRect(window, &bounds);
    correction::PaintHint(dc, bounds, correction::AutomaticWindowDpi(window), self->preferences_.automatic.font,
        std::wstring(self->item_.original, self->item_.originalUnits),
        std::wstring(self->item_.replacement, self->item_.replacementUnits), self->preferences_.automatic.tab,
        PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE);
    if (message == WM_PAINT) EndPaint(window, &ps);
    return 0;
  }
  if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  return DefWindowProcW(window, message, w, l);
}
void AutomaticCorrection::Capture(ITfContext* context, TfEditCookie cookie,
                                  ITfRange* committed, const std::wstring& text) try {
#ifdef WEASELC_LOCAL_TEST_BUILD
  correction::FrontendDiagnostic gate;
  gate.stage = 1; gate.units = static_cast<DWORD>(text.size());
  gate.flags = (context ? 1 : 0) | (committed ? 2 : 0);
  correction::SaveFrontendDiagnostic(L"LastGate", gate);
  correction::PreferencesRead preferenceRead;
#endif
  // Carry forward only text this service just committed at the same tracked
  // insertion point. Never pull earlier document contents into the model.
  std::wstring accumulated;
  com_ptr<ITfRange> previous;
  if (correction::SameIdentity(context_, context) && range_ && committed && SettingsCurrent() &&
      GetTickCount64() - started_ < kLifetime && text_.size() + text.size() <= 1024) {
    const auto revision = revision_;
    const auto old = text_;
    com_ptr<ITfRange> oldRange = range_;
    if (owner_->_CorrectionSafeScope(context, cookie, oldRange) &&
        Same(oldRange, committed, cookie, TF_ANCHOR_END, TF_ANCHOR_START) &&
        ReadExact(oldRange, cookie, old) && revision == revision_ && oldRange->Clone(&previous) == S_OK)
      accumulated = old;
  }
  Cancel();
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (!testing_ || testingBroker_)
#endif
    if (!LoadBrokerPreferences()) {
      preferences_ = correction::Preferences{};
#ifdef WEASELC_LOCAL_TEST_BUILD
      preferenceRead.stage = correction::PreferencesStage::QueryFailed;
      preferenceRead.error = ERROR_NOT_READY;
#endif
    }
#ifdef WEASELC_LOCAL_TEST_BUILD
    else { preferenceRead.stage = correction::PreferencesStage::Ready; preferenceRead.error = ERROR_SUCCESS; }
#endif
#ifdef WEASELC_LOCAL_TEST_BUILD
  gate.source = static_cast<DWORD>(preferenceRead.stage);
  gate.error = HRESULT_FROM_WIN32(preferenceRead.error);
  gate.flags |= (static_cast<DWORD>(preferences_.mode) << 8) |
      (preferences_.automatic.enabled ? 0x10000 : 0);
#endif
  if (!preferences_.automatic.enabled || preferences_.mode != correction::Mode::Local ||
      !correction::ValidAutomaticOptions(preferences_.automatic) || !context || !committed ||
      !correction::ValidAutomaticText(text.c_str(), static_cast<unsigned>(text.size()), 1024)) {
#ifdef WEASELC_LOCAL_TEST_BUILD
    gate.stage = !preferences_.automatic.enabled || preferences_.mode != correction::Mode::Local ? 2 :
        !correction::ValidAutomaticOptions(preferences_.automatic) ? 3 : !context || !committed ? 4 : 5;
    correction::SaveFrontendDiagnostic(L"LastGate", gate);
#endif
    return;
  }
#ifdef WEASELC_LOCAL_TEST_BUILD
  gate.stage = 6;
  correction::SaveFrontendDiagnostic(L"LastGate", gate);
#endif
  Report(correction::AutomaticStage::Captured);
  context_ = context;
  if (previous && !accumulated.empty()) {
    range_ = previous;
    if (range_->ShiftEndToRange(cookie, committed, TF_ANCHOR_END) != S_OK) { Report(correction::AutomaticStage::RangeMismatch); Cancel(); return; }
    accumulated += text;
  } else {
    accumulated = text;
    if (committed->Clone(&range_) != S_OK) { Report(correction::AutomaticStage::RangeMismatch); Cancel(); return; }
  }
  if (!range_ ||
      range_->SetGravity(cookie, TF_GRAVITY_BACKWARD, TF_GRAVITY_BACKWARD) != S_OK) { Report(correction::AutomaticStage::RangeMismatch); Cancel(); return; }
  text_ = std::move(accumulated);
  started_ = GetTickCount64();
  if (!Validate(cookie, context)) { Cancel(); return; }
#ifdef WEASELC_LOCAL_TEST_BUILD
  // Validate has read back the exact owned range under the host's edit lock.
  correction::CaptureDiagnostic captured;
  captured.totalUnits = static_cast<DWORD>(text_.size());
  captured.addedUnits = static_cast<DWORD>(text.size());
  captured.retainedUnits = captured.totalUnits - captured.addedUnits;
  correction::SavePipelineDiagnostic(L"LastCapture", captured);
#endif
  if (!EnsureWindow()) { Report(correction::AutomaticStage::LayoutUnavailable); Cancel(); return; }
  RecordInput(context, cookie);
  if (!context_) return;
  nextSettings_ = started_ + 500;
  nextValidation_ = started_;
  if (!SetTimer(window_, 1, 40, nullptr)) { Report(correction::AutomaticStage::LayoutUnavailable); Cancel(); }
} catch (...) { Cancel(); }
bool AutomaticCorrection::Validate(TfEditCookie cookie, ITfContext* context) {
  if (!context_ || !correction::SameIdentity(context, context_) || !range_) {
    Report(correction::AutomaticStage::RangeMismatch); return false;
  }
  if (GetTickCount64() - started_ >= Lifetime()) { Report(correction::AutomaticStage::Expired); return false; }
  if (!Focused()) return false;
  if (!SettingsCurrent()) { Report(correction::AutomaticStage::SettingsChanged); return false; }
  const auto revision = revision_;
  const auto text = text_;
  com_ptr<ITfRange> target = range_;
  com_ptr<ITfContext> held = context_;
  TF_STATUS status{};
  if (context->GetStatus(&status) != S_OK) { Report(correction::AutomaticStage::StatusUnavailable); return false; }
  if (status.dwDynamicFlags & TF_SD_READONLY) {
    Report(correction::AutomaticStage::ReadOnly); return false;
  }
  correction::ProbeStatus scope = correction::ProbeStatus::ScopeUnavailable;
  correction::ScopeDiagnostic details;
  if (!owner_->_CorrectionSafeScope(context, cookie, target, &scope, &details)) {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    if (!testing_)
#endif
      correction::SaveScopeDiagnostic(details);
    Report(scope == correction::ProbeStatus::ProtectedField ? correction::AutomaticStage::ProtectedField :
        correction::AutomaticStage::ScopeUnavailable); return false;
  }
  if(details.stage==correction::ScopeStage::NativePlainText || details.stage==correction::ScopeStage::WordDocument)
    correction::SaveScopeDiagnostic(details);
  if (revision != revision_ || !ReadExact(target, cookie, text) || revision != revision_) {
    Report(correction::AutomaticStage::TextChanged); return false;
  }
  TF_SELECTION selection{}; ULONG count = 0;
  const HRESULT hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &count);
  com_ptr<ITfRange> selected; selected.Attach(selection.range);
  BOOL empty = FALSE;
  if (hr != S_OK || count != 1 || !selected || selection.style.fInterimChar ||
      selected->IsEmpty(cookie, &empty) != S_OK || !empty || revision != revision_) {
    Report(correction::AutomaticStage::SelectionUnavailable); return false;
  }
  if (expectedSelection_ && (!Same(expectedSelection_, selected, cookie, TF_ANCHOR_START, TF_ANCHOR_START) ||
      !Same(expectedSelection_, selected, cookie, TF_ANCHOR_END, TF_ANCHOR_END) ||
      expectedComposing_ != !!owner_->_IsComposing())) { Report(correction::AutomaticStage::SelectionMoved); return false; }
  if (owner_->_IsComposing()) {
    com_ptr<ITfRange> composition;
    com_ptr<ITfComposition> live = owner_->_pComposition;
    if (!correction::SameIdentity(owner_->_pCompositionContext, context) || !live || live->GetRange(&composition) != S_OK ||
        !composition || revision != revision_ || !Same(target, composition, cookie, TF_ANCHOR_END, TF_ANCHOR_START)) { Report(correction::AutomaticStage::RangeMismatch); return false; }
    LONG start = -1, end = 1;
    if (composition->CompareStart(cookie, selected, TF_ANCHOR_START, &start) != S_OK || start > 0 ||
        composition->CompareEnd(cookie, selected, TF_ANCHOR_END, &end) != S_OK || end < 0) { Report(correction::AutomaticStage::SelectionMoved); return false; }
    if (expectedSelection_ && !ReadExact(composition, cookie, expectedPreedit_)) { Report(correction::AutomaticStage::TextChanged); return false; }
  } else if (!Same(target, selected, cookie, TF_ANCHOR_END, TF_ANCHOR_END)) { Report(correction::AutomaticStage::SelectionMoved); return false; }
  return revision == revision_;
}
void AutomaticCorrection::Observe(ITfContext* context, TfEditCookie cookie) try {
  if (context_ && !Validate(cookie, context)) Cancel();
} catch (...) { Cancel(); }
void AutomaticCorrection::RecordInput(ITfContext* context, TfEditCookie cookie) try {
  if (!context_ || !correction::SameIdentity(context_, context)) return;
  const auto revision = revision_;
  com_ptr<ITfContext> held = context_;
  TF_SELECTION selection{}; ULONG count = 0;
  const HRESULT hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &count);
  com_ptr<ITfRange> selected; selected.Attach(selection.range);
  BOOL empty = FALSE;
  if (hr != S_OK || count != 1 || !selected || selected->IsEmpty(cookie, &empty) != S_OK || !empty ||
      selected->SetGravity(cookie, TF_GRAVITY_BACKWARD, TF_GRAVITY_BACKWARD) != S_OK) { Report(correction::AutomaticStage::RangeMismatch); Cancel(); return; }
  std::wstring preedit;
  const bool composing = !!owner_->_IsComposing();
  if (composing) {
    com_ptr<ITfComposition> live = owner_->_pComposition;
    com_ptr<ITfRange> range;
    WCHAR buffer[1025]{}; ULONG units = 0;
    if (!live || live->GetRange(&range) != S_OK || !range ||
        !owner_->_CorrectionSafeScope(context, cookie, range) ||
        range->GetText(cookie, 0, buffer, _countof(buffer), &units) != S_OK || units > 1024) { Cancel(); return; }
    preedit.assign(buffer, units); SecureZeroMemory(buffer, sizeof(buffer));
  }
  if (revision != revision_) return;
  expectedSelection_ = selected; expectedPreedit_ = std::move(preedit); expectedComposing_ = composing;
  if (!Validate(cookie, context)) { Cancel(); return; }
  ReadPosition(context, cookie);
} catch (...) { Cancel(); }
void AutomaticCorrection::ReadPosition(ITfContext* context, TfEditCookie cookie) {
  const auto revision = revision_;
  TF_SELECTION selection{}; ULONG count = 0;
  const auto hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &count);
  com_ptr<ITfRange> selected; selected.Attach(selection.range);
  com_ptr<ITfContextView> view; BOOL clipped = TRUE; RECT rect{};
  HWND host = nullptr;
  const bool available = hr == S_OK && count == 1 && selected &&
      context->GetActiveView(&view) == S_OK && view &&
      view->GetTextExt(cookie, selected, &rect, &clipped) == S_OK && !clipped && rect.bottom > rect.top;
  if (revision != revision_) return;
  positionValid_ = available;
  if (available) {
    caret_ = rect;
    if (view->GetWnd(&host) == S_OK && host) anchorDpi_ = correction::AutomaticWindowDpi(host);
  } else Hide();
}
void AutomaticCorrection::LayoutChanged(ITfContext* context, bool destroyed) {
  if (!context_ || !correction::SameIdentity(context_, context)) return;
  if (destroyed) { Cancel(); return; }
  positionValid_ = false; Hide(); nextValidation_ = 0;
}
HRESULT AutomaticCorrection::Edit(TfEditCookie cookie, ITfContext* context, bool apply, bool* applied, WriteVerification* verification) {
  if (applied) *applied = false;
  if (!Validate(cookie, context)) { Cancel(); return S_FALSE; }
  ReadPosition(context, cookie);
  // A layout/candidate can disappear between timer ticks or between the TSF
  // query and actual key callbacks. Recheck presentation under this edit lock.
  Refresh();
  if (!apply) return S_OK;
  if (!positionValid_) { Hide(); return S_FALSE; }
  if (!ready_ || !window_ || !IsWindowVisible(window_) || !preferences_.automatic.tab) return S_FALSE;
  const auto revision = revision_;
  const auto item = item_;
  com_ptr<ITfRange> change;
  com_ptr<ITfRange> target = range_;
  com_ptr<ITfContext> held = context_;
  if (!target || target->Clone(&change) != S_OK || !change) return S_FALSE;
  LONG moved = 0;
  if (change->ShiftStart(cookie, item.start, &moved, nullptr) != S_OK || moved != static_cast<LONG>(item.start)) return S_FALSE;
  if (change->Collapse(cookie, TF_ANCHOR_START) != S_OK ||
      change->ShiftEnd(cookie, item.originalUnits, &moved, nullptr) != S_OK || moved != static_cast<LONG>(item.originalUnits) ||
      !ReadExact(change, cookie, std::wstring(item.original, item.originalUnits))) return S_FALSE;
  TF_SELECTION selection{}; ULONG count = 0;
  const HRESULT selected = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &count);
  com_ptr<ITfRange> restore; restore.Attach(selection.range);
  if (selected != S_OK || count != 1 || !restore) return S_FALSE;
  if (revision != revision_ || !Validate(cookie, context)) return S_FALSE;
  if (owner_->_IsComposing()) {
    // A browser can synchronize only the final live preedit at lock exit.
    // Expanding and shrinking it inside one lock hides prefix edits from that
    // synchronization. End TSF composition now, keep both its text and Rime's
    // input buffer, and let Request run the write/resume under separate locks.
    if (!verification) return E_POINTER;
    auto composition = owner_->_pComposition;
    WriteVerification pending;
    pending.range = change; pending.source = target; pending.selection = restore;
    pending.original.assign(item.original, item.originalUnits);
    pending.text.assign(item.replacement, item.replacementUnits);
    pending.sourceText = text_; pending.preeditText = expectedPreedit_;
    pending.offset = item.start;
    pending.generation = owner_->_GetEditGeneration();
    if (!composition || composition->GetRange(&pending.preedit) != S_OK || !pending.preedit ||
        !ReadExact(pending.preedit, cookie, pending.preeditText)) return S_FALSE;
    if (revision != revision_ || !owner_->_IsEditSessionCurrent(pending.generation) ||
        !owner_->_IsCurrentComposition(composition) || !Focused(context)) return S_FALSE;
    Cancel(); pending.revision = revision_;
    owner_->_ClearCompositionDisplayAttributes(cookie, context, composition);
    if (!Focused(context) || !owner_->_IsEditSessionCurrent(pending.generation) ||
        revision_ != pending.revision || !owner_->_IsCurrentComposition(composition)) return S_FALSE;
    const auto ended = owner_->_EndTrackedComposition(cookie, composition);
    if (ended != S_OK) return ended;
    owner_->_FinalizeComposition(composition);
    pending.detached = true;
    *verification = std::move(pending);
    // Consume exactly once after a successful first mutation, even if a later
    // host lock fails. Falling through to normal Tab would add a second action.
    if (applied) *applied = true;
    Report(correction::AutomaticStage::TabRestoreUnconfirmed);
    return S_OK;
  }
  // No preedit: retain the existing single-write path, with a temporary TSF
  // correction composition and a separate post-lock verification.
  const auto generation = owner_->_GetEditGeneration();
  Cancel();
  const auto writeRevision = revision_;
  // A host may reenter us while changing composition properties. No write may
  // escape the document/lifecycle checked before that callback.
  if (!Focused(context) || revision_ != writeRevision || !owner_->_IsEditSessionCurrent(generation)) return S_FALSE;
  const HRESULT hr = change->SetText(cookie, TF_ST_CORRECTION, item.replacement, item.replacementUnits);
  // An acknowledged write consumes this key once, even if verification fails:
  // retrying or falling through could duplicate a write already made by a host.
  // This flag is NOT evidence that the host retained the replacement.
  if (SUCCEEDED(hr) && applied) *applied = true;
  bool written = false;
  if (SUCCEEDED(hr)) {
    Report(correction::AutomaticStage::TabWriteUnconfirmed);
    written = hr == S_OK && ReadExact(change, cookie, std::wstring(item.replacement, item.replacementUnits));
    if (written) Report(correction::AutomaticStage::TabRestoreUnconfirmed);
  }
  if (FAILED(hr)) return hr;
  selection.range = restore;
  const HRESULT restored = owner_->_IsEditSessionCurrent(generation) ? context->SetSelection(cookie, 1, &selection) : TF_E_DISCONNECTED;
  if (written && restored == S_OK && owner_->_IsEditSessionCurrent(generation) && Focused(context) &&
      ReadExact(change, cookie, std::wstring(item.replacement, item.replacementUnits))) {
    Report(correction::AutomaticStage::TabPersistenceUnconfirmed);
    if (verification) {
      verification->range = change;
      verification->text.assign(item.replacement, item.replacementUnits);
      verification->generation = generation;
    }
  }
  // An in-lock read may still be a TSF cache. Request() verifies again only
  // AFTER this write session returns, under a separate synchronous READ lock.
  return S_OK;
}
HRESULT AutomaticCorrection::ContinueWrite(TfEditCookie cookie, ITfContext* context,
                                           WriteVerification& pending, bool resume) {
  if (!pending.detached || !pending.range || !pending.source || !pending.selection || !pending.preedit ||
      !owner_->_IsEditSessionCurrent(pending.generation) || revision_ != pending.revision ||
      owner_->_IsComposing() || !Focused(context) || !SettingsCurrent()) return S_FALSE;
  if (!owner_->_CorrectionSafeScope(context, cookie, pending.source) ||
      !owner_->_CorrectionSafeScope(context, cookie, pending.preedit)) return S_FALSE;
  if (!ReadExact(pending.source, cookie, pending.sourceText)) {
    if (pending.written) Report(correction::AutomaticStage::TabPersistenceUnconfirmed);
    return S_FALSE;
  }
  if (!ReadExact(pending.preedit, cookie, pending.preeditText)) return S_FALSE;
  TF_SELECTION selected{}; ULONG count = 0;
  const auto got = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selected, &count);
  com_ptr<ITfRange> selection; selection.Attach(selected.range);
  if (got != S_OK || count != 1 || !selection || selected.style.fInterimChar ||
      !Same(selection, pending.selection, cookie, TF_ANCHOR_START, TF_ANCHOR_START) ||
      !Same(selection, pending.selection, cookie, TF_ANCHOR_END, TF_ANCHOR_END) ||
      !owner_->_IsEditSessionCurrent(pending.generation) || revision_ != pending.revision || !Focused(context)) return S_FALSE;
  if (resume) {
    // Start over the existing tracked text, NOT at the current caret. No input
    // is reinserted and no Rime key/candidate/learning event is replayed.
    com_ptr<ITfContextComposition> compositions;
    com_ptr<ITfComposition> composition;
    if (context->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&compositions)) != S_OK || !compositions)
      return E_NOINTERFACE;
    const auto started = compositions->StartComposition(cookie, pending.preedit, owner_, &composition);
    if (started != S_OK || !composition) return started == S_OK ? E_FAIL : started;
    if (!owner_->_IsEditSessionCurrent(pending.generation) || revision_ != pending.revision ||
        !Focused(context) || owner_->_IsComposing()) {
      owner_->_EndTrackedComposition(cookie, composition); return TF_E_DISCONNECTED;
    }
    owner_->_SetComposition(composition, context);
    owner_->_SetCompositionDisplayAttributes(cookie, context, pending.preedit);
    selected.range = pending.selection;
    if (!owner_->_IsEditSessionCurrent(pending.generation) || revision_ != pending.revision || !Focused(context) ||
        !owner_->_IsCurrentComposition(composition) ||
        context->SetSelection(cookie, 1, &selected) != S_OK ||
        !owner_->_IsEditSessionCurrent(pending.generation) || revision_ != pending.revision ||
        !owner_->_IsCurrentComposition(composition) || !Focused(context)) {
      owner_->_EndTrackedComposition(cookie, composition); owner_->_FinalizeComposition(composition);
      return E_FAIL;
    }
    pending.resumed = true;
    return S_OK;
  }
  if (!ReadExact(pending.range, cookie, pending.original)) return S_FALSE;
  if (!owner_->_IsEditSessionCurrent(pending.generation) || revision_ != pending.revision ||
      owner_->_IsComposing() || !Focused(context)) return S_FALSE;
  // Default TSF correction composition is now possible: our preedit is absent.
  // Never retry an acknowledged write, including S_FALSE or a failed readback.
  const auto written = pending.range->SetText(cookie, TF_ST_CORRECTION,
      pending.text.data(), static_cast<LONG>(pending.text.size()));
  if (written != S_OK) return written;
  Report(correction::AutomaticStage::TabWriteUnconfirmed);
  if (!ReadExact(pending.range, cookie, pending.text)) return S_FALSE;
  pending.written = true;
  Report(correction::AutomaticStage::TabRestoreUnconfirmed);
  pending.sourceText.replace(pending.offset, pending.original.size(), pending.text);
  selected.range = pending.selection;
  if (!owner_->_IsEditSessionCurrent(pending.generation) || revision_ != pending.revision || !Focused(context)) return S_FALSE;
  return context->SetSelection(cookie, 1, &selected);
}
HRESULT AutomaticCorrection::VerifyWrite(TfEditCookie cookie, ITfContext* context, const WriteVerification& verification) {
  if (!verification.range || verification.text.empty() || verification.text.size() > 32 ||
      !owner_->_IsEditSessionCurrent(verification.generation) || !Focused(context) ||
      !owner_->_CorrectionSafeScope(context, cookie, verification.range)) return S_FALSE;
  if (verification.detached) {
    com_ptr<ITfRange> live;
    auto composition = owner_->_pComposition;
    if (!verification.resumed || !composition || !correction::SameIdentity(owner_->_pCompositionContext, context) ||
        composition->GetRange(&live) != S_OK || !live ||
        !Same(live, verification.preedit, cookie, TF_ANCHOR_START, TF_ANCHOR_START) ||
        !Same(live, verification.preedit, cookie, TF_ANCHOR_END, TF_ANCHOR_END) ||
        !ReadExact(live, cookie, verification.preeditText)) return S_FALSE;
  }
  const bool retained = ReadExact(verification.range, cookie, verification.text);
  if (!owner_->_IsEditSessionCurrent(verification.generation) || !Focused(context)) return S_FALSE;
  Report(retained ? correction::AutomaticStage::TabApplied : correction::AutomaticStage::TabPersistenceUnconfirmed);
  return retained ? S_OK : S_FALSE;
}
bool AutomaticCorrection::Request(bool apply, ITfContext* context) {
  if (requesting_ || !context_ || !correction::SameIdentity(context_, context)) return false;
  com_ptr<AutomaticEdit> session;
  requesting_ = true;
  struct Guard { bool& flag; ~Guard() { flag = false; } } guard{requesting_};
  session.Attach(new AutomaticEdit(owner_, context, apply));
  HRESULT edit = E_FAIL;
  const HRESULT hr = context->RequestEditSession(Client(), session,
      TF_ES_SYNC | (apply ? TF_ES_READWRITE : TF_ES_READ), &edit);
  if (FAILED(hr)) Report(correction::AutomaticStage::RequestDenied, {correction::FocusStage::Ready, hr});
  if (apply && session->verification.detached) {
    auto& pending = session->verification;
    auto step = [&](bool resume) {
      com_ptr<AutomaticStep> next;
      next.Attach(new AutomaticStep(owner_, context, [&](TfEditCookie cookie) {
        return ContinueWrite(cookie, context, pending, resume);
      }));
      HRESULT result = E_FAIL;
      HRESULT requested = E_FAIL;
      try { requested = context->RequestEditSession(Client(), next, TF_ES_SYNC | TF_ES_READWRITE, &result); }
      catch (...) { next->Retire(); throw; }
      next->Retire();
      return requested == S_OK && result == S_OK;
    };
    bool written = false;
    try { written = step(false); step(true); } catch (...) {}
    if (!pending.resumed && owner_->_IsEditSessionCurrent(pending.generation) && !owner_->_IsComposing()) {
      // If resumption is refused, leave host text intact and release only this
      // backend preedit. Never queue a late write or clear a newer composition.
      owner_->m_client.ClearComposition(); owner_->_cand->EndUI();
    }
    if ((!written || !pending.resumed) && pending.written &&
        diagnostic_.stage != correction::AutomaticStage::TabPersistenceUnconfirmed)
      Report(correction::AutomaticStage::TabRestoreUnconfirmed);
    if (!pending.written) pending.range.Release();
  }
  if (apply && session->applied && hr == S_OK && edit == S_OK && session->verification.range &&
      (!session->verification.detached || session->verification.resumed)) {
    // Verification is diagnostic only. Allocation/host failures after an
    // acknowledged write must never turn this key into a second normal edit.
    try {
      com_ptr<AutomaticVerify> verify;
      verify.Attach(new AutomaticVerify(owner_, context, session->verification));
      HRESULT verified = E_FAIL;
      const HRESULT requested = context->RequestEditSession(Client(), verify, TF_ES_SYNC | TF_ES_READ, &verified);
      if (requested != S_OK || verified != S_OK)
        Report(correction::AutomaticStage::TabPersistenceUnconfirmed,
            {correction::FocusStage::Ready, requested != S_OK ? requested : verified});
    } catch (...) { Report(correction::AutomaticStage::TabPersistenceUnconfirmed); }
  }
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_ && apply && !session->applied) fprintf(stderr, "Automatic write request/edit: %08lx/%08lx\n", hr, edit);
#endif
  // SYNC only: a refused lock must never leave a delayed document write queued.
  return apply ? session->applied : hr == S_OK && edit == S_OK;
}
void AutomaticCorrection::Position(const RECT& caret) {
  // Candidate position alone does not prove that the host caret is visible.
  if (positionValid_) caret_ = caret;
  Refresh();
}
void AutomaticCorrection::Refresh() {
  if (ready_ && GetTickCount64() - started_ >= Lifetime()) { Cancel(); return; }
  if (!ready_ || !window_) { Hide(); return; }
  if (!positionValid_) { Report(correction::AutomaticStage::LayoutUnavailable); Hide(); return; }
  if (!Focused()) { Hide(); return; }
  if (!preferences_.timing && !owner_->_IsComposing()) {
    Report(correction::AutomaticStage::WaitingComposition); Hide(); return;
  }
  RECT anchor = caret_;
  UINT scale = anchorDpi_;
  if (owner_->_IsComposing()) {
    RECT candidate{};
    bool requireCandidate = true;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    requireCandidate = !testing_ || strictCandidateTest_;
#endif
    // Keep the failure branch outside #ifdef: production must retain the else.
    if (owner_->_cand->WindowRect(candidate)) {
      anchor = candidate;
      const auto dpi = owner_->_cand->WindowDpi();
      if (dpi) scale = dpi;
    }
    else if (requireCandidate) {
      Report(correction::AutomaticStage::CandidateUnavailable); Hide(); return;
    }
  }
  if (anchor.bottom <= anchor.top) { Hide(); return; }
  MONITORINFO monitor{sizeof(monitor)};
  if (!GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor)) { Hide(); return; }
  // Immediate hints use the host caret, not the DPI of a hidden old candidate.
  const auto& options = preferences_.automatic;
  SIZE size{(std::min)(static_cast<LONG>(MulDiv(options.width, scale, 96)), monitor.rcWork.right - monitor.rcWork.left), 0};
  if (size.cx != measuredSize_.cx || scale != measuredDpi_) {
    HDC dc = GetDC(window_);
    const auto layout = correction::MeasureHint(dc, size.cx, scale, options.font,
        std::wstring(item_.original, item_.originalUnits), std::wstring(item_.replacement, item_.replacementUnits),
        options.tab, PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE);
    ReleaseDC(window_, dc);
    measuredSize_ = {size.cx, layout.complete ? layout.height : -1}; measuredDpi_ = scale;
  }
  size.cy = measuredSize_.cy;
  if (size.cy <= 0 || size.cy > monitor.rcWork.bottom - monitor.rcWork.top) { Hide(); return; }
  const RECT placed = correction::PlaceAutomaticHint(anchor, monitor.rcWork, size, options.side, MulDiv(options.gap, scale, 96));
  if (IsRectEmpty(&placed)) { Hide(); return; }
  RECT previous{}; GetWindowRect(window_, &previous);
  if (EqualRect(&previous, &placed) && IsWindowVisible(window_)) return;
  InvalidateRect(window_, nullptr, TRUE);
  if (!SetWindowPos(window_, HWND_TOPMOST, placed.left, placed.top, placed.right - placed.left,
      placed.bottom - placed.top, SWP_NOACTIVATE | SWP_SHOWWINDOW) || !IsWindowVisible(window_)) {
    Report(correction::AutomaticStage::LayoutUnavailable); Hide(); return;
  }
  Report(correction::AutomaticStage::Shown);
}
bool AutomaticCorrection::IntegratedKey(WPARAM key, LPARAM flags, BOOL* eaten) {
  // The integrated-candidate callback is an actual key offer, not a TSF Test
  // callback. Only claim the correction shortcut; ordinary candidate/navigation
  // keys remain available to the application's integration implementation.
  if (key != VK_TAB && key != VK_ESCAPE && key != VK_LEFT && key != VK_RIGHT &&
      key != VK_HOME && key != VK_END && key != VK_DELETE && key != VK_BACK &&
      key != VK_SHIFT && key != VK_CONTROL && key != VK_MENU) return false;
  com_ptr<ITfContext> target = context_;
  return Key(target, key, flags, false, false, eaten);
}
bool AutomaticCorrection::Key(ITfContext* context, WPARAM key, LPARAM flags, bool test, bool up, BOOL* eaten) try {
  if (!eaten) return false;
  if (key != VK_TAB) {
    if (!up && (key == VK_ESCAPE || key == VK_LEFT || key == VK_RIGHT || key == VK_HOME ||
        key == VK_END || key == VK_DELETE || key == VK_BACK || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU)) Cancel();
    else if (!up && context_ && !Request(false, context)) Cancel();
    return false;
  }
  if (up) {
    if (!downTab_) return false;
    *eaten = TRUE;
    if (!test) downTab_ = false;
    return true;
  }
  if (downTab_) { *eaten = TRUE; return true; }  // Don't accept twice on autorepeat.
  const bool modifiers = (GetKeyState(VK_SHIFT) | GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU) | GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000;
  if (modifiers || (flags & (1LL << 30)) || !ready_ || !preferences_.automatic.tab ||
      !window_ || !IsWindowVisible(window_) || !Focused()) return false;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (!testing_)
#endif
    if (owner_->_status.ascii_mode || owner_->_status.disabled || owner_->_IsKeyboardDisabled() ||
        (owner_->_isToOpenClose && !owner_->_IsKeyboardOpen())) { Cancel(); return false; }
  if (test) {
    if (!Request(false, context)) { Cancel(); return false; }
    if (!ready_ || !window_ || !IsWindowVisible(window_)) return false;
    *eaten = TRUE; return true;
  }
  if (!Request(true, context)) { Cancel(); return false; }
  downTab_ = true; *eaten = TRUE; return true;
} catch (...) { Cancel(); return false; }
bool AutomaticCorrection::AcceptReply(const correction::AutomaticReply& reply) {
  if (reply.magic != 0x31524357 || reply.version != 1 || reply.count > 8) return false;
  for (unsigned i = 0; i < reply.count; ++i) {
    const auto& item = reply.items[i];
    if (!correction::ValidAutomaticText(item.original, item.originalUnits, 32) ||
        !correction::ValidAutomaticText(item.replacement, item.replacementUnits, 32) ||
        !std::isfinite(item.score) || item.score < (preferences_.local.useCustomThreshold ? preferences_.local.minimumScore :
            preferences_.local.sensitivity == 1 ? .80 : preferences_.local.sensitivity == 2 ? .65 : .90) || item.score > 1 ||
        item.start > text_.size() || item.originalUnits > text_.size() - item.start ||
        text_.compare(item.start, item.originalUnits, item.original, item.originalUnits) != 0 ||
        std::wstring(item.original, item.originalUnits) == std::wstring(item.replacement, item.replacementUnits)) return false;
  }
  checked_ = true;
  if (!reply.count) {
    // No displayable suggestion is a completed check, not a document reset.
    // Retain only our bounded, range-validated commits for the next word. A
    // changed focus/text/selection, timeout or new settings still cancels them.
    ready_ = false; item_ = {}; label_.clear(); Hide();
    Report(correction::AutomaticStage::NoSuggestion); return true;
  }
  item_ = reply.items[0];
  measuredDpi_ = 0; measuredSize_ = {};
  label_ = std::wstring(item_.original, item_.originalUnits) + L" → " + std::wstring(item_.replacement, item_.replacementUnits);
  const bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
  label_ += preferences_.automatic.tab ? (chinese ? L"\nTab 接受 · Esc 忽略" : L"\nTab accepts · Esc dismisses") :
      (chinese ? L"\n纠错建议（Tab 已关闭）" : L"\nCorrection (Tab disabled)");
  SetWindowTextW(window_, (std::wstring(correction::HintTitle(chinese)) + L": " + label_).c_str());
  InvalidateRect(window_, nullptr, TRUE);
  ready_ = true; Report(correction::AutomaticStage::Ready); return true;
}
bool AutomaticCorrection::StartWorker() {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (!testing_ || testingBroker_)
#endif
  {
    correction::BrokerRequest request; request.operation = correction::BrokerOperation::Check;
    request.requestId = ++brokerRequestId_; request.revision = brokerRevision_;
    request.input.units = static_cast<DWORD>(text_.size());
    memcpy(request.input.text, text_.data(), text_.size() * sizeof(WCHAR));
    const bool sent = brokerCall_.Begin(BrokerServer(), request);
    SecureZeroMemory(&request, sizeof(request)); return sent;
  }
#ifndef WEASEL_CORRECTION_PROBE_BUILD
  return false;
#else
  // Pipes remain below the 4 KiB capacity, so the one initial write cannot wait
  // for model execution. Only the worker's explicit three standard handles inherit.
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE input = nullptr, write = nullptr, outputWrite = nullptr, error = nullptr;
  STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  SIZE_T bytes = 0;
  std::vector<BYTE> attributes;
  std::wstring executable;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_) executable = testWorker_;
  else
#endif
  {
    WCHAR module[32768]{};
    const DWORD count = GetModuleFileNameW(g_hInst, module, _countof(module));
    if (!count || count == _countof(module)) return false;
    WCHAR windows[32768]{};
    const auto length = GetWindowsDirectoryW(windows, _countof(windows));
    if (!length || length >= _countof(windows)) return false;
    const auto installed = correction::SystemCorrectionModule(module, windows);
    executable = correction::CorrectionWorkerPath(module, windows,
        installed ? correction::InstalledCorrectionRoot() : L"");
  }
  const auto fileAttributes = executable.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(executable.c_str());
  if (fileAttributes == INVALID_FILE_ATTRIBUTES || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
  const auto directory = executable.substr(0, executable.find_last_of(L"\\/"));
  auto cleanup = [&]() {
    if (startup.lpAttributeList) DeleteProcThreadAttributeList(startup.lpAttributeList);
    Close(input); Close(write); Close(outputWrite); Close(error); Close(process.hThread); Close(process.hProcess);
  };
  if (!CreatePipe(&input, &write, &security, 4096) || !CreatePipe(&output_, &outputWrite, &security, 4096) ||
      !SetHandleInformation(write, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0)) {
    cleanup(); StopWorker(); return false;
  }
  error = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  attributes.resize(bytes);
  auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (error == INVALID_HANDLE_VALUE || !InitializeProcThreadAttributeList(list, 1, 0, &bytes)) { cleanup(); StopWorker(); return false; }
  startup.lpAttributeList = list;
  HANDLE inherited[]{input, outputWrite, error};
  if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
    cleanup(); StopWorker(); return false;
  }
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.dwFlags |= STARTF_FORCEOFFFEEDBACK;
  startup.StartupInfo.hStdInput = input; startup.StartupInfo.hStdOutput = outputWrite; startup.StartupInfo.hStdError = error;
  job_ = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  std::wstring command = L"\"" + executable + L"\" /correction-worker";
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_) command += L"-selftest";
#endif
  if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
      !CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
          CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(),
          &startup.StartupInfo, &process)) { cleanup(); StopWorker(); return false; }
  if (!AssignProcessToJobObject(job_, process.hProcess)) {
    TerminateProcess(process.hProcess, 1); cleanup(); StopWorker(); return false;
  }
  correction::AutomaticRequest request;
  request.units = static_cast<unsigned>(text_.size());
  memcpy(request.text, text_.data(), text_.size() * sizeof(WCHAR));
  DWORD written = 0;
  const bool sent = WriteFile(write, &request, sizeof(request), &written, nullptr) && written == sizeof(request);
  SecureZeroMemory(&request, sizeof(request));
  if (!sent || ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
    cleanup(); StopWorker(); return false;
  }
  process_ = process.hProcess; process.hProcess = nullptr;
  cleanup(); return true;
#endif
}
void AutomaticCorrection::Poll() {
  if (polling_) return;
  polling_ = true;
  struct Guard { bool& flag; ~Guard() { flag = false; } } guard{polling_};
  const auto now = GetTickCount64();
  if (!Focused()) { ResetKeys(); Cancel(); return; }
  if (now - started_ >= Lifetime()) { Report(correction::AutomaticStage::Expired); Cancel(); return; }
  if (now >= nextValidation_) {
    if (!Request(false, context_)) { Cancel(); return; }
    nextValidation_ = now + 120;
  }
  if (now >= nextSettings_) {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    if (!testing_ || testingBroker_)
#endif
      if (!SettingsCurrent()) { Cancel(); return; }
    nextSettings_ = now + 500;
  }
  if (!checked_ && !ready_ && !process_ && !brokerCall_.Active() && now - started_ >= preferences_.automatic.debounce) {
    if (!Request(false, context_)) { Cancel(); return; }
    if (!StartWorker()) { Report(correction::AutomaticStage::WorkerFailed); Cancel(); return; }
    Report(correction::AutomaticStage::WorkerStarted);
  }
  if (brokerCall_.Active()) {
    correction::BrokerReply reply;
    const int state = brokerCall_.Poll(reply);
    if (state < 0) { Report(correction::AutomaticStage::WorkerFailed); Cancel(); return; }
    if (state == 1) {
      const bool valid = reply.status == correction::BrokerStatus::Ok &&
          reply.settings.revision == brokerRevision_ &&
          (reply.modelState == static_cast<DWORD>(correction::State::Suggestions) ||
           reply.modelState == static_cast<DWORD>(correction::State::NoSuggestion) ||
           reply.modelState == static_cast<DWORD>(correction::State::Filtered));
      if (!valid) { SecureZeroMemory(&reply, sizeof(reply)); Report(correction::AutomaticStage::WorkerFailed); Cancel(); return; }
      const bool accepted = AcceptReply(reply.result);
      SecureZeroMemory(&reply, sizeof(reply));
      if (!accepted) { Report(correction::AutomaticStage::InvalidReply); Cancel(); return; }
      if (!Request(false, context_)) { Cancel(); return; }
    }
  }
  if (process_) {
    DWORD available = 0;
    if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr)) { Report(correction::AutomaticStage::WorkerFailed); Cancel(); return; }
    if (available < sizeof(correction::AutomaticReply) && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
      // The process may have written/exited between the first peek and wait.
      if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr) || available < sizeof(correction::AutomaticReply)) { Report(correction::AutomaticStage::WorkerFailed); Cancel(); return; }
    }
    if (available > sizeof(correction::AutomaticReply)) { Report(correction::AutomaticStage::InvalidReply); Cancel(); return; }
    if (available == sizeof(correction::AutomaticReply)) {
      correction::AutomaticReply reply; DWORD read = 0;
      const bool received = ReadFile(output_, &reply, sizeof(reply), &read, nullptr) && read == sizeof(reply);
      StopWorker();
      if (!received || !AcceptReply(reply)) {
        Report(correction::AutomaticStage::InvalidReply);
        SecureZeroMemory(&reply, sizeof(reply)); Cancel(); return;
      }
      if (!Request(false, context_)) {
        SecureZeroMemory(&reply, sizeof(reply)); Cancel(); return;
      }
      SecureZeroMemory(&reply, sizeof(reply));
    }
  }
  if (ready_) Refresh();
}
bool AutomaticCorrection::SettingsCurrent() const {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_ && !testingBroker_) return true;
#endif
  correction::BrokerRequest request; correction::BrokerReply reply; correction::BrokerCall call;
  return call.Exchange(BrokerServer(), request, reply) &&
      reply.status == correction::BrokerStatus::Ok && reply.settings.enabled &&
      reply.settings.revision == brokerRevision_;
}
bool AutomaticCorrection::LoadBrokerPreferences() {
  correction::BrokerRequest request; correction::BrokerReply reply; correction::BrokerCall call;
  if (!call.Exchange(BrokerServer(), request, reply) ||
      reply.status != correction::BrokerStatus::Ok) return false;
  const auto& saved = reply.settings;
  correction::Preferences value;
  value.mode = saved.enabled ? correction::Mode::Local : correction::Mode::Off;
  value.timing = saved.timing;
  auto& a = value.automatic; a.enabled = saved.enabled != 0; a.tab = saved.tab != 0;
  a.side = saved.side; a.gap = saved.gap; a.width = saved.width; a.font = saved.font; a.debounce = saved.debounce;
  if (!saved.revision || saved.enabled > 1 || saved.tab > 1 || saved.timing > 1 ||
      saved.timeoutSeconds < 2 || saved.timeoutSeconds > 60 || !std::isfinite(saved.threshold) ||
      saved.threshold < 0 || saved.threshold > 1 || !correction::ValidAutomaticOptions(a)) return false;
  value.local.timeoutSeconds = saved.timeoutSeconds;
  value.local.useCustomThreshold = true; value.local.minimumScore = saved.threshold;
  preferences_ = std::move(value); brokerRevision_ = saved.revision;
  return true;
}
ULONGLONG AutomaticCorrection::Lifetime() const {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_ && !testingBroker_) return kLifetime;
#endif
  return (std::max)(kLifetime, preferences_.local.timeoutSeconds * 1000ULL + preferences_.automatic.debounce + 2000);
}
std::wstring AutomaticCorrection::BrokerServer() const {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_ && testingBroker_) return testWorker_;
#endif
  return correction::CorrectionBrokerServerPath();
}
#ifdef WEASEL_CORRECTION_PROBE_BUILD
bool AutomaticCorrection::CandidateTest(DWORD mode, RECT& position) {
  if (!testing_) return false;
  strictCandidateTest_ = mode != 0;
  const bool result = owner_->_cand->TestPresentation(mode, position);
  // Mode 4 models a candidate disappearing between the timer and a key event.
  if (mode != 4) Refresh();
  return result;
}
void AutomaticCorrection::ConfigureTest(const correction::Preferences& preferences, TfClientId client, const std::wstring& worker) {
  Cancel(); ResetKeys(); testing_ = true; testFocused_ = true;
  strictCandidateTest_ = false;
  testingBroker_ = worker.compare(0, 7, L"broker:") == 0;
  preferences_ = preferences; testClient_ = client; testWorker_ = testingBroker_ ? worker.substr(7) : worker;
}
bool AutomaticCorrection::InjectTest(const correction::AutomaticReply& reply) {
  if (!testing_ || testingBroker_ || !context_ || !AcceptReply(reply)) return false;
  if (!Request(false, context_)) { Cancel(); return false; }
  Refresh(); return true;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticConfigure(
    ITfTextInputProcessorEx* service, const correction::Preferences* preferences,
    TfClientId client, const WCHAR* worker) {
  if (!service || !preferences || client == TF_CLIENTID_NULL) return E_INVALIDARG;
  auto* owner = static_cast<WeaselTSF*>(service);
  // The unactivated service must use the diagnostic host's client for its own
  // deferred composition cleanup too, not just for automatic-correction locks.
  owner->ProbeSetEditClient(client);
  owner->Automatic().ConfigureTest(*preferences, client, worker ? worker : L"");
  return S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticInject(
    ITfTextInputProcessorEx* service, const correction::AutomaticReply* reply) {
  if (!service || !reply) return E_INVALIDARG;
  return static_cast<WeaselTSF*>(service)->Automatic().InjectTest(*reply) ? S_OK : S_FALSE;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticState(
    ITfTextInputProcessorEx* service, DWORD* ready, HWND* window) {
  if (!service || !ready || !window) return E_INVALIDARG;
  auto& automatic = static_cast<WeaselTSF*>(service)->Automatic();
  *ready = automatic.TestReady(); *window = automatic.TestWindow(); return S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticDiagnostic(
    ITfTextInputProcessorEx* service, correction::AutomaticDiagnostic* result) {
  if (!service || !result) return E_INVALIDARG;
  *result = static_cast<WeaselTSF*>(service)->Automatic().DiagnosticTest(); return S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticCandidate(
    ITfTextInputProcessorEx* service, DWORD mode, RECT* position) {
  if (!service || !position || mode > 4) return E_INVALIDARG;
  auto& automatic = static_cast<WeaselTSF*>(service)->Automatic();
  return automatic.CandidateTest(mode, *position) ? S_OK : S_FALSE;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticPoll(ITfTextInputProcessorEx* service) {
  if (!service) return E_INVALIDARG;
  static_cast<WeaselTSF*>(service)->Automatic().Poll(); return S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticExpire(ITfTextInputProcessorEx* service) {
  if (!service) return E_INVALIDARG;
  static_cast<WeaselTSF*>(service)->Automatic().ExpireTest(); return S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticEnvironment(
    ITfTextInputProcessorEx* service, BOOL focused, DWORD* process) {
  if (!service || !process) return E_INVALIDARG;
  auto& automatic = static_cast<WeaselTSF*>(service)->Automatic();
  automatic.FocusTest(focused != FALSE); *process = automatic.ProcessTest(); return S_OK;
}
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticKey(
    ITfTextInputProcessorEx* service, ITfContext* context, WPARAM key, LPARAM flags, DWORD operation, BOOL* eaten) {
  if (!service || !context || !eaten) return E_INVALIDARG;
  *eaten = FALSE;
  return static_cast<WeaselTSF*>(service)->Automatic().Key(context, key, flags,
      operation & 1, operation & 2, eaten) ? S_OK : S_FALSE;
}
#endif
