#include "stdafx.h"

#include <WeaselIPCData.h>
#include <thread>
#include <shellapi.h>
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "LanguageBar.h"
#include "Compartment.h"
#include "ResponseParser.h"

#ifdef WEASEL_CORRECTION_PROBE_BUILD
static volatile LONG g_probeActivationStage = 0;
static volatile LONG g_probeActivationResult = E_PENDING;
static volatile LONG g_probeCreated = 0;
static volatile LONG g_probeDestroyed = 0;
static volatile LONG g_probeActivated = 0;
static volatile LONG g_probeDeactivated = 0;

// Diagnostic callers supply an instance created by this DLL's class factory.
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeGetCandidate(
    ITfTextInputProcessorEx* service, IUnknown** candidate) {
  if (!service || !candidate) return E_POINTER;
  return static_cast<WeaselTSF*>(service)->ProbeGetCandidate(candidate);
}

HRESULT WeaselTSF::ProbeGetCandidate(IUnknown** candidate) {
  return _cand->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(candidate));
}

extern "C" __declspec(dllexport) void WINAPI WeaselProbeGetLifecycleCounts(
    LONG* created, LONG* destroyed, LONG* activated, LONG* deactivated) {
  if (created) *created = InterlockedCompareExchange(&g_probeCreated, 0, 0);
  if (destroyed) *destroyed = InterlockedCompareExchange(&g_probeDestroyed, 0, 0);
  if (activated) *activated = InterlockedCompareExchange(&g_probeActivated, 0, 0);
  if (deactivated) *deactivated = InterlockedCompareExchange(&g_probeDeactivated, 0, 0);
}

extern "C" __declspec(dllexport) LONG WINAPI WeaselProbeGetActivationStage() {
  return InterlockedCompareExchange(&g_probeActivationStage, 0, 0);
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeGetActivationResult() {
  return InterlockedCompareExchange(&g_probeActivationResult, 0, 0);
}

void ProbeActivationResult(HRESULT result) {
  InterlockedExchange(&g_probeActivationResult, result);
}

static void ProbeActivationStage(LONG stage) {
  InterlockedExchange(&g_probeActivationStage, stage);
  ProbeActivationResult(E_PENDING);
}
#else
static void ProbeActivationStage(LONG) {}
#endif

static void error_message(const WCHAR* msg) {
  static DWORD next_tick = 0;
  DWORD now = GetTickCount();
  if (now > next_tick) {
    next_tick = now + 10000;  // (ms)
    MessageBox(NULL, msg, get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
  }
}

WeaselTSF::WeaselTSF() {
  _cRef = 1;

  _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;

  _dwTextEditSinkCookie = TF_INVALID_COOKIE;
  _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
  _fTestKeyDownPending = FALSE;
  _fTestKeyUpPending = FALSE;

  _fCUASWorkaroundTested = _fCUASWorkaroundEnabled = FALSE;

  _cand.Attach(new CCandidateList(this));
  _automatic = std::make_unique<AutomaticCorrection>(this);

  DllAddRef();
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  // Count completed constructions; a candidate allocation failure can throw.
  InterlockedIncrement(&g_probeCreated);
#endif
}

WeaselTSF::~WeaselTSF() {
  _automatic.reset();
  _cand->DetachService();
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  InterlockedIncrement(&g_probeDestroyed);
#endif
  DllRelease();
}

STDAPI WeaselTSF::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == NULL)
    return E_POINTER;

  *ppvObject = NULL;

  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfTextInputProcessor))
    *ppvObject = (ITfTextInputProcessor*)this;
  else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    *ppvObject = (ITfTextInputProcessorEx*)this;
  else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    *ppvObject = (ITfThreadMgrEventSink*)this;
  else if (IsEqualIID(riid, IID_ITfTextEditSink))
    *ppvObject = (ITfTextEditSink*)this;
  else if (IsEqualIID(riid, IID_ITfTextLayoutSink))
    *ppvObject = (ITfTextLayoutSink*)this;
  else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    *ppvObject = (ITfKeyEventSink*)this;
  else if (IsEqualIID(riid, IID_ITfCompositionSink))
    *ppvObject = (ITfCompositionSink*)this;
  else if (IsEqualIID(riid, IID_ITfEditSession))
    *ppvObject = (ITfEditSession*)this;
  else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    *ppvObject = (ITfThreadFocusSink*)this;
  else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    *ppvObject = (ITfDisplayAttributeProvider*)this;

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDAPI_(ULONG) WeaselTSF::AddRef() {
  return ++_cRef;
}

STDAPI_(ULONG) WeaselTSF::Release() {
  LONG cr = --_cRef;

  assert(_cRef >= 0);

  if (_cRef == 0)
    delete this;

  return cr;
}

STDAPI WeaselTSF::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) {
  return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI WeaselTSF::Deactivate() {
  _InvalidateEditSessions();
  if (!_pThreadMgr)
    return S_OK;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  InterlockedIncrement(&g_probeDeactivated);
#endif

  m_client.EndSession();

  _InitTextEditSink(com_ptr<ITfDocumentMgr>());

  _UninitThreadMgrEventSink();

  _UninitKeyEventSink();
  _UninitPreservedKey();

  _UninitLanguageBar();

  _UninitCompartment();

  _UninitThreadFocusSink();

  // End registered candidate UI while its thread manager is still available.
  _cand->DestroyAll();
  _FinalizeComposition();

  _pThreadMgr = NULL;

  _tfClientId = TF_CLIENTID_NULL;

  return S_OK;
}

STDAPI WeaselTSF::ActivateEx(ITfThreadMgr* pThreadMgr,
                             TfClientId tfClientId,
                             DWORD dwFlags) {
  if (!pThreadMgr || tfClientId == TF_CLIENTID_NULL)
    return E_INVALIDARG;
  if (_pThreadMgr)
    return E_UNEXPECTED;
  _InvalidateEditSessions();
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  InterlockedIncrement(&g_probeActivated);
#endif

  com_ptr<ITfDocumentMgr> pDocMgrFocus;
  _activateFlags = dwFlags;

  _pThreadMgr = pThreadMgr;
  _tfClientId = tfClientId;

  ProbeActivationStage(1);
  if (!_InitThreadMgrEventSink())
    goto ExitError;

  if ((_pThreadMgr->GetFocus(&pDocMgrFocus) == S_OK) &&
      (pDocMgrFocus != NULL)) {
    _InitTextEditSink(pDocMgrFocus);
  }

  ProbeActivationStage(2);
  if (!_InitKeyEventSink())
    goto ExitError;

  // if (!_InitDisplayAttributeGuidAtom())
  //	goto ExitError;
  //	some app might init failed because it not provide DisplayAttributeInfo,
  // like some opengl stuff
  _InitDisplayAttributeGuidAtom();

  ProbeActivationStage(3);
  if (!_InitPreservedKey())
    goto ExitError;

  ProbeActivationStage(4);
  if (!_InitLanguageBar())
    goto ExitError;

  if (!_IsKeyboardOpen())
    _SetKeyboardOpen(TRUE);

  ProbeActivationStage(5);
  if (!_InitCompartment())
    goto ExitError;
  ProbeActivationStage(6);
  if (!_InitThreadFocusSink())
    goto ExitError;

  ProbeActivationStage(7);
  ProbeActivationStage(_EnsureServerConnected() ? 100 : 101);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(S_OK);
#endif

  return S_OK;

ExitError:
  Deactivate();
  return E_FAIL;
}

STDMETHODIMP WeaselTSF::OnSetThreadFocus() {
  std::wstring _ToggleImeOnOpenClose{};
  RegGetStringValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                    L"ToggleImeOnOpenClose", _ToggleImeOnOpenClose);
  _isToOpenClose = (_ToggleImeOnOpenClose == L"yes");
  if (m_client.Echo()) {
    m_client.ProcessKeyEvent(0);
    weasel::ResponseParser parser(NULL, NULL, &_status, NULL, &_cand->style());
    bool ok = m_client.GetResponseData(std::ref(parser));
    if (ok)
      _UpdateLanguageBar(_status);
  }
  return S_OK;
}
STDMETHODIMP WeaselTSF::OnKillThreadFocus() {
  _AbortComposition();
  _InvalidateEditSessions();
  return S_OK;
}
BOOL WeaselTSF::_InitThreadFocusSink() {
  com_ptr<ITfSource> pSource;
  if (FAILED(_pThreadMgr->QueryInterface(&pSource)))
    return FALSE;
  if (FAILED(pSource->AdviseSink(IID_ITfThreadFocusSink,
                                 (ITfThreadFocusSink*)this,
                                 &_dwThreadFocusSinkCookie)))
    return FALSE;
  return TRUE;
}
void WeaselTSF::_UninitThreadFocusSink() {
  if (!_pThreadMgr || _dwThreadFocusSinkCookie == TF_INVALID_COOKIE)
    return;

  com_ptr<ITfSource> pSource;
  if (SUCCEEDED(_pThreadMgr->QueryInterface(&pSource)))
    pSource->UnadviseSink(_dwThreadFocusSinkCookie);
  _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
}

STDMETHODIMP WeaselTSF::OnActivated(REFCLSID clsid,
                                    REFGUID guidProfile,
                                    BOOL isActivated) {
  if (!IsEqualCLSID(clsid, c_clsidTextService)) {
    return S_OK;
  }

  if (isActivated) {
    _ShowLanguageBar(TRUE);
    _UpdateLanguageBar(_status);
  } else {
    _DeleteCandidateList();
    _ShowLanguageBar(FALSE);
  }
  return S_OK;
}

void WeaselTSF::_Reconnect() {
  m_client.Disconnect();
  m_client.Connect(NULL);
  m_client.StartSession();
  weasel::ResponseParser parser(NULL, NULL, &_status, NULL, &_cand->style());
  bool ok = m_client.GetResponseData(std::ref(parser));
  if (ok) {
    _UpdateLanguageBar(_status);
  }
}

static unsigned int retry = 0;

bool WeaselTSF::_EnsureServerConnected() {
  if (!m_client.Echo()) {
    _Reconnect();
    retry++;
    if (retry >= 6) {
      HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerExclusiveMutex");
      if (!m_client.Echo() && GetLastError() != ERROR_ALREADY_EXISTS) {
        std::wstring dir = _GetRootDir();
        ShellExecuteW(NULL, L"open", (dir + L"\\start_service.bat").c_str(),
                      NULL, dir.c_str(), SW_HIDE);
        // Retry on a later input event. A detached lambda holding raw `this`
        // could reconnect after the service had already been destroyed.
      }
      if (hMutex) {
        CloseHandle(hMutex);
      }
      retry = 0;
    }
    return (m_client.Echo() != 0);
  } else {
    return true;
  }
}
