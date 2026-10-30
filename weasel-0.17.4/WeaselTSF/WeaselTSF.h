#pragma once
#include <CorrectionFocus.h>
#include <KeyDispatchDiagnostic.h>

#include "Globals.h"
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include "AutomaticCorrection.h"

class CCandidateList;
class CLangBarItemButton;
class CCompartmentEventSink;
struct ServerResponse;

class WeaselTSF : public ITfTextInputProcessorEx,
                  public ITfThreadMgrEventSink,
                  public ITfTextEditSink,
                  public ITfTextLayoutSink,
                  public ITfKeyEventSink,
                  public ITfCompositionSink,
                  public ITfThreadFocusSink,
                  public ITfActiveLanguageProfileNotifySink,
                  public ITfEditSession,
                  public ITfDisplayAttributeProvider {
 public:
  WeaselTSF();
  ~WeaselTSF();
  AutomaticCorrection& Automatic() { return *_automatic; }
  bool _CorrectionSafeScope(ITfContext*, TfEditCookie, ITfRange*, correction::ProbeStatus* reason = nullptr, correction::ScopeDiagnostic* details = nullptr);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  HRESULT ProbeGetCandidate(IUnknown** candidate);
  HRESULT ProbeExerciseLanguageBar(ITfThreadMgr* manager);
#endif

  /* IUnknown */
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  /* ITfTextInputProcessor */
  STDMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId);
  STDMETHODIMP Deactivate();

  /* ITfTextInputProcessorEx */
  STDMETHODIMP ActivateEx(ITfThreadMgr* pThreadMgr,
                          TfClientId tfClientId,
                          DWORD dwFlags);

  /* ITfThreadMgrEventSink */
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pDocMgr);
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr);
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* pDocMgrFocus,
                          ITfDocumentMgr* pDocMgrPrevFocus);
  STDMETHODIMP OnPushContext(ITfContext* pContext);
  STDMETHODIMP OnPopContext(ITfContext* pContext);

  /* ITfTextEditSink */
  STDMETHODIMP OnEndEdit(ITfContext* pic,
                         TfEditCookie ecReadOnly,
                         ITfEditRecord* pEditRecord);

  /* ITfTextLayoutSink */
  STDMETHODIMP OnLayoutChange(ITfContext* pContext,
                              TfLayoutCode lcode,
                              ITfContextView* pContextView);

  /* ITfKeyEventSink */
  STDMETHODIMP OnSetFocus(BOOL fForeground);
  STDMETHODIMP OnTestKeyDown(ITfContext* pContext,
                             WPARAM wParam,
                             LPARAM lParam,
                             BOOL* pfEaten);
  STDMETHODIMP OnKeyDown(ITfContext* pContext,
                         WPARAM wParam,
                         LPARAM lParam,
                         BOOL* pfEaten);
  STDMETHODIMP OnTestKeyUp(ITfContext* pContext,
                           WPARAM wParam,
                           LPARAM lParam,
                           BOOL* pfEaten);
  STDMETHODIMP OnKeyUp(ITfContext* pContext,
                       WPARAM wParam,
                       LPARAM lParam,
                       BOOL* pfEaten);
  STDMETHODIMP OnPreservedKey(ITfContext* pContext,
                              REFGUID rguid,
                              BOOL* pfEaten);

  // ITfThreadFocusSink
  STDMETHODIMP OnSetThreadFocus();
  STDMETHODIMP OnKillThreadFocus();

  /* ITfCompositionSink */
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                       ITfComposition* pComposition);

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

  /* ITfActiveLanguageProfileNotifySink */
  STDMETHODIMP OnActivated(REFCLSID clsid,
                           REFGUID guidProfile,
                           BOOL isActivated);

  // ITfDisplayAttributeProvider
  STDMETHODIMP EnumDisplayAttributeInfo(
      __RPC__deref_out_opt IEnumTfDisplayAttributeInfo** ppEnum);
  STDMETHODIMP GetDisplayAttributeInfo(
      __RPC__in REFGUID guidInfo,
      __RPC__deref_out_opt ITfDisplayAttributeInfo** ppInfo);

  ///* ITfCompartmentEventSink */
  // STDMETHODIMP OnChange(_In_ REFGUID guid);

  /* Compartments */
  BOOL _IsKeyboardDisabled();
  BOOL _IsKeyboardOpen();
  HRESULT _SetKeyboardOpen(BOOL fOpen);
  HRESULT _GetCompartmentDWORD(DWORD& value, const GUID guid);
  HRESULT _SetCompartmentDWORD(const DWORD& value, const GUID guid);

  /* Composition */
  void _StartComposition(com_ptr<ITfContext> pContext,
                         BOOL fCUASWorkaroundEnabled);
  void _EndComposition(com_ptr<ITfContext> pContext, BOOL clear);
  BOOL _ShowInlinePreedit(com_ptr<ITfContext> pContext,
                          const std::shared_ptr<weasel::Context> context);
  void _UpdateComposition(com_ptr<ITfContext> pContext);
  void _InvalidateEditSessions();
  void _BindEditContext(ITfContext* context);
  ULONGLONG _GetEditGeneration() const { return _editGeneration; }
  bool _IsEditSessionCurrent(ULONGLONG generation) const {
    return generation == _editGeneration;
  }
  HRESULT _ApplyServerResponse(TfEditCookie cookie, ITfContext* context,
                              const ServerResponse& response);
  HRESULT _ApplyCompositionResponse(TfEditCookie cookie, ITfContext* context,
                                   const ServerResponse& response);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  void ProbeCaptureCommit(ITfContext*, TfEditCookie, const std::wstring&);
  void ProbeCancelCapture();
  HRESULT ProbeCompositionRange(ITfRange** range) {
    if (!range) return E_POINTER;
    *range = nullptr;
    return _pComposition ? _pComposition->GetRange(range) : S_FALSE;
  }
  void ProbeSetEditClient(TfClientId client) {
    if (!_pThreadMgr) _tfClientId = client;
  }
  void ProbeSetKeySink(void(WINAPI* sink)(WPARAM, LPARAM, BOOL*, void*), void* state) {
    _probeKeySink = sink; _probeKeySinkContext = state;
  }
#endif
  BOOL _IsComposing();
  void _SetComposition(com_ptr<ITfComposition> pComposition,
                       ITfContext* context = nullptr);
  void _SetCompositionPosition(const RECT& rc);
  BOOL _UpdateCompositionWindow(com_ptr<ITfContext> pContext);
  void _FinalizeComposition(ITfComposition* expected = nullptr);
  bool _IsCurrentComposition(ITfComposition* composition) const {
    return _pComposition == composition;
  }
  void _AbortComposition(bool clear = true);
  void _RetireComposition(bool hostTerminated = false);
  HRESULT _EndTrackedComposition(TfEditCookie cookie, ITfComposition* composition);

  /* Language bar */
  HWND _GetFocusedContextWindow();
  void _HandleLangBarMenuSelect(UINT wID);

  /* IPC */
  bool _EnsureServerConnected();

  /* UI */
  void _UpdateUI(const weasel::Context& ctx, const weasel::Status& status);
  void _StartUI();
  void _EndUI();
  void _ShowUI();
  void _HideUI();
  com_ptr<ITfContext> _GetUIContextDocument();

  /* Display Attribute */
  void _ClearCompositionDisplayAttributes(TfEditCookie ec,
                                          _In_ ITfContext* pContext,
                                          ITfComposition* composition);
  BOOL _SetCompositionDisplayAttributes(TfEditCookie ec,
                                        _In_ ITfContext* pContext,
                                        ITfRange* pRangeComposition);
  BOOL _InitDisplayAttributeGuidAtom();

  com_ptr<ITfThreadMgr> _GetThreadMgr() { return _pThreadMgr; }
  void HandleUICallback(size_t* const sel,
                        size_t* const hov,
                        bool* const next,
                        bool* const scroll_next);

 private:
  friend class AutomaticCorrection;
  std::unique_ptr<AutomaticCorrection> _automatic;
  /* ui callback functions private */
  void _SelectCandidateOnCurrentPage(const size_t index);
  void _HandleMouseHoverEvent(const size_t index);
  void _HandleMousePageEvent(bool* const nextPage, bool* const scrollNextPage);
  /* TSF Related */
  BOOL _InitThreadMgrEventSink();
  void _UninitThreadMgrEventSink();
  // ITfThreadFocusSink
  BOOL _InitThreadFocusSink();
  void _UninitThreadFocusSink();
  DWORD _dwThreadFocusSinkCookie;

  BOOL _InitTextEditSink(com_ptr<ITfDocumentMgr> pDocMgr);

  BOOL _InitKeyEventSink();
  void _UninitKeyEventSink();
  void _ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten);

  BOOL _InitPreservedKey();
  void _UninitPreservedKey();

  BOOL _InitLanguageBar();
  void _UninitLanguageBar();
  void _UpdateLanguageBar(weasel::Status stat);
  void _ShowLanguageBar(BOOL show);
  void _EnableLanguageBar(BOOL enable);

  BOOL _InsertText(com_ptr<ITfContext> pContext, const std::wstring& ext);

  void _DeleteCandidateList();

  BOOL _InitCompartment();
  void _UninitCompartment();
  HRESULT _HandleCompartment(REFGUID guidCompartment);

  void _Reconnect();
  std::wstring _GetRootDir();

  bool isImmersive() const {
    return (_activateFlags & TF_TMF_IMMERSIVEMODE) != 0;
  }

  com_ptr<ITfThreadMgr> _pThreadMgr;
  TfClientId _tfClientId = TF_CLIENTID_NULL;
  bool _keyEventSinkAdvised = false;
  DWORD _dwThreadMgrEventSinkCookie;

  com_ptr<ITfContext> _pTextEditSinkContext;
  com_ptr<ITfContext> _correctionProbeContext;
  bool _correctionProbePending = false;
  DWORD _dwTextEditSinkCookie, _dwTextLayoutSinkCookie;
  BYTE _lpbKeyState[256];
  struct PendingKey {
    com_ptr<ITfContext> context;
    WPARAM key = 0;
    LPARAM flags = 0;
    LONG time = 0;
    BOOL handled = FALSE;
    bool Matches(ITfContext* target, WPARAM value, LPARAM message, LONG messageTime, bool query) const {
      return context && correction::SameIdentity(context, target) && key == value && flags == message &&
          (!query || time == messageTime);
    }
    void Set(ITfContext* target, WPARAM value, LPARAM message, LONG messageTime, BOOL result) {
      context = target; key = value; flags = message; time = messageTime; handled = result;
    }
    void Reset() { context = nullptr; }
  };
  PendingKey _pendingKeyDown, _pendingKeyUp;
  LONG _KeyMessageTime() const {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    if (_probeMessageTimeEnabled) return _probeMessageTime;
#endif
    return GetMessageTime();
  }
#ifdef WEASEL_CORRECTION_PROBE_BUILD
 public:
  void ProbeMessageTime(LONG value) { _probeMessageTime = value; _probeMessageTimeEnabled = true; }
 private:
  LONG _probeMessageTime = 0;
  bool _probeMessageTimeEnabled = false;
#endif
  HRESULT _HandleKey(ITfContext*, WPARAM, LPARAM, BOOL*, bool test, bool up);
#if defined(WEASELC_LOCAL_TEST_BUILD) || defined(WEASEL_CORRECTION_PROBE_BUILD)
 public:
  const correction::KeyDispatchDiagnostic& ProbeKeyDispatch(unsigned channel) const { return _keyDispatch[channel]; }
  bool TraceKeyDispatch() const { return _traceKeyDispatch; }
  void CompleteKeyDispatch(unsigned channel, BOOL eaten, ULONGLONG generation) noexcept;
  void BeginIntegratedDispatch() { if (_traceKeyDispatch) ++_keyDispatch[2].callbacks; }
 private:
  correction::KeyDispatchDiagnostic _keyDispatch[3];
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  bool _traceKeyDispatch = true;
#else
  // Opt in only for a deliberately launched synthetic-input test process.
  // Ordinary test hosts incur no extra diagnostic IPC per key.
  bool _traceKeyDispatch = []() {
    WCHAR value[2]{};
    return GetEnvironmentVariableW(L"WEASELC_TRACE_KEYS", value, _countof(value)) == 1 && value[0] == L'1';
  }();
#endif
#endif
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  using ProbeKeySink = void(WINAPI*)(WPARAM, LPARAM, BOOL*, void*);
  ProbeKeySink _probeKeySink = nullptr;
  void* _probeKeySinkContext = nullptr;
#endif

  com_ptr<ITfContext> _pEditSessionContext;
  ULONGLONG _editGeneration = 0;
  std::shared_ptr<const weasel::UIStyle> _lastCapturedStyle;
  std::wstring _editSessionText;

  com_ptr<CCompartmentEventSink> _pKeyboardCompartmentSink;
  com_ptr<CCompartmentEventSink> _pConvertionCompartmentSink;

  com_ptr<ITfComposition> _pComposition;
  com_ptr<ITfContext> _pCompositionContext;
  ITfComposition* _endingComposition = nullptr;  // borrowed during synchronous End

  com_ptr<CLangBarItemButton> _pLangBarButton;
  com_ptr<ITfLangBarItemMgr> _pLangBarItemMgr;

  com_ptr<CCandidateList> _cand;

  LONG _cRef;  // COM ref count

  /* CUAS Candidate Window Position Workaround */
  BOOL _fCUASWorkaroundTested, _fCUASWorkaroundEnabled;

  /* Weasel Related */
  weasel::Client m_client;
  DWORD _activateFlags = 0;

  /* IME status */
  weasel::Status _status;

  // guidatom for the display attibute.
  TfGuidAtom _gaDisplayAttributeInput;
  BOOL _async_edit = false;
  BOOL _committed = false;
  BOOL _isToOpenClose = false;
};
