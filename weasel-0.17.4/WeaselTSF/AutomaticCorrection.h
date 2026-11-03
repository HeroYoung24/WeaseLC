#pragma once
#include <CorrectionAutomatic.h>
#include <CorrectionDiagnostics.h>
#include <CorrectionBrokerClient.h>
#include <CorrectionHintTheme.h>
#include <CorrectionBehavior.h>
#include <CorrectionPendingHints.h>
#include <CorrectionHintKeyCycle.h>
#include <CorrectionLatestCheck.h>
#include "../WeaselDeployer/CorrectionPreferences.h"

class WeaselTSF;
// Owned by the apartment-bound service. No thread, COM object or host HWND is
// passed to the model process. All TSF access happens under a granted cookie.
class AutomaticCorrection {
 public:
  explicit AutomaticCorrection(WeaselTSF* owner) : owner_(owner) {}
  ~AutomaticCorrection();
  void Cancel();
  void ResetKeys() { downTab_ = false; flowTab_.ResetAfterFocusLoss(); flowEsc_.ResetAfterFocusLoss(); flowTabStroke_ = flowEscStroke_ = 0; }
  void Capture(ITfContext*, TfEditCookie, ITfRange*, const std::wstring&);
  void Observe(ITfContext*, TfEditCookie);
  void RecordInput(ITfContext*, TfEditCookie);
  void LayoutChanged(ITfContext*, bool destroyed);
  void Position(const RECT& caret);
  void Refresh();
  bool Key(ITfContext*, WPARAM, LPARAM, bool test, bool up, BOOL* eaten);
  bool IntegratedKey(WPARAM, LPARAM, BOOL* eaten);
  struct WriteVerification {
    com_ptr<ITfRange> range;
    std::wstring text;
    ULONGLONG generation = 0;
    // Apartment-local transaction. Never serialized or queued asynchronously.
    com_ptr<ITfRange> source, selection, preedit;
    std::wstring original, sourceText, preeditText;
    ULONGLONG revision = 0;
    size_t offset = 0;
    bool detached = false, acknowledged = false, written = false, resumed = false;
  };
  HRESULT Edit(TfEditCookie, ITfContext*, bool apply, bool* applied, WriteVerification*);
  HRESULT VerifyWrite(TfEditCookie, ITfContext*, const WriteVerification&);
  HRESULT ContinueWrite(TfEditCookie, ITfContext*, WriteVerification&, bool resume);
  bool Focused(ITfContext* target = nullptr) const;
  void Poll();
  bool InputOnlyHost(bool forcePolicy = false) const {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    if (testing_ && testInputOnlyHost_) return true;
#endif
    return correction::InputOnlyHost(forcePolicy);
  }
#ifdef WEASELC_LOCAL_TEST_BUILD
  bool ScopeTraceEnabled() const { return !InputOnlyHost() && preferences_.mode==correction::Mode::Local && preferences_.automatic.enabled; }
  void ScheduleScopeReadTrace(ITfContext* context) { scopeReadTraceContext_=context; }
  bool ConsumeScopeReadTrace(ITfContext* context);
#endif
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  void ConfigureTest(const correction::Preferences&, TfClientId, const std::wstring& worker);
  bool InjectTest(const correction::AutomaticReply&);
  HWND TestWindow() const { return window_; }
  bool TestReady() const { return ready_; }
  void ExpireTest() { started_ = GetTickCount64() - Lifetime(); }
  void FocusTest(bool focused) { testFocused_ = focused; }
  DWORD ProcessTest() const { return process_ ? GetProcessId(process_) : 0; }
  correction::AutomaticDiagnostic DiagnosticTest() const { return diagnostic_; }
  DWORD CachedUnitsTest() const { return static_cast<DWORD>(text_.size()); }
  void PendingScopeTest(bool enabled) { testPendingScope_ = enabled; }
  void InputOnlyHostTest(bool enabled) { testInputOnlyHost_ = enabled; }
  void BehaviorTest(DWORD* counts, DWORD age) {
    std::copy_n(testBehavior_, 3, counts);
    if (age && exposure_.shown) exposure_.shown = GetTickCount64() - age;
  }
  void ExpirePendingTest() { if (scopeWaitUntil_) scopeWaitUntil_ = 1; }
  void RevalidateTest() { nextValidation_ = 0; }
  ULONGLONG ScopeDeadlineTest() const { return scopeWaitUntil_; }
  DWORD WordEvidenceTest() const { return testing_ ? testWordEvidence_ : 0; }
  bool WordEvidenceTest(DWORD value) { if (!testing_ || value > 1023) return false; testWordEvidence_=value; return true; }
  bool CandidateTest(DWORD mode, RECT& position);
#endif
 private:
  static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
  bool EnsureWindow();
  bool Validate(TfEditCookie, ITfContext*);
  bool Request(bool apply, ITfContext*);
  HRESULT WriteWordAndTail(TfEditCookie, ITfContext*, WriteVerification&, bool* applied = nullptr);
  bool StartWorker();
  void StopWorker();
  void DiscardSuggestion(bool completed);
  void InferenceFailed(correction::AutomaticStage);
  bool WaitForScope(const correction::ScopeDiagnostic&, ITfContext*, TfEditCookie);
  bool CanWaitForScope(const correction::ScopeDiagnostic&) const;
  bool TrackIdleSelection(ITfContext*, TfEditCookie);
  bool Arm();
  void VerifiedCapture();
  bool AcceptReply(const correction::AutomaticReply&);
  void Hide();
  void FlowReset();
  void FlowCapture();
  bool FlowReply(const correction::AutomaticReply&);
  void FlowPresent();
  void FlowRevalidate();
  void FlowComplete(bool success);
  bool FlowRestore(ITfContext*, const WriteVerification&);
  bool FlowKey(ITfContext*, WPARAM, LPARAM, bool, bool, BOOL*);
  void RecordBehavior(const correction::BehaviorExposure&, correction::BehaviorOutcome) noexcept;
  TfClientId Client() const;
  bool SettingsCurrent() const;
  bool LoadBrokerPreferences();
  ULONGLONG Lifetime() const;
  std::wstring BrokerServer() const;
  void ReadPosition(ITfContext*, TfEditCookie);
  void Report(correction::AutomaticStage, correction::FocusResult = {}) const;
  mutable correction::AutomaticDiagnostic diagnostic_;

  WeaselTSF* owner_;  // non-owning; the service destroys this before releasing DLL
  com_ptr<ITfContext> context_;
  com_ptr<ITfRange> range_;
  com_ptr<ITfRange> expectedSelection_;
  std::wstring expectedPreedit_;
  bool expectedComposing_ = false, positionValid_ = false;
  std::wstring text_, label_;
  correction::Preferences preferences_;
  correction::AutomaticReply::Item item_{};
  ULONGLONG revision_ = 0, started_ = 0, nextSettings_ = 0, nextValidation_ = 0;
  ULONGLONG scopeWaitUntil_ = 0;
  DWORD lastAddedUnits_ = 0;
  bool scopePending_ = false;
  ULONGLONG brokerRevision_ = 0, brokerRequestId_ = 0;
  correction::BrokerCall brokerCall_;
  HWND window_ = nullptr;
  RECT caret_{};
  UINT anchorDpi_ = 96;
  UINT measuredDpi_ = 0;
  SIZE measuredSize_{};
  correction::HintPalette paintedPalette_{};
  HANDLE process_ = nullptr, job_ = nullptr, output_ = nullptr;
  bool ready_ = false, checked_ = false, downTab_ = false;
  bool polling_ = false, requesting_ = false;
  correction::BehaviorExposure exposure_, pendingEscape_;
  ULONGLONG pendingEscapeUntil_ = 0;
  com_ptr<ITfContext> escapeContext_;
  struct FlowRecord { correction::input_flow::HintIdentity identity; correction::AutomaticReply::Item item; };
  std::array<FlowRecord,64> flowRecords_{};
  size_t flowRecordCount_ = 0;
  size_t flowShownCount_ = 0;
  correction::input_flow::PendingHints flowQueue_;
  correction::input_flow::LatestCheck flowScheduler_;
  correction::input_flow::HintKeyCycle flowTab_, flowEsc_;
  correction::input_flow::HintIdentity flowActive_;
  std::optional<correction::input_flow::CheckWork> flowWork_;
  ULONGLONG flowEpoch_ = 1, flowVersion_ = 0, flowSerial_ = 0, flowKeySerial_ = 0;
  ULONGLONG flowTabStroke_ = 0, flowEscStroke_ = 0;
  bool flowInitialized_ = false, flowWriting_ = false, flowEscBlocked_ = false, flowIntegrated_ = false, flowEngaged_ = false;
  std::wstring flowLastView_;
#ifdef WEASELC_LOCAL_TEST_BUILD
  com_ptr<ITfContext> scopeReadTraceContext_;
#endif
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  bool testing_ = false;
  bool testingBroker_ = false;
  bool testFocused_ = true;
  bool strictCandidateTest_ = false;
  bool testPendingScope_ = false;
  bool testInputOnlyHost_ = false;
  DWORD testBehavior_[3]{};
  DWORD testWordEvidence_ = 0;
  TfClientId testClient_ = TF_CLIENTID_NULL;
  std::wstring testWorker_;
#endif
};
