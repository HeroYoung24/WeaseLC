#pragma once
#include "MemoryTextStore.h"

static void PumpTsfMessages() {
  MSG message;
  unsigned delivered = 0;
  while (delivered++ < 1000 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

#include "ContextProbeTests.h"

static bool TestLocalInput(ITfThreadMgr* manager, REFCLSID serviceClsid,
                           MemoryTextStore* store, bool checkInput) {
  ComPtr<ITfLangBarItemMgr> items;
  ComPtr<ITfLangBarItem> item;
  ComPtr<ITfLangBarItemButton> button;
  HRESULT hr = manager->QueryInterface(IID_ITfLangBarItemMgr,
      reinterpret_cast<void**>(items.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = items->GetItem(kInputModeItem, &item);
  if (hr == S_OK && item) hr = item.As(&button);
  else hr = E_FAIL;
  PrintResult(L"System language bar button", hr);
  if (FAILED(hr) || !button) return false;
  TF_LANGBARITEMINFO info = {};
  HICON icon = nullptr;
  ComPtr<MenuCapture> menu;
  menu.Attach(new MenuCapture());
  const bool barValid = item->GetInfo(&info) == S_OK &&
      IsEqualCLSID(info.clsidService, serviceClsid) &&
      IsEqualGUID(info.guidItem, kInputModeItem) &&
      button->GetIcon(&icon) == S_OK && icon &&
      button->InitMenu(menu.Get()) == S_OK && menu->correction && menu->probe;
  std::wcout << L"System language bar identity/icon/menus valid = " << barValid << L'\n';
  if (!barValid) return false;
  if (!checkInput) return true;
  button->OnMenuSelect(ID_WEASELTRAY_DISABLE_ASCII);
  PumpTsfMessages();
  ComPtr<ITfKeystrokeMgr> keys;
  hr = manager->QueryInterface(IID_ITfKeystrokeMgr,
      reinterpret_cast<void**>(keys.GetAddressOf()));
  if (FAILED(hr)) return false;
  BOOL threadFocus = FALSE;
  manager->IsThreadFocus(&threadFocus);
  CLSID foreground = CLSID_NULL;
  PrintResult(L"ITfKeystrokeMgr::GetForeground", keys->GetForeground(&foreground));
  std::wcout << L"Thread has foreground focus = " << threadFocus
             << L", foreground service is probe = "
             << IsEqualCLSID(foreground, serviceClsid) << L'\n';
  BYTE savedKeyboard[256] = {}, testKeyboard[256] = {};
  GetKeyboardState(savedKeyboard);
  SetKeyboardState(testKeyboard);
  bool keysValid = true;
  for (UINT key : {UINT('N'), UINT('I'), UINT('H'), UINT('A'), UINT('O'), UINT(VK_SPACE)}) {
    const LPARAM keyInfo = 1 | (static_cast<LPARAM>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC)) << 16);
    BOOL eaten = FALSE;
    hr = keys->TestKeyDown(key, keyInfo, &eaten);
    if (SUCCEEDED(hr) && eaten) hr = keys->KeyDown(key, keyInfo, &eaten);
    std::wcout << L"TSF key " << key << L" eaten = " << eaten << L'\n';
    keysValid = SUCCEEDED(hr) && eaten && keysValid;
    PumpTsfMessages();
    BOOL upEaten = FALSE;
    const LPARAM upInfo = keyInfo | (LPARAM(1) << 30) | (LPARAM(1) << 31);
    hr = keys->TestKeyUp(key, upInfo, &upEaten);
    if (SUCCEEDED(hr) && upEaten) keys->KeyUp(key, upInfo, &upEaten);
    PumpTsfMessages();
  }
  SetKeyboardState(savedKeyboard);
  const bool chinese = std::any_of(store->text.begin(), store->text.end(),
      [](WCHAR character) { return character >= 0x4e00 && character <= 0x9fff; });
  std::wcout << L"Scratch text UTF-16 units = " << store->text.size()
             << L", Chinese committed = " << chinese
             << L", candidate starts = " << store->candidateStarts
             << L", candidate ends = " << store->candidateEnds << L'\n';
  // Print only synthetic code points, never text from a user's document.
  for (WCHAR character : store->text)
    std::wcout << L"Synthetic character U+" << std::hex
               << static_cast<unsigned>(character) << std::dec << L'\n';
  return keysValid && chinese && store->candidateStarts > 0;
}

// Real TSF activation, restricted to this diagnostic process. The activation
// context supplies COM identity without installing the DLL in the registry.
static bool TestLocalProfile(REFCLSID serviceClsid, bool checkInput, bool checkScope,
                             int scopeScenario, bool requireKeyboardCategory, bool hiddenProfile) {
  const CLSID probeClsid = {0xb749b45e, 0x7dce, 0x4496,
      {0x84, 0x14, 0x9e, 0xe1, 0x09, 0x70, 0x26, 0x0a}};
  if (!IsEqualCLSID(serviceClsid, probeClsid)) return false;
  const GUID profile = {
      0xa2f702d2, 0xdd84, 0x4c9e,
      {0x9c, 0x18, 0x66, 0xfe, 0xba, 0x75, 0xc4, 0xe3}};
  WCHAR executable[MAX_PATH] = {};
  if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return false;
  std::wstring directory(executable);
  directory.resize(directory.find_last_of(L"\\/") + 1);
  const std::wstring manifest = directory + L"ProbeProfile.manifest";
  const std::wstring dll = directory + L"weaselProbe.dll";
  ACTCTXW activation = {};
  activation.cbSize = sizeof(activation);
  activation.lpSource = manifest.c_str();
  HANDLE context = CreateActCtxW(&activation);
  if (context == INVALID_HANDLE_VALUE) {
    std::wcout << L"CreateActCtx error = " << GetLastError() << L'\n';
    return false;
  }
  ULONG_PTR contextCookie = 0;
  if (!ActivateActCtx(context, &contextCookie)) {
    ReleaseActCtx(context);
    return false;
  }

  HMODULE module = GetModuleHandleW(L"weaselProbe.dll");
  auto lifecycle = module ? reinterpret_cast<void(WINAPI*)(LONG*, LONG*, LONG*, LONG*)>(
      GetProcAddress(module, "WeaselProbeGetLifecycleCounts")) : nullptr;
  LONG activationBefore = 0, activationAfter = 0;
  // TSF can activate during registration, thread-manager activation, or focus
  // changes. Compare with the start of this test, after the preceding mocks.
  if (lifecycle) lifecycle(nullptr, nullptr, &activationBefore, nullptr);
  bool valid = false;
  {
    ComPtr<ITfTextInputProcessorEx> resolved;
    HRESULT hr = CoCreateInstance(serviceClsid, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfTextInputProcessorEx,
        reinterpret_cast<void**>(resolved.GetAddressOf()));
    PrintResult(L"Registration-free CoCreateInstance(probe)", hr);
    if (SUCCEEDED(hr)) {
      resolved.Reset();
      ComPtr<ITfThreadMgr> manager;
      hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
          IID_ITfThreadMgr, reinterpret_cast<void**>(manager.GetAddressOf()));
      PrintResult(L"Local profile thread manager", hr);
      if (SUCCEEDED(hr)) {
        TfClientId client = TF_CLIENTID_NULL;
        hr = manager->Activate(&client);
        PrintResult(L"Local profile thread manager Activate", hr);
        if (SUCCEEDED(hr)) {
          ComPtr<ITfInputProcessorProfileMgr> profiles;
          hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
              CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
              reinterpret_cast<void**>(profiles.GetAddressOf()));
          PrintResult(L"Local profile manager", hr);
          if (SUCCEEDED(hr)) {
            constexpr LANGID language = 0x0804;
            const WCHAR description[] = L"Weasel Probe Process Test";
            const DWORD registrationFlags = TF_RP_HIDDENINSETTINGUI | TF_RP_LOCALPROCESS;
            // RP_HIDDENINSETTINGUI shares its numeric bit with URP_ALLPROFILES.
            // Never pass registration flags to the unregistration API.
            const DWORD unregistrationFlags = TF_URP_LOCALPROCESS;
            if (hiddenProfile) {
              TF_INPUTPROCESSORPROFILE registered = {};
              hr = profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, language,
                  serviceClsid, profile, nullptr, &registered);
              PrintResult(L"Hidden profile GetProfile before activation", hr);
              ComPtr<ITfInputProcessorProfiles> legacy;
              const HRESULT legacyHr = profiles.As(&legacy);
              PrintResult(L"Hidden profile legacy metadata interface", legacyHr);
              bool readableEnabled = false;
              if (legacyHr == S_OK) {
                BOOL enabled = FALSE;
                const HRESULT enabledHr = legacy->IsEnabledLanguageProfile(serviceClsid,
                    language, profile, &enabled);
                PrintResult(L"Hidden profile IsEnabledLanguageProfile", enabledHr);
                std::wcout << L"Hidden profile enabled = " << enabled << L'\n';
                BSTR profileDescription = nullptr;
                const HRESULT descriptionHr = legacy->GetLanguageProfileDescription(serviceClsid,
                    language, profile, &profileDescription);
                PrintResult(L"Hidden profile description metadata", descriptionHr);
                readableEnabled = enabledHr == S_OK && enabled && descriptionHr == S_OK && profileDescription;
                if (profileDescription) SysFreeString(profileDescription);
              }
              const WCHAR* keyPath = L"Software\\Microsoft\\CTF\\TIP\\"
                  L"{B749B45E-7DCE-4496-8414-9EE10970260A}\\LanguageProfile\\"
                  L"0x00000804\\{A2F702D2-DD84-4C9E-9C18-66FEBA75C4E3}";
              HKEY key = nullptr;
              const LSTATUS metadata = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0,
                  KEY_READ, &key);
              std::wcout << L"Fresh child hidden profile registry metadata status = " << metadata << L'\n';
              if (key) RegCloseKey(key);
              // A typed GetProfile failure is diagnostic evidence, not proof
              // the fixed, enabled registry profile cannot be activated. Keep
              // the strict category/active identity gate after activation.
              hr = readableEnabled && metadata == ERROR_SUCCESS ? S_OK : E_FAIL;
            } else {
              hr = profiles->RegisterProfile(serviceClsid, language, profile,
                  description, _countof(description) - 1, dll.c_str(),
                  static_cast<ULONG>(dll.size()), 0, nullptr, 0, TRUE,
                  registrationFlags);
            }
            PrintResult(hiddenProfile ? L"Read temporary hidden profile registered by host" :
                                        L"RegisterProfile(LOCALPROCESS)", hr);
            if (SUCCEEDED(hr)) {
              ComPtr<MemoryTextStore> store;
              store.Attach(new MemoryTextStore());
              ComPtr<ITfSource> source;
              DWORD uiCookie = TF_INVALID_COOKIE;
              HRESULT uiHr = manager.As(&source);
              if (SUCCEEDED(uiHr)) uiHr = source->AdviseSink(IID_ITfUIElementSink,
                  static_cast<ITfUIElementSink*>(store.Get()), &uiCookie);
              PrintResult(L"Scratch candidate UI host", uiHr);
              ComPtr<ITfDocumentMgr> document;
              ComPtr<ITfContext> textContext;
              TfEditCookie editCookie = 0;
              HRESULT docHr = manager->CreateDocumentMgr(&document);
              if (SUCCEEDED(docHr))
                docHr = document->CreateContext(client, 0,
                    static_cast<ITextStoreACP*>(store.Get()),
                    &textContext, &editCookie);
              if (SUCCEEDED(docHr)) docHr = document->Push(textContext.Get());
              if (SUCCEEDED(docHr)) docHr = manager->SetFocus(document.Get());
              PrintResult(L"Local profile document focus", docHr);
              hr = profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,
                  language, serviceClsid, profile, nullptr,
                  TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
              PrintResult(L"ActivateProfile(FORPROCESS)", hr);
              // Deliver TSF's posted activation notifications in this thread.
              PumpTsfMessages();
              if (lifecycle) lifecycle(nullptr, nullptr, &activationAfter, nullptr);
              const bool freshActivation = lifecycle && activationAfter > activationBefore;
              std::wcout << L"Initial fresh activation = " << freshActivation
                         << L", attempts before/after = " << activationBefore << L"/"
                         << activationAfter << L'\n';
              auto getStage = module ? reinterpret_cast<LONG(WINAPI*)()>(
                  GetProcAddress(module, "WeaselProbeGetActivationStage")) : nullptr;
              auto getResult = module ? reinterpret_cast<HRESULT(WINAPI*)()>(
                  GetProcAddress(module, "WeaselProbeGetActivationResult")) : nullptr;
              const LONG stage = getStage ? getStage() : 0;
              std::wcout << (freshActivation ? L"System-triggered probe activation stage = " :
                                               L"Previous diagnostic stage (no fresh activation) = ")
                         << stage << L'\n';
              if (getResult) PrintResult(freshActivation ? L"System-triggered stage result" :
                  L"Previous diagnostic stage result (no fresh activation)", getResult());
              valid = hr == S_OK && SUCCEEDED(docHr) && freshActivation &&
                  getResult && getResult() == S_OK && (stage == 100 || stage == 101);
              if (requireKeyboardCategory) {
                ComPtr<ITfCategoryMgr> categories;
                GUID closest = GUID_NULL;
                HRESULT closestHr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                    CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                    reinterpret_cast<void**>(categories.GetAddressOf()));
                const GUID* choices[] = {&GUID_TFCAT_TIP_KEYBOARD};
                if (closestHr == S_OK) closestHr = categories->FindClosestCategory(
                    serviceClsid, &closest, choices, _countof(choices));
                PrintResult(L"Fresh process FindClosestCategory(KEYBOARD)", closestHr);
                std::wcout << L"Fresh process sees keyboard category = "
                           << (closestHr == S_OK && IsEqualGUID(closest, GUID_TFCAT_TIP_KEYBOARD)) << L'\n';
                TF_INPUTPROCESSORPROFILE registered = {}, active = {};
                const HRESULT registeredHr = profiles->GetProfile(
                    TF_PROFILETYPE_INPUTPROCESSOR, language, serviceClsid, profile,
                    nullptr, &registered);
                const HRESULT activeHr = profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active);
                PrintResult(L"Classified registered keyboard profile", registeredHr);
                PrintResult(L"Classified active keyboard profile", activeHr);
                WCHAR categoryName[40] = {}, activeService[40] = {};
                StringFromGUID2(registered.catid, categoryName, _countof(categoryName));
                StringFromGUID2(active.clsid, activeService, _countof(activeService));
                std::wcout << L"Registered category = " << categoryName
                           << L", active service = " << activeService
                           << L", active profile matches = "
                           << IsEqualGUID(active.guidProfile, profile) << L'\n';
                const bool classified = registeredHr == S_OK && activeHr == S_OK &&
                    IsEqualGUID(registered.catid, GUID_TFCAT_TIP_KEYBOARD) &&
                    IsEqualCLSID(active.clsid, serviceClsid) &&
                    IsEqualGUID(active.guidProfile, profile);
                std::wcout << L"Keyboard classification and active identity valid = " << classified << L'\n';
                valid = valid && classified;
              }
              bool focusedForTest = !checkInput;
              if (valid && SUCCEEDED(uiHr) && checkInput) {
                valid = IsWindow(store->Window()) != FALSE;
                if (!valid) std::wcout << L"Scratch window unavailable; input test skipped.\n" << std::flush;
              }
              if (valid && SUCCEEDED(uiHr) && checkInput) {
                ITfDocumentMgr* previous = nullptr;
                HRESULT focusHr = manager->AssociateFocus(store->Window(),
                    document.Get(), &previous);
                if (previous) previous->Release();
                PrintResult(L"AssociateFocus(scratch window)", focusHr);
                store->ShowMessage(
                    L"请点击此窗口开始测试。\n程序将自动输入测试拼音，无须手动输入。\n"
                    L"仅测试本窗口；完成后关闭窗口即可。");
                // ShowWindow's first call can inherit SW_HIDE from the tool's
                // STARTUPINFO. Explicitly show only this owned test window.
                SetWindowPos(store->Window(), HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                std::wcout << L"Scratch window visible = " << IsWindowVisible(store->Window()) << L'\n' << std::flush;
                std::wcout << L"Waiting for user to focus the scratch test window...\n" << std::flush;
                const ULONGLONG deadline = GetTickCount64() + 180000;
                while (IsWindow(store->Window()) && GetTickCount64() < deadline) {
                  PumpTsfMessages();
                  DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
                  if (foregroundThread == GetCurrentThreadId()) break;
                  MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
                }
                valid = SUCCEEDED(focusHr) && IsWindow(store->Window()) &&
                    GetWindowThreadProcessId(GetForegroundWindow(), nullptr) == GetCurrentThreadId();
                focusedForTest = valid;
                std::wcout << L"Scratch window focused before timeout = " << focusedForTest << L'\n' << std::flush;
                if (focusedForTest) {
                  ::SetFocus(store->Window());
                  manager->SetFocus(document.Get());
                  PumpTsfMessages();
                }
              }
              if (valid && SUCCEEDED(uiHr))
                valid = TestLocalInput(manager.Get(), serviceClsid, store.Get(), checkInput);
              else
                valid = false;
              if (valid && checkScope)
                valid = TestContextProbe(manager.Get(), client, module,
                    profiles.Get(), serviceClsid, profile, scopeScenario, document.Get(),
                    requireKeyboardCategory);
              std::wcout << L"Profile checks valid = " << valid << L'\n' << std::flush;
              if (checkInput && IsWindow(store->Window())) {
                const std::wstring result = valid
                    ? L"测试通过。\n本窗口已完成中文上屏：" + store->text +
                        L"\n语言栏按钮、图标及纠错菜单检查通过。\n请关闭窗口结束测试。"
                    : (focusedForTest
                        ? L"测试未通过。\n请关闭窗口；排查结果会返回当前任务。"
                        : L"测试尚未执行：等待窗口获得焦点已超时。\n请关闭窗口；稍后可重新测试。");
                store->ShowMessage(result);
                const ULONGLONG closeDeadline = GetTickCount64() + 180000;
                while (IsWindow(store->Window()) && GetTickCount64() < closeDeadline) {
                  PumpTsfMessages();
                  MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
                }
              }
              std::wcout << L"Starting local profile cleanup...\n" << std::flush;
              PrintResult(L"DeactivateProfile(FORPROCESS)",
                  profiles->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,
                      language, serviceClsid, profile, nullptr, TF_IPPMF_FORPROCESS));
              manager->SetFocus(nullptr);
              if (document) document->Pop(TF_POPF_ALL);
              if (source && uiCookie != TF_INVALID_COOKIE) source->UnadviseSink(uiCookie);
              if (!hiddenProfile) {
                const HRESULT unregisterHr = profiles->UnregisterProfile(serviceClsid, language,
                    profile, unregistrationFlags);
                PrintResult(L"UnregisterProfile(LOCALPROCESS)", unregisterHr);
                valid = valid && unregisterHr == S_OK;
              }
            }
          }
          PrintResult(L"Local profile thread manager Deactivate", manager->Deactivate());
        }
      }
    }
  }
  DeactivateActCtx(0, contextCookie);
  ReleaseActCtx(context);
  return valid;
}
