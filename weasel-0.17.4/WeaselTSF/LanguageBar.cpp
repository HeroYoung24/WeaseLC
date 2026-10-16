#include "stdafx.h"
#include <resource.h>
#include <thread>
#include <shellapi.h>
#include "WeaselTSF.h"
#include "LanguageBar.h"
#include "CandidateList.h"
#include <WeaselUtility.h>
#include <CorrectionProbe.h>
#ifdef WEASEL_CORRECTION_PROBE_BUILD
#include <ProbeDiagnostics.h>

namespace {
ProbeLanguageBarEvent g_languageBarTrace[128] = {};
LONG g_languageBarTraceCount = 0;

DWORD ExistingLanguageBarOwner(ITfLangBarItemMgr* manager,
                               CLangBarItemButton* button, GUID* service) {
  if (!manager) return 0;
  com_ptr<ITfLangBarItem> existing;
  const HRESULT hr = manager->GetItem(GUID_LBI_INPUTMODE, &existing);
  if (hr == S_FALSE) return 1;
  if (hr != S_OK || !existing) return 0;
  TF_LANGBARITEMINFO info = {};
  if (existing->GetInfo(&info) != S_OK) return 0;
  *service = info.clsidService;
  if (!IsEqualCLSID(info.clsidService, c_clsidTextService)) return 4;
  com_ptr<IUnknown> incomingIdentity, existingIdentity;
  if (button) button->QueryInterface(IID_IUnknown,
      reinterpret_cast<void**>(&incomingIdentity));
  existing->QueryInterface(&existingIdentity);
  return incomingIdentity && incomingIdentity == existingIdentity ? 2 : 3;
}

void RecordLanguageBarEvent(WeaselTSF* service, DWORD operation,
                            HRESULT result, ITfLangBarItemMgr* manager = nullptr,
                            CLangBarItemButton* button = nullptr) {
  const LONG index = InterlockedIncrement(&g_languageBarTraceCount) - 1;
  if (index < _countof(g_languageBarTrace)) {
    ProbeLanguageBarEvent event = {reinterpret_cast<ULONG_PTR>(service), operation, result};
    if (manager) event.existingOwner = ExistingLanguageBarOwner(manager, button,
                                                               &event.existingService);
    g_languageBarTrace[index] = event;
  }
}
}

extern "C" __declspec(dllexport) ULONG WINAPI WeaselProbeCopyLanguageBarTrace(
    ProbeLanguageBarEvent* events, ULONG capacity) {
  if (!events || !capacity) return 0;
  const ULONG count = std::min<ULONG>(capacity, std::min<ULONG>(
      InterlockedCompareExchange(&g_languageBarTraceCount, 0, 0),
      _countof(g_languageBarTrace)));
  std::copy_n(g_languageBarTrace, count, events);
  return count;
}
#endif

static const DWORD LANGBARITEMSINK_COOKIE = 0x42424242;

// RemoveItem identifies items by GUID, not by COM identity. A successful AddItem
// can also be silently ignored. Check the actual object before claiming it.
static HRESULT CheckLanguageBarOwnership(ITfLangBarItemMgr* manager,
                                         ITfLangBarItem* expected) {
  com_ptr<ITfLangBarItem> existing;
  HRESULT hr = manager->GetItem(GUID_LBI_INPUTMODE, &existing);
  if (hr != S_OK) return hr;
  if (!existing) return E_FAIL;
  com_ptr<IUnknown> actualIdentity, expectedIdentity;
  hr = existing->QueryInterface(&actualIdentity);
  if (hr == S_OK) hr = expected->QueryInterface(IID_IUnknown,
      reinterpret_cast<void**>(&expectedIdentity));
  if (hr != S_OK) return hr;
  return actualIdentity == expectedIdentity ? S_OK : E_ACCESSDENIED;
}

#ifdef WEASEL_CORRECTION_PROBE_BUILD
HRESULT WeaselTSF::ProbeExerciseLanguageBar(ITfThreadMgr* manager) {
  if (!manager || _pThreadMgr) return E_INVALIDARG;
  _pThreadMgr = manager;
  const bool initialized = _InitLanguageBar() != FALSE;
  _UninitLanguageBar();
  _pThreadMgr.Release();
  return initialized ? S_OK : E_FAIL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeExerciseLanguageBar(
    ITfTextInputProcessorEx* service, ITfThreadMgr* manager) {
  if (!service) return E_POINTER;
  return static_cast<WeaselTSF*>(service)->ProbeExerciseLanguageBar(manager);
}
#endif

static void HMENU2ITfMenu(HMENU hMenu, ITfMenu* pTfMenu) {
  /* NOTE: Only limited functions are supported */
  int N = GetMenuItemCount(hMenu);
  for (int i = 0; i < N; i++) {
    MENUITEMINFO mii;
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
    mii.dwTypeData = NULL;
    if (GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
      UINT id = mii.wID;
      if (mii.fType == MFT_SEPARATOR)
        pTfMenu->AddMenuItem(id, TF_LBMENUF_SEPARATOR, NULL, NULL, NULL, 0,
                             NULL);
      else if (mii.fType == MFT_STRING) {
        mii.dwTypeData = (LPWSTR)malloc(sizeof(WCHAR) * (mii.cch + 1));
        mii.cch++;
        if (GetMenuItemInfo(hMenu, i, TRUE, &mii))
          pTfMenu->AddMenuItem(id, 0, NULL, NULL, mii.dwTypeData, mii.cch,
                               NULL);
        free(mii.dwTypeData);
      }
    }
  }
}

static LPCWSTR GetWeaselRegName() {
  LPCWSTR WEASEL_REG_NAME_;
  if (is_wow64())
    WEASEL_REG_NAME_ = WEASEL_PRODUCT_REG_KEY_32;
  else
    WEASEL_REG_NAME_ = WEASEL_PRODUCT_REG_KEY;

  return WEASEL_REG_NAME_;
}

static bool open(const std::wstring& path) {
  return (uintptr_t)ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL,
                                  SW_SHOWNORMAL) > 32;
}

static bool explore(const std::wstring& path) {
  std::wstring quoted_path = L"\"" + path + L"\"";
  return (uintptr_t)ShellExecuteW(NULL, L"explore", quoted_path.c_str(), NULL,
                                  NULL, SW_SHOWNORMAL) > 32;
}

CLangBarItemButton::CLangBarItemButton(com_ptr<WeaselTSF> pTextService,
                                       REFGUID guid,
                                       weasel::UIStyle& style)
    : _status(0),
      _style(style),
      _current_schema_zhung_icon(),
      _current_schema_ascii_icon() {
  DllAddRef();

  _pLangBarItemSink = NULL;
  _cRef = 1;
  _pTextService = pTextService;
  _guid = guid;
  ascii_mode = false;
}

CLangBarItemButton::~CLangBarItemButton() {
  DllRelease();
}

STDAPI CLangBarItemButton::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == NULL)
    return E_INVALIDARG;

  *ppvObject = NULL;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
      IsEqualIID(riid, IID_ITfLangBarItemButton))
    *ppvObject = (ITfLangBarItemButton*)this;
  else if (IsEqualIID(riid, IID_ITfSource))
    *ppvObject = (ITfSource*)this;

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDAPI_(ULONG) CLangBarItemButton::AddRef() {
  return ++_cRef;
}

STDAPI_(ULONG) CLangBarItemButton::Release() {
  LONG cr = --_cRef;
  assert(_cRef >= 0);
  if (_cRef == 0)
    delete this;
  return cr;
}

STDAPI CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
  pInfo->clsidService = c_clsidTextService;
  pInfo->guidItem = _guid;
  pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU |
                   TF_LBI_STYLE_SHOWNINTRAY;
  pInfo->ulSort = 1;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  lstrcpyW(pInfo->szDescription, L"Weasel Context Probe");
#else
  lstrcpyW(pInfo->szDescription, L"WeaselTSF Button");
#endif
  return S_OK;
}

STDAPI CLangBarItemButton::GetStatus(DWORD* pdwStatus) {
  *pdwStatus = _status;
  return S_OK;
}

STDAPI CLangBarItemButton::Show(BOOL fShow) {
  SetLangbarStatus(TF_LBI_STATUS_HIDDEN, fShow ? FALSE : TRUE);
  return S_OK;
}

static LANGID GetActiveProfileLangId() {
  CComPtr<ITfInputProcessorProfileMgr> pInputProcessorProfileMgr;
  HRESULT hr = pInputProcessorProfileMgr.CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_ALL);
  if (FAILED(hr))
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

  TF_INPUTPROCESSORPROFILE profile;
  hr = pInputProcessorProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,
                                                   &profile);
  if (FAILED(hr))
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
  return profile.langid;
}

STDAPI CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip) {
  LANGID langid = get_language_id();
  if (langid == TEXTSERVICE_LANGID_HANS) {
    *pbstrToolTip = SysAllocString(L"左键切换模式，右键打开菜单");
  } else if (langid == TEXTSERVICE_LANGID_HANT) {
    *pbstrToolTip = SysAllocString(L"左鍵切換模式，右鍵打開菜單");
  } else {
    *pbstrToolTip = SysAllocString(
        L"Left-click to switch modes\n\nRight-click for more options");
  }

  return (*pbstrToolTip == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDAPI CLangBarItemButton::OnClick(TfLBIClick click,
                                   POINT pt,
                                   const RECT* prcArea) {
  if (click == TF_LBI_CLK_LEFT) {
    _pTextService->_HandleLangBarMenuSelect(
        ascii_mode ? ID_WEASELTRAY_DISABLE_ASCII : ID_WEASELTRAY_ENABLE_ASCII);
    ascii_mode = !ascii_mode;
    if (_pLangBarItemSink) {
      _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
    }
  } else if (click == TF_LBI_CLK_RIGHT) {
    /* Open menu */
    HWND hwnd = _pTextService->_GetFocusedContextWindow();
    if (hwnd != NULL) {
      LANGID langid = get_language_id();
      HMENU menu;
      if (langid == TEXTSERVICE_LANGID_HANS) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANS));
      } else if (langid == TEXTSERVICE_LANGID_HANT) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANT));
      } else {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
      }
      HMENU popupMenu = GetSubMenu(menu, 0);
      UINT wID = TrackPopupMenuEx(
          popupMenu, TPM_NONOTIFY | TPM_RETURNCMD | TPM_HORPOSANIMATION, pt.x,
          pt.y, hwnd, NULL);
      DestroyMenu(menu);
      _pTextService->_HandleLangBarMenuSelect(wID);
    }
  }
  return S_OK;
}

STDAPI CLangBarItemButton::InitMenu(ITfMenu* pMenu) {
  HMENU menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
  HMENU popupMenu = GetSubMenu(menu, 0);
  HMENU2ITfMenu(popupMenu, pMenu);
  DestroyMenu(menu);
  return S_OK;
}

STDAPI CLangBarItemButton::OnMenuSelect(UINT wID) {
  _pTextService->_HandleLangBarMenuSelect(wID);
  return S_OK;
}

STDAPI CLangBarItemButton::GetIcon(HICON* phIcon) {
  if (ascii_mode) {
    if (_style.current_ascii_icon.empty())
      *phIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_EN), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    else
      *phIcon =
          (HICON)LoadImageW(NULL, _style.current_ascii_icon.c_str(), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON),
                            GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
  } else {
    if (_style.current_zhung_icon.empty())
      *phIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ZH), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    else
      *phIcon =
          (HICON)LoadImageW(NULL, _style.current_zhung_icon.c_str(), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON),
                            GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
  }
  return (*phIcon == NULL) ? E_FAIL : S_OK;
}

STDAPI CLangBarItemButton::GetText(BSTR* pbstrText) {
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  *pbstrText = SysAllocString(L"Weasel Context Probe");
#else
  *pbstrText = SysAllocString(L"WeaselTSF Button");
#endif
  return (*pbstrText == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDAPI CLangBarItemButton::AdviseSink(REFIID riid,
                                      IUnknown* punk,
                                      DWORD* pdwCookie) {
  if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
    return CONNECT_E_CANNOTCONNECT;
  if (_pLangBarItemSink != NULL)
    return CONNECT_E_ADVISELIMIT;

  if (punk->QueryInterface(IID_ITfLangBarItemSink,
                           (LPVOID*)&_pLangBarItemSink) != S_OK) {
    _pLangBarItemSink = NULL;
    return E_NOINTERFACE;
  }
  *pdwCookie = LANGBARITEMSINK_COOKIE;
  return S_OK;
}

STDAPI CLangBarItemButton::UnadviseSink(DWORD dwCookie) {
  if (dwCookie != LANGBARITEMSINK_COOKIE || _pLangBarItemSink == NULL)
    return CONNECT_E_NOCONNECTION;
  _pLangBarItemSink = NULL;
  return S_OK;
}

void CLangBarItemButton::UpdateWeaselStatus(weasel::Status stat) {
  if (stat.ascii_mode != ascii_mode) {
    ascii_mode = stat.ascii_mode;
  }
  if (_current_schema_zhung_icon != _style.current_zhung_icon) {
    _current_schema_zhung_icon = _style.current_zhung_icon;
  }
  if (_current_schema_ascii_icon != _style.current_ascii_icon) {
    _current_schema_ascii_icon = _style.current_ascii_icon;
  }
  if (_pLangBarItemSink) {
    _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
  }
}

void CLangBarItemButton::SetLangbarStatus(DWORD dwStatus, BOOL fSet) {
  BOOL isChange = FALSE;

  if (fSet) {
    if (!(_status & dwStatus)) {
      _status |= dwStatus;
      isChange = TRUE;
    }
  } else {
    if (_status & dwStatus) {
      _status &= ~dwStatus;
      isChange = TRUE;
    }
  }

  if (isChange && _pLangBarItemSink) {
    _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
  }

  return;
}

std::wstring WeaselTSF::_GetRootDir() {
  std::wstring dir{};
  RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(), L"WeaselRoot", dir);
  return dir;
}

void WeaselTSF::_HandleLangBarMenuSelect(UINT wID) {
  std::wstring dir{};
  switch (wID) {
    case ID_WEASELTRAY_CORRECTION_PROBE: {
      _correctionProbePending = false;
      _correctionProbeContext = nullptr;
      com_ptr<ITfDocumentMgr> document;
      com_ptr<ITfContext> context;
      const bool focused = _pThreadMgr &&
          _pThreadMgr->GetFocus(&document) == S_OK && document &&
          document->GetTop(&context) == S_OK && context;
      // Explicit user action can arrive before the focus notification. Bind
      // to the current document rather than retaining a previous document.
      if (focused && (context != _pTextEditSinkContext ||
                      _dwTextEditSinkCookie == TF_INVALID_COOKIE))
        _InitTextEditSink(document);
      if (focused && context == _pTextEditSinkContext &&
          _dwTextEditSinkCookie != TF_INVALID_COOKIE) {
        _correctionProbeContext = context;
        _correctionProbePending = true;
        correction::SaveProbeResult({1, correction::ProbeStatus::AwaitingEdit});
      } else {
        correction::SaveProbeResult({1, correction::ProbeStatus::NoContext});
      }
      break;
    }
    case ID_WEASELTRAY_RERUN_SERVICE:
    case ID_WEASELTRAY_INSTALLDIR:
      if (RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(),
                            L"WeaselRoot", dir) == ERROR_SUCCESS) {
        if (wID == ID_WEASELTRAY_RERUN_SERVICE) {
#ifdef WEASELC_LOCAL_TEST_BUILD
          break; // Explicit launcher owns the local-test backend lifecycle.
#endif
          std::thread th([dir]() {
            ShellExecuteW(NULL, L"open", (dir + L"\\start_service.bat").c_str(),
                          NULL, dir.c_str(), SW_HIDE);
          });
          th.detach();
        } else
          explore(dir);
      }
      break;
    case ID_WEASELTRAY_USERCONFIG:
      if (FAILED(RegGetStringValue(HKEY_CURRENT_USER, WEASEL_PRODUCT_REG_KEY,
                                   L"RimeUserDir", dir)) ||
          dir.empty()) {
        WCHAR _path[MAX_PATH] = {0};
        ExpandEnvironmentStringsW(WEASEL_DEFAULT_USER_DIR, _path, _countof(_path));
        dir = std::wstring(_path);
      }
      if (!dir.empty() && fs::exists(dir))
        explore(dir);
      else
        MessageBoxW(NULL, (L"Not found: " + dir).c_str(), L"RimeUserDir",
                    MB_ICONERROR | MB_OK);
      break;
    case ID_WEASELTRAY_LOGDIR:
      explore(WeaselLogPath().wstring());
      break;
    case ID_WEASELTRAY_WIKI:
      open(L"https://rime.im/docs/");
      break;
    case ID_WEASELTRAY_FORUM:
      open(L"https://rime.im/discuss/");
      break;
    default:
      m_client.TrayCommand(wID);
      break;
  }
}

HWND WeaselTSF::_GetFocusedContextWindow() {
  HWND hwnd = NULL;
  ITfDocumentMgr* pDocMgr;
  if (_pThreadMgr->GetFocus(&pDocMgr) == S_OK && pDocMgr != NULL) {
    ITfContext* pContext;
    if (pDocMgr->GetTop(&pContext) == S_OK && pContext != NULL) {
      ITfContextView* pContextView;
      if (pContext->GetActiveView(&pContextView) == S_OK &&
          pContextView != NULL) {
        pContextView->GetWnd(&hwnd);
        pContextView->Release();
      }
      pContext->Release();
    }
    pDocMgr->Release();
  }

  if (hwnd == NULL) {
    HWND hwndForeground = GetForegroundWindow();
    if (GetWindowThreadProcessId(hwndForeground, NULL) == GetCurrentThreadId())
      hwnd = hwndForeground;
  }

  return hwnd;
}

BOOL WeaselTSF::_InitLanguageBar() {
  com_ptr<ITfLangBarItemMgr> pLangBarItemMgr;
  BOOL fRet = FALSE;

  HRESULT hr = _pThreadMgr->QueryInterface(&pLangBarItemMgr);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(hr);
  RecordLanguageBarEvent(this, 1, hr);
#endif
  if (hr != S_OK)
    return FALSE;

  _pLangBarButton.Attach(new CLangBarItemButton(this, GUID_LBI_INPUTMODE,
                                                _cand->style()));
  if (_pLangBarButton == NULL)
    return FALSE;

  hr = pLangBarItemMgr->AddItem(_pLangBarButton);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(hr);
  RecordLanguageBarEvent(this, 2, hr,
      FAILED(hr) ? pLangBarItemMgr.p : nullptr, _pLangBarButton);
#endif
  if (hr != S_OK) {
    _pLangBarButton = NULL;
    return FALSE;
  }

  // Keep the same registration manager for cleanup, including partial failure.
  _pLangBarItemMgr = pLangBarItemMgr;
  hr = CheckLanguageBarOwnership(_pLangBarItemMgr, _pLangBarButton);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(hr);
  RecordLanguageBarEvent(this, 5, hr,
      hr != S_OK ? pLangBarItemMgr.p : nullptr, _pLangBarButton);
#endif
  if (hr != S_OK) return FALSE;

  _pLangBarButton->Show(TRUE);
  fRet = TRUE;

  return fRet;
}

void WeaselTSF::_UninitLanguageBar() {
  if (_pLangBarButton == NULL) {
    _pLangBarItemMgr.Release();
    return;
  }

  HRESULT hr = _pLangBarItemMgr ?
      CheckLanguageBarOwnership(_pLangBarItemMgr, _pLangBarButton) : E_FAIL;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  RecordLanguageBarEvent(this, 4, hr);
#endif
  // S_FALSE means no visible matching item. Removing our registration is safe;
  // a foreign object with the same GUID must never be removed.
  if (_pLangBarItemMgr && (hr == S_OK || hr == S_FALSE)) {
    hr = _pLangBarItemMgr->RemoveItem(_pLangBarButton);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    RecordLanguageBarEvent(this, 3, hr,
        FAILED(hr) ? _pLangBarItemMgr.p : nullptr, _pLangBarButton);
#endif
  }

  _pLangBarButton = NULL;
  _pLangBarItemMgr.Release();
}

void WeaselTSF::_UpdateLanguageBar(weasel::Status stat) {
  if (!_pLangBarButton)
    return;
  DWORD flags;
  _GetCompartmentDWORD(flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
  if (stat.ascii_mode)
    flags &= (~TF_CONVERSIONMODE_NATIVE);
  else
    flags |= TF_CONVERSIONMODE_NATIVE;
  if (stat.full_shape)
    flags |= TF_CONVERSIONMODE_FULLSHAPE;
  else
    flags &= (~TF_CONVERSIONMODE_FULLSHAPE);
  _SetCompartmentDWORD(flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);

  _pLangBarButton->UpdateWeaselStatus(stat);
}

void WeaselTSF::_ShowLanguageBar(BOOL show) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->Show(show);
}

void WeaselTSF::_EnableLanguageBar(BOOL enable) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->SetLangbarStatus(TF_LBI_STATUS_DISABLED, !enable);
}
