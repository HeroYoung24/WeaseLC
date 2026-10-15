#pragma once
#include "../../include/CorrectionProbe.h"
#include <vector>

class ScratchEditSession final : public ITfEditSession {
 public:
  explicit ScratchEditSession(ITfContext* context) : context_(context) {}
  STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfEditSession) return E_NOINTERFACE;
    *object = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG result = --references_; if (!result) delete this; return result;
  }
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    const std::wstring synthetic = std::wstring(80, L'A') + std::wstring(80, L'B');
    ComPtr<ITfRange> range;
    HRESULT hr = context_->GetStart(cookie, &range);
    if (SUCCEEDED(hr)) hr = range->SetText(cookie, 0, synthetic.c_str(),
                                          static_cast<LONG>(synthetic.size()));
    if (SUCCEEDED(hr)) hr = range->Collapse(cookie, TF_ANCHOR_START);
    LONG shifted = 0;
    if (SUCCEEDED(hr)) hr = range->ShiftEnd(cookie, 80, &shifted, nullptr);
    if (SUCCEEDED(hr) && shifted != 80) return E_FAIL;
    if (SUCCEEDED(hr)) hr = range->Collapse(cookie, TF_ANCHOR_END);
    if (SUCCEEDED(hr)) {
      TF_SELECTION selection = {range.Get(), {TF_AE_END, FALSE}};
      hr = context_->SetSelection(cookie, 1, &selection);
    }
    return hr;
  }
 private:
  ULONG references_ = 1;
  ComPtr<ITfContext> context_;
};

static bool TestContextProbe(ITfThreadMgr* manager, TfClientId client, HMODULE module,
    ITfInputProcessorProfileMgr* profiles, REFCLSID serviceClsid, REFGUID profile,
    int scenario, ITfDocumentMgr* restoreDocument, bool requireKeyboardCategory) {
  auto capture = reinterpret_cast<void(WINAPI*)(BOOL)>(
      GetProcAddress(module, "WeaselProbeCaptureResultsInMemory"));
  auto getResult = reinterpret_cast<HRESULT(WINAPI*)(correction::ProbeResult*)>(
      GetProcAddress(module, "WeaselProbeGetCapturedResult"));
  auto getStage = reinterpret_cast<LONG(WINAPI*)()>(
      GetProcAddress(module, "WeaselProbeGetActivationStage"));
  auto getActivationResult = reinterpret_cast<HRESULT(WINAPI*)()>(
      GetProcAddress(module, "WeaselProbeGetActivationResult"));
  auto lifecycle = reinterpret_cast<void(WINAPI*)(LONG*, LONG*, LONG*, LONG*)>(
      GetProcAddress(module, "WeaselProbeGetLifecycleCounts"));
  if (!capture || !getResult || !getStage || !getActivationResult || !lifecycle) return false;
  capture(TRUE);
  ComPtr<ITfLangBarItemMgr> items;
  ComPtr<ITfLangBarItem> item;
  ComPtr<ITfLangBarItemButton> button;
  HRESULT hr = manager->QueryInterface(IID_ITfLangBarItemMgr,
      reinterpret_cast<void**>(items.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = items->GetItem(kInputModeItem, &item);
  if (hr == S_OK && item) hr = item.As(&button);
  else hr = E_FAIL;
  bool valid = SUCCEEDED(hr) && button;
  struct ScratchDocument {
    ComPtr<MemoryTextStore> store;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> context;
  };
  std::vector<ScratchDocument> scratchDocuments;
  const bool switchDocumentsOnly = scenario == 4;
  for (int iteration = 0; valid && iteration < (scenario >= 3 ? 3 : 1); ++iteration) {
    const int scope = scenario >= 3 ? iteration : scenario;
    capture(TRUE);
    ComPtr<MemoryTextStore> store;
    store.Attach(new MemoryTextStore(scope == 2 ? IS_PASSWORD : IS_TEXT,
                                     scope != 1));
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> context;
    TfEditCookie editCookie = 0;
    hr = manager->CreateDocumentMgr(&document);
    if (SUCCEEDED(hr)) hr = document->CreateContext(client, 0,
        static_cast<ITextStoreACP*>(store.Get()), &context, &editCookie);
    if (SUCCEEDED(hr)) hr = document->Push(context.Get());
    LONG activationBefore = 0;
    lifecycle(nullptr, nullptr, &activationBefore, nullptr);
    button.Reset();
    item.Reset();
    items.Reset();
    if (SUCCEEDED(hr) && !switchDocumentsOnly) {
      hr = profiles->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,
          0x0804, serviceClsid, profile, nullptr, TF_IPPMF_FORPROCESS);
      PrintResult(L"Probe scenario deactivate", hr);
      PumpTsfMessages();
    }
    if (SUCCEEDED(hr)) hr = manager->SetFocus(document.Get());
    PumpTsfMessages();
    TF_INPUTPROCESSORPROFILE registered = {}, active = {};
    PrintResult(L"Get registered process profile", profiles->GetProfile(
        TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, serviceClsid, profile, nullptr, &registered));
    PrintResult(L"Get active keyboard profile", profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active));
    std::wcout << L"Registered flags = " << registered.dwFlags
               << L", category is null = " << IsEqualGUID(registered.catid, GUID_NULL)
               << L", active is probe = " << IsEqualCLSID(active.clsid, serviceClsid)
               << L", thread keyboard language = "
               << LOWORD(reinterpret_cast<ULONG_PTR>(GetKeyboardLayout(0))) << L'\n';
    if (SUCCEEDED(hr) && !switchDocumentsOnly) {
      hr = profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,
          0x0804, serviceClsid, profile, nullptr,
          TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
      PrintResult(L"Probe scenario activate", hr);
    }
    const ULONGLONG activationDeadline = GetTickCount64() + 2000;
    LONG observedActivations = activationBefore;
    do {
      PumpTsfMessages();
      lifecycle(nullptr, nullptr, &observedActivations, nullptr);
      if (switchDocumentsOnly || observedActivations > activationBefore || FAILED(hr)) break;
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    } while (GetTickCount64() < activationDeadline);
    const HRESULT activeHr = profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active);
    PrintResult(L"Get active keyboard profile after activation", activeHr);
    std::wcout << L"Active after activation is probe = "
               << IsEqualCLSID(active.clsid, serviceClsid) << L'\n';
    LONG activationAfter = 0;
    lifecycle(nullptr, nullptr, &activationAfter, nullptr);
    const bool activated = (switchDocumentsOnly ? activationAfter == activationBefore :
        activationAfter > activationBefore) &&
        (getStage() == 100 || getStage() == 101) && getActivationResult() == S_OK;
    std::wcout << (switchDocumentsOnly ? L"Active service preserved = " : L"Fresh activation = ")
               << activated << L", stage = " << getStage()
               << L", attempts before/after = " << activationBefore << L"/"
               << activationAfter << L'\n';
    PrintResult(L"Fresh activation stage result", getActivationResult());
    button.Reset();
    item.Reset();
    if (SUCCEEDED(hr)) hr = manager->QueryInterface(IID_ITfLangBarItemMgr,
        reinterpret_cast<void**>(items.GetAddressOf()));
    if (SUCCEEDED(hr)) hr = items->GetItem(kInputModeItem, &item);
    PrintResult(L"Probe scenario language bar item", hr);
    if (hr == S_OK && item) hr = item.As(&button);
    else hr = E_FAIL;
    if (SUCCEEDED(hr) && button) hr = button->OnMenuSelect(ID_WEASELTRAY_CORRECTION_PROBE);
    correction::ProbeResult pending;
    getResult(&pending);
    std::wcout << L"Probe scenario " << scope << L", arm HRESULT = "
               << std::hex << static_cast<unsigned long>(hr) << std::dec
               << L", pending status = " << static_cast<DWORD>(pending.status) << L'\n';
    const bool categoryValid = !requireKeyboardCategory ||
        (activeHr == S_OK && IsEqualCLSID(active.clsid, serviceClsid) &&
         IsEqualGUID(active.guidProfile, profile) &&
         IsEqualGUID(registered.catid, GUID_TFCAT_TIP_KEYBOARD));
    bool scenarioValid = activated && categoryValid && SUCCEEDED(hr) &&
        pending.status == correction::ProbeStatus::AwaitingEdit;
    if (scenarioValid) {
      ComPtr<ScratchEditSession> edit;
      edit.Attach(new ScratchEditSession(context.Get()));
      HRESULT sessionResult = E_PENDING;
      hr = context->RequestEditSession(client, edit.Get(),
          TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
      PrintResult(L"Synthetic probe edit request", hr);
      PrintResult(L"Synthetic probe edit session", sessionResult);
      PumpTsfMessages();
      correction::ProbeResult result;
      getResult(&result);
      const auto expected = scope == 0 ? correction::ProbeStatus::Readable :
          (scope == 1 ? correction::ProbeStatus::ScopeUnavailable :
                           correction::ProbeStatus::ProtectedField);
      scenarioValid = SUCCEEDED(hr) && SUCCEEDED(sessionResult) &&
          result.status == expected &&
          result.before == (scope == 0 ? 64U : 0U) &&
          result.after == (scope == 0 ? 64U : 0U);
      std::wcout << L"Context scope scenario " << scope << L", status = "
                 << static_cast<DWORD>(result.status) << L", before = "
                 << result.before << L", after = " << result.after
                 << L", valid = " << scenarioValid << L'\n';
      if (scenarioValid && switchDocumentsOnly && scope == 0) {
        capture(TRUE);
        hr = button->OnMenuSelect(ID_WEASELTRAY_CORRECTION_PROBE);
        correction::ProbeResult armed;
        getResult(&armed);
        scenarioValid = hr == S_OK && armed.status == correction::ProbeStatus::AwaitingEdit;
        hr = manager->SetFocus(restoreDocument);
        PumpTsfMessages();
        sessionResult = E_PENDING;
        if (hr == S_OK) hr = context->RequestEditSession(client, edit.Get(),
            TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
        PumpTsfMessages();
        correction::ProbeResult canceled;
        getResult(&canceled);
        scenarioValid = scenarioValid && hr == S_OK && sessionResult == S_OK &&
            canceled.status == correction::ProbeStatus::FocusChanged &&
            canceled.before == 0 && canceled.after == 0;
        std::wcout << L"Probe canceled on focus change = " << scenarioValid << L'\n';
      }
    }
    PrintResult(L"Restore scratch document focus", manager->SetFocus(restoreDocument));
    PumpTsfMessages();
    // Retain all scratch windows/documents during switching. Destroying a
    // host window also invokes TSF host teardown and is a different test.
    scratchDocuments.push_back({store, document, context});
    valid = scenarioValid && valid;
  }
  for (auto& scratch : scratchDocuments)
    if (scratch.document) scratch.document->Pop(TF_POPF_ALL);
  capture(FALSE);
  std::wcout << L"Context probe boundary checks = " << valid << L'\n';
  return valid;
}
