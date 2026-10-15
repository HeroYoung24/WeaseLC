#pragma once
#include <CorrectionAutomatic.h>
#include "../WeaselDeployer/CorrectionPreferences.h"

class WeaselTSF;
// Owned by the apartment-bound service. No thread, COM object or host HWND is
// passed to the model process. All TSF access happens under a granted cookie.
class AutomaticCorrection {
 public:
  explicit AutomaticCorrection(WeaselTSF* owner) : owner_(owner) {}
  ~AutomaticCorrection();
  void Cancel();
  void Capture(ITfContext*, TfEditCookie, ITfRange*, const std::wstring&);
  void Observe(ITfContext*, TfEditCookie);
  void Position(const RECT& caret);
  void Refresh();
  bool Key(ITfContext*, WPARAM, LPARAM, bool test, bool up, BOOL* eaten);
  HRESULT Edit(TfEditCookie, ITfContext*, bool apply, bool* applied);
  bool Focused(ITfContext* target = nullptr) const;
  void Poll();
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  void ConfigureTest(const correction::Preferences&, TfClientId, const std::wstring& worker);
  bool InjectTest(const correction::AutomaticReply&);
  HWND TestWindow() const { return window_; }
  bool TestReady() const { return ready_; }
  void ExpireTest() { started_ = GetTickCount64() - 10000; }
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

  WeaselTSF* owner_;  // non-owning; the service destroys this before releasing DLL
  com_ptr<ITfContext> context_;
  com_ptr<ITfRange> range_;
  std::wstring text_, label_;
  correction::Preferences preferences_;
  correction::AutomaticReply::Item item_{};
  ULONGLONG revision_ = 0, started_ = 0, nextSettings_ = 0;
  FILETIME settingsWrite_{};
  HWND window_ = nullptr;
  RECT caret_{};
  HANDLE process_ = nullptr, job_ = nullptr, output_ = nullptr;
  bool ready_ = false, testedTab_ = false, downTab_ = false;
  bool polling_ = false, requesting_ = false;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  bool testing_ = false;
  TfClientId testClient_ = TF_CLIENTID_NULL;
  std::wstring testWorker_;
#endif
};
