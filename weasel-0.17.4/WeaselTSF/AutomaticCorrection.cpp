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
  if (testing_) return target != nullptr;
#endif
  if (!target || !owner_->_pThreadMgr || Client() == TF_CLIENTID_NULL) return false;
  com_ptr<ITfDocumentMgr> document;
  com_ptr<ITfContext> focused;
  com_ptr<ITfContextView> view;
  HWND host = nullptr;
  return owner_->_pThreadMgr->GetFocus(&document) == S_OK && document &&
      document->GetTop(&focused) == S_OK && focused == target &&
      target->GetActiveView(&view) == S_OK && view && view->GetWnd(&host) == S_OK && host &&
      GetAncestor(host, GA_ROOT) == GetAncestor(GetForegroundWindow(), GA_ROOT);
}
void AutomaticCorrection::Hide() { if (window_) ShowWindow(window_, SW_HIDE); }
void AutomaticCorrection::StopWorker() {
  // Closing this job terminates only the child we created, including on teardown.
  Close(job_); Close(process_); Close(output_);
}
void AutomaticCorrection::Cancel() {
  ++revision_; ready_ = false; testedTab_ = false; downTab_ = false;
  StopWorker(); Hide();
  context_.Release(); range_.Release();
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
  window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
      wc.lpszClassName, L"WeaseLC correction hint", WS_POPUP, 0, 0, 0, 0,
      nullptr, nullptr, g_hInst, this);
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
  if (message == WM_TIMER) {
    // Host callbacks can release the last external service reference.
    auto* owner = self->owner_; owner->AddRef();
    try { self->Poll(); } catch (...) { self->Cancel(); }
    owner->Release(); return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(window, &ps);
    RECT bounds; GetClientRect(window, &bounds);
    HBRUSH background = CreateSolidBrush(RGB(248, 250, 255));
    FillRect(dc, &bounds, background); DeleteObject(background);
    FrameRect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    const UINT dpi = correction::AutomaticWindowDpi(window);
    HFONT font = CreateFontW(-MulDiv(self->preferences_.automatic.font, dpi, 96), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto previous = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(25, 45, 75));
    InflateRect(&bounds, -MulDiv(10, dpi, 96), -MulDiv(6, dpi, 96));
    DrawTextW(dc, self->label_.c_str(), static_cast<int>(self->label_.size()), &bounds,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, previous); DeleteObject(font); EndPaint(window, &ps); return 0;
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
  if (context_ == context && range_ && committed && SettingsCurrent() &&
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
  context_ = context;
  if (previous && !accumulated.empty()) {
    range_ = previous;
    if (range_->ShiftEndToRange(cookie, committed, TF_ANCHOR_END) != S_OK) { Cancel(); return; }
    accumulated += text;
  } else {
    accumulated = text;
    if (committed->Clone(&range_) != S_OK) { Cancel(); return; }
  }
  if (!range_ ||
      range_->SetGravity(cookie, TF_GRAVITY_BACKWARD, TF_GRAVITY_BACKWARD) != S_OK) { Cancel(); return; }
  text_ = std::move(accumulated);
  started_ = GetTickCount64();
  SettingsTime(settingsWrite_);
  if (!Validate(cookie, context) || !EnsureWindow()) { Cancel(); return; }
  com_ptr<ITfRange> captured = range_;
  const auto revision = revision_;
  com_ptr<ITfContextView> view; BOOL clipped = TRUE; RECT caret{};
  if (context->GetActiveView(&view) == S_OK && view &&
      view->GetTextExt(cookie, captured, &caret, &clipped) == S_OK && !clipped && revision == revision_) caret_ = caret;
  if (revision != revision_) return;
  nextSettings_ = started_ + 500;
  SetTimer(window_, 1, 40, nullptr);
} catch (...) { Cancel(); }
bool AutomaticCorrection::Validate(TfEditCookie cookie, ITfContext* context) {
  if (!context_ || context != context_ || !range_ || GetTickCount64() - started_ >= kLifetime ||
      !Focused() || !SettingsCurrent()) return false;
  const auto revision = revision_;
  const auto text = text_;
  com_ptr<ITfRange> target = range_;
  com_ptr<ITfContext> held = context_;
  TF_STATUS status{};
  if (context->GetStatus(&status) != S_OK || (status.dwDynamicFlags & TF_SD_READONLY)) return false;
  if (!owner_->_CorrectionSafeScope(context, cookie, target) || revision != revision_ ||
      !ReadExact(target, cookie, text) || revision != revision_) return false;
  TF_SELECTION selection{}; ULONG count = 0;
  const HRESULT hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &count);
  com_ptr<ITfRange> selected; selected.Attach(selection.range);
  BOOL empty = FALSE;
  if (hr != S_OK || count != 1 || !selected || selection.style.fInterimChar ||
      selected->IsEmpty(cookie, &empty) != S_OK || !empty || revision != revision_) return false;
  if (owner_->_IsComposing()) {
    com_ptr<ITfRange> composition;
    com_ptr<ITfComposition> live = owner_->_pComposition;
    if (owner_->_pCompositionContext != context || !live || live->GetRange(&composition) != S_OK ||
        !composition || revision != revision_ || !Same(target, composition, cookie, TF_ANCHOR_END, TF_ANCHOR_START)) return false;
    LONG start = -1, end = 1;
    if (composition->CompareStart(cookie, selected, TF_ANCHOR_START, &start) != S_OK || start > 0 ||
        composition->CompareEnd(cookie, selected, TF_ANCHOR_END, &end) != S_OK || end < 0) return false;
  } else if (!Same(target, selected, cookie, TF_ANCHOR_END, TF_ANCHOR_END)) return false;
  return revision == revision_;
}
void AutomaticCorrection::Observe(ITfContext* context, TfEditCookie cookie) try {
  if (context_ && !Validate(cookie, context)) Cancel();
} catch (...) { Cancel(); }
HRESULT AutomaticCorrection::Edit(TfEditCookie cookie, ITfContext* context, bool apply, bool* applied) {
  if (applied) *applied = false;
  if (!Validate(cookie, context)) { Cancel(); return S_FALSE; }
  if (!apply) return S_OK;
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
  if (requesting_ || !context_ || context_ != context) return false;
  requesting_ = true;
  struct Guard { bool& flag; ~Guard() { flag = false; } } guard{requesting_};
  com_ptr<AutomaticEdit> session; session.Attach(new AutomaticEdit(owner_, context, apply));
  HRESULT edit = E_FAIL;
  const HRESULT hr = context->RequestEditSession(Client(), session,
      TF_ES_SYNC | (apply ? TF_ES_READWRITE : TF_ES_READ), &edit);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (testing_ && apply && !session->applied) fprintf(stderr, "Automatic write request/edit: %08lx/%08lx\n", hr, edit);
#endif
  // SYNC only: a refused lock must never leave a delayed document write queued.
  return apply ? session->applied : hr == S_OK && edit == S_OK;
}
void AutomaticCorrection::Position(const RECT& caret) { caret_ = caret; Refresh(); }
void AutomaticCorrection::Refresh() {
  if (ready_ && GetTickCount64() - started_ >= kLifetime) { Cancel(); return; }
  if (!ready_ || !window_ || !Focused() ||
      (!preferences_.timing && !owner_->_IsComposing())) { Hide(); return; }
  RECT anchor = caret_;
  if (owner_->_IsComposing()) {
    RECT candidate{};
    if (owner_->_cand->WindowRect(candidate)) anchor = candidate;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    else if (!testing_)
#endif
      { Hide(); return; }  // Respect apps that own the candidate UI themselves.
  }
  if (anchor.bottom <= anchor.top) { Hide(); return; }
  MONITORINFO monitor{sizeof(monitor)};
  if (!GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor)) { Hide(); return; }
  // Use the anchor monitor DPI, also when moving from a different monitor.
  const UINT dpi = owner_->_cand->WindowDpi();
  const UINT scale = dpi ? dpi : correction::AutomaticWindowDpi(window_);
  const auto& options = preferences_.automatic;
  SIZE size{MulDiv(options.width, scale, 96), MulDiv(options.font * 4 + 20, scale, 96)};
  const RECT placed = correction::PlaceAutomaticHint(anchor, monitor.rcWork, size, options.side, MulDiv(options.gap, scale, 96));
  InvalidateRect(window_, nullptr, TRUE);
  SetWindowPos(window_, HWND_TOPMOST, placed.left, placed.top, placed.right - placed.left,
      placed.bottom - placed.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
bool AutomaticCorrection::Key(ITfContext* context, WPARAM key, LPARAM flags, bool test, bool up, BOOL* eaten) try {
  if (!eaten) return false;
  if (key != VK_TAB) {
    testedTab_ = false;
    if (!up && (key == VK_ESCAPE || key == VK_LEFT || key == VK_RIGHT || key == VK_HOME ||
        key == VK_END || key == VK_DELETE || key == VK_BACK || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU)) Cancel();
    return false;
  }
  if (up) {
    if (!downTab_) { testedTab_ = false; return false; }
    *eaten = TRUE;
    if (!test) { downTab_ = false; testedTab_ = false; }
    return true;
  }
  if (downTab_) { *eaten = TRUE; return true; }  // Don't accept twice on autorepeat.
  const bool modifiers = (GetKeyState(VK_SHIFT) | GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU) | GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000;
  if (modifiers || (flags & (1LL << 30)) || !ready_ || !preferences_.automatic.tab ||
      !window_ || !IsWindowVisible(window_) || !Focused()) { testedTab_ = false; return false; }
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  if (!testing_)
#endif
    if (owner_->_status.ascii_mode || owner_->_status.disabled || owner_->_IsKeyboardDisabled() ||
        (owner_->_isToOpenClose && !owner_->_IsKeyboardOpen())) { Cancel(); return false; }
  if (test) {
    if (!Request(false, context)) { Cancel(); return false; }
    testedTab_ = true; *eaten = TRUE; return true;
  }
  testedTab_ = false;
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
  if (!reply.count) return false;
  item_ = reply.items[0];
  label_ = std::wstring(item_.original, item_.originalUnits) + L" → " + std::wstring(item_.replacement, item_.replacementUnits);
  const bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
  label_ += preferences_.automatic.tab ? (chinese ? L"\nTab 接受 · Esc 忽略" : L"\nTab accepts · Esc dismisses") :
      (chinese ? L"\n纠错建议（Tab 已关闭）" : L"\nCorrection (Tab disabled)");
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
    executable = module;
    executable.resize(executable.find_last_of(L"\\/") + 1);
    executable += L"WeaselDeployer.exe";
  }
  if (executable.empty() || GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
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
          CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
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
  if (!Focused() || now - started_ >= kLifetime) { Cancel(); return; }
  if (now >= nextSettings_) {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    if (!testing_)
#endif
      if (!SettingsCurrent()) { Cancel(); return; }
    nextSettings_ = now + 500;
  }
  if (!ready_ && !process_ && now - started_ >= preferences_.automatic.debounce) {
    if (!Request(false, context_) || !StartWorker()) { Cancel(); return; }
  }
  if (process_) {
    DWORD available = 0;
    if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr)) { Cancel(); return; }
    if (available < sizeof(correction::AutomaticReply) && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
      // The process may have written/exited between the first peek and wait.
      if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr) || available < sizeof(correction::AutomaticReply)) { Cancel(); return; }
    }
    if (available > sizeof(correction::AutomaticReply)) { Cancel(); return; }
    if (available == sizeof(correction::AutomaticReply)) {
      correction::AutomaticReply reply; DWORD read = 0;
      const bool received = ReadFile(output_, &reply, sizeof(reply), &read, nullptr) && read == sizeof(reply);
      StopWorker();
      if (!received || !AcceptReply(reply) || !Request(false, context_)) {
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
  Cancel(); testing_ = true; preferences_ = preferences; testClient_ = client; testWorker_ = worker;
}
bool AutomaticCorrection::InjectTest(const correction::AutomaticReply& reply) {
  if (!testing_ || !context_ || !AcceptReply(reply)) return false;
  Refresh(); return true;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticConfigure(
    ITfTextInputProcessorEx* service, const correction::Preferences* preferences,
    TfClientId client, const WCHAR* worker) {
  if (!service || !preferences || client == TF_CLIENTID_NULL) return E_INVALIDARG;
  static_cast<WeaselTSF*>(service)->Automatic().ConfigureTest(*preferences, client, worker ? worker : L"");
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
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticKey(
    ITfTextInputProcessorEx* service, ITfContext* context, WPARAM key, LPARAM flags, DWORD operation, BOOL* eaten) {
  if (!service || !context || !eaten) return E_INVALIDARG;
  *eaten = FALSE;
  return static_cast<WeaselTSF*>(service)->Automatic().Key(context, key, flags,
      operation & 1, operation & 2, eaten) ? S_OK : S_FALSE;
}
#endif
