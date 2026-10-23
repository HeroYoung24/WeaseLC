#pragma once
#include <CorrectionAutomatic.h>
#include <CorrectionDiagnostics.h>
#include <CorrectionBrokerClient.h>
#include "../WeaselDeployer/CorrectionPreferences.h"

class WeaselTSF;
// Owned by the apartment-bound service. No thread, COM object or host HWND is
// passed to the model process. All TSF access happens under a granted cookie.
class AutomaticCorrection {
 public:
  explicit AutomaticCorrection(WeaselTSF* owner) : owner_(owner) {}
  ~AutomaticCorrection();
  void Cancel();
  void ResetKeys() { downTab_ = false; }
  void Capture(ITfContext*, TfEditCookie, ITfRange*, const std::wstring&);
  void Observe(ITfContext*, TfEditCookie);
  void RecordInput(ITfContext*, TfEditCookie);
  void LayoutChanged(ITfContext*, bool destroyed);
  void Position(const RECT& caret);
  void Refresh();
  bool Key(ITfContext*, WPARAM, LPARAM, bool test, bool up, BOOL* eaten);
  struct WriteVerification {
    com_ptr<ITfRange> range;
    std::wstring text;
    ULONGLONG generation = 0;
  };
  HRESULT Edit(TfEditCookie, ITfContext*, bool apply, bool* applied, WriteVerification*);
  HRESULT VerifyWrite(TfEditCookie, ITfContext*, const WriteVerification&);
  bool Focused(ITfContext* target = nullptr) const;
  void Poll();
#ifdef WEASELC_LOCAL_TEST_BUILD
  bool ScopeTraceEnabled() const { return preferences_.mode==correction::Mode::Local && preferences_.automatic.enabled; }
  void ScheduleScopeReadTrace(ITfContext* context) { scopeReadTraceContext_=context; }
  bool ConsumeScopeReadTrace(ITfContext* context);
#endif
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  void ConfigureTest(const correction::Preferences&, TfClientId, const std::wstring& worker);
  bool InjectTest(const correction::AutomaticReply&);
  HWND TestWindow() const { return window_; }
  bool TestReady() const { return ready_; }
  void ExpireTest() { started_ = GetTickCount64() - 10000; }
  void FocusTest(bool focused) { testFocused_ = focused; }
  DWORD ProcessTest() const { return process_ ? GetProcessId(process_) : 0; }
  correction::AutomaticDiagnostic DiagnosticTest() const { return diagnostic_; }
  bool CandidateTest(DWORD mode, RECT& position);
#endif
 private:
  static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
  bool EnsureWindow();
  bool Validate(TfEditCookie, ITfContext*);
  bool Request(bool apply, ITfContext*);
  bool StartWorker();
  void StopWorker();
  bool AcceptReply(const correction::AutomaticReply&);
  void Hide();
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
  ULONGLONG brokerRevision_ = 0, brokerRequestId_ = 0;
  correction::BrokerCall brokerCall_;
  HWND window_ = nullptr;
  RECT caret_{};
  UINT anchorDpi_ = 96;
  UINT measuredDpi_ = 0;
  SIZE measuredSize_{};
  HANDLE process_ = nullptr, job_ = nullptr, output_ = nullptr;
  bool ready_ = false, checked_ = false, downTab_ = false;
  bool polling_ = false, requesting_ = false;
#ifdef WEASELC_LOCAL_TEST_BUILD
  com_ptr<ITfContext> scopeReadTraceContext_;
#endif
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  bool testing_ = false;
  bool testingBroker_ = false;
  bool testFocused_ = true;
  bool strictCandidateTest_ = false;
  TfClientId testClient_ = TF_CLIENTID_NULL;
  std::wstring testWorker_;
#endif
};
