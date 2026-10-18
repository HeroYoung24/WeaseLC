#include "stdafx.h"
#include "AutomaticCorrection.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "EditSession.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <CorrectionProbe.h>
#include <CorrectionHintView.h>
#include <CorrectionDeployment.h>

namespace {
constexpr ULONGLONG kLifetime = 10000;
bool SettingsTime(FILETIME& time) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, correction::kRegistryKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
  const auto status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr, nullptr, nullptr, nullptr, &time);
  RegCloseKey(key); return status == ERROR_SUCCESS;
}
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
    try { return _pTextService->Automatic().Edit(cookie, _pContext, apply_, &applied); }
    catch (...) { _pTextService->Automatic().Cancel(); return applied ? S_OK : E_FAIL; }
  }
  bool applied = false;
 private:
  bool apply_;
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
  // Closing this job terminates only the child we created, including on teardown.
  Close(job_); Close(process_); Close(output_);
}
void AutomaticCorrection::Cancel() {
  ++revision_; ready_ = false;
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
  if (!testing_)
#endif
    preferences_ = correction::LoadPreferences();
  if (!preferences_.automatic.enabled || preferences_.mode != correction::Mode::Local ||
      !correction::ValidAutomaticOptions(preferences_.automatic) || !context || !committed ||
      !correction::ValidAutomaticText(text.c_str(), static_cast<unsigned>(text.size()), 1024)) return;
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
  SettingsTime(settingsWrite_);
  if (!Validate(cookie, context)) { Cancel(); return; }
  if (!EnsureWindow()) { Report(correction::AutomaticStage::LayoutUnavailable); Cancel(); return; }
  RecordInput(context, cookie);
  if (!context_) return;
  nextSettings_ = started_ + 500;
  nextValidation_ = started_;
  SetTimer(window_, 1, 40, nullptr);
} catch (...) { Cancel(); }
bool AutomaticCorrection::Validate(TfEditCookie cookie, ITfContext* context) {
  if (!context_ || !correction::SameIdentity(context, context_) || !range_) {
    Report(correction::AutomaticStage::RangeMismatch); return false;
  }
  if (GetTickCount64() - started_ >= kLifetime) { Report(correction::AutomaticStage::Expired); return false; }
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
  if (!owner_->_CorrectionSafeScope(context, cookie, target, &scope)) {
    Report(scope == correction::ProbeStatus::ProtectedField ? correction::AutomaticStage::ProtectedField :
        correction::AutomaticStage::ScopeUnavailable); return false;
  }
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
HRESULT AutomaticCorrection::Edit(TfEditCookie cookie, ITfContext* context, bool apply, bool* applied) {
  if (applied) *applied = false;
  if (!Validate(cookie, context)) { Cancel(); return S_FALSE; }
  ReadPosition(context, cookie);
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
  // TSF requires SetText to be covered by the caller's existing composition.
  // Extend its start for this one write lock, then restore a tracked start anchor.
  // The preedit text and Rime session are never finalized or reconstructed.
  com_ptr<ITfComposition> composition;
  com_ptr<ITfRange> preeditStart;
  if (owner_->_IsComposing()) {
    composition = owner_->_pComposition;
    if (composition->GetRange(&preeditStart) != S_OK || !preeditStart ||
        preeditStart->Collapse(cookie, TF_ANCHOR_START) != S_OK ||
        preeditStart->SetGravity(cookie, TF_GRAVITY_FORWARD, TF_GRAVITY_FORWARD) != S_OK ||
        revision != revision_) return S_FALSE;
  }
  // Retire every suggestion before mutating the host. Preserve the current
  // composition and caret through tracked TSF ranges; no synthetic key events.
  const auto generation = owner_->_GetEditGeneration();
  Cancel();
  const auto writeRevision = revision_;
  auto restoreComposition = [&]() {
    if (!composition || composition->ShiftStart(cookie, preeditStart) == S_OK) return;
    // If a host refuses restoration, end without erasing any host text. Retire
    // only that same composition so later keys cannot overwrite earlier text.
    composition->EndComposition(cookie);
    if (owner_->_IsCurrentComposition(composition)) {
      owner_->_FinalizeComposition(composition);
      owner_->m_client.ClearComposition(); owner_->_cand->EndUI();
    }
  };
  if (composition && composition->ShiftStart(cookie, change) != S_OK) { restoreComposition(); return S_FALSE; }
  // A host may reenter us while changing composition properties. No write may
  // escape the document/lifecycle checked before that callback.
  if (!Focused(context) || revision_ != writeRevision || !owner_->_IsEditSessionCurrent(generation) ||
      (composition && !owner_->_IsCurrentComposition(composition))) {
    restoreComposition(); return S_FALSE;
  }
  const HRESULT hr = change->SetText(cookie, TF_ST_CORRECTION, item.replacement, item.replacementUnits);
  if (SUCCEEDED(hr) && applied) *applied = true;
  restoreComposition();
  if (FAILED(hr)) return hr;
  selection.range = restore;
  if (owner_->_IsEditSessionCurrent(generation)) context->SetSelection(cookie, 1, &selection);
  return S_OK;
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
  if (ready_ && GetTickCount64() - started_ >= kLifetime) { Cancel(); return; }
  if (!ready_ || !window_) { Hide(); return; }
  if (!positionValid_) { Report(correction::AutomaticStage::LayoutUnavailable); Hide(); return; }
  if (!Focused()) { Hide(); return; }
  if (!preferences_.timing && !owner_->_IsComposing()) {
    Report(correction::AutomaticStage::WaitingComposition); Hide(); return;
  }
  RECT anchor = caret_;
  if (owner_->_IsComposing()) {
    RECT candidate{};
    if (owner_->_cand->WindowRect(candidate)) anchor = candidate;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    else if (!testing_)
#endif
      { Report(correction::AutomaticStage::CandidateUnavailable); Hide(); return; }  // Respect apps that own the candidate UI themselves.
  }
  if (anchor.bottom <= anchor.top) { Hide(); return; }
  MONITORINFO monitor{sizeof(monitor)};
  if (!GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor)) { Hide(); return; }
  // Use the anchor monitor DPI, also when moving from a different monitor.
  const UINT dpi = owner_->_cand->WindowDpi();
  const UINT scale = dpi ? dpi : anchorDpi_;
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
  Report(correction::AutomaticStage::Shown);
  SetWindowPos(window_, HWND_TOPMOST, placed.left, placed.top, placed.right - placed.left,
      placed.bottom - placed.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
    *eaten = TRUE; return true;
  }
  if (!Request(true, context)) { Cancel(); return false; }
  downTab_ = true; *eaten = TRUE; Report(correction::AutomaticStage::TabApplied); return true;
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
  if (!reply.count) return false;
  item_ = reply.items[0];
  measuredDpi_ = 0; measuredSize_ = {};
  label_ = std::wstring(item_.original, item_.originalUnits) + L" → " + std::wstring(item_.replacement, item_.replacementUnits);
  const bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
  label_ += preferences_.automatic.tab ? (chinese ? L"\nTab 接受 · Esc 忽略" : L"\nTab accepts · Esc dismisses") :
      (chinese ? L"\n纠错建议（Tab 已关闭）" : L"\nCorrection (Tab disabled)");
  SetWindowTextW(window_, (std::wstring(correction::HintTitle(chinese)) + L": " + label_).c_str());
  InvalidateRect(window_, nullptr, TRUE);
  ready_ = true; return true;
}
bool AutomaticCorrection::StartWorker() {
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
}
void AutomaticCorrection::Poll() {
  if (polling_) return;
  polling_ = true;
  struct Guard { bool& flag; ~Guard() { flag = false; } } guard{polling_};
  const auto now = GetTickCount64();
  if (!Focused()) { ResetKeys(); Cancel(); return; }
  if (now - started_ >= kLifetime) { Report(correction::AutomaticStage::Expired); Cancel(); return; }
  if (now >= nextValidation_) {
    if (!Request(false, context_)) { Cancel(); return; }
    nextValidation_ = now + 120;
  }
  if (now >= nextSettings_) {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    if (!testing_)
#endif
      if (!SettingsCurrent()) { Cancel(); return; }
    nextSettings_ = now + 500;
  }
  if (!ready_ && !process_ && now - started_ >= preferences_.automatic.debounce) {
    if (!Request(false, context_)) { Cancel(); return; }
    if (!StartWorker()) { Report(correction::AutomaticStage::WorkerFailed); Cancel(); return; }
    Report(correction::AutomaticStage::WorkerStarted);
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
      if (!received || !reply.count || !AcceptReply(reply)) {
        Report(received && reply.magic == 0x31524357 && reply.version == 1 && !reply.count ?
            correction::AutomaticStage::NoSuggestion : correction::AutomaticStage::InvalidReply);
        SecureZeroMemory(&reply, sizeof(reply)); Cancel(); return;
      }
      Report(correction::AutomaticStage::Ready);
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
  if (testing_) return true;
#endif
  FILETIME current{};
  return SettingsTime(current) && CompareFileTime(&current, &settingsWrite_) == 0;
}
#ifdef WEASEL_CORRECTION_PROBE_BUILD
void AutomaticCorrection::ConfigureTest(const correction::Preferences& preferences, TfClientId client, const std::wstring& worker) {
  Cancel(); ResetKeys(); testing_ = true; testFocused_ = true;
  preferences_ = preferences; testClient_ = client; testWorker_ = worker;
}
bool AutomaticCorrection::InjectTest(const correction::AutomaticReply& reply) {
  if (!testing_ || !context_ || !AcceptReply(reply)) return false;
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
