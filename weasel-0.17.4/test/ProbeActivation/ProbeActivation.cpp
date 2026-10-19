#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <wrl/client.h>
#include <dbghelp.h>

#include <iomanip>
#include <iostream>
#include <string>
#include "../../include/resource.h"
#include "../../include/ProbeDiagnostics.h"
#include "FailedActivation.h"
#include "HostBoundary.h"
#include "RegistrationTests.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "advapi32.lib")

using Microsoft::WRL::ComPtr;
#include "CompositionTests.h"

static LONG CALLBACK ReportDiagnosticException(EXCEPTION_POINTERS* exception) {
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
    return EXCEPTION_CONTINUE_SEARCH;
  std::wcerr << L"Diagnostic access violation at "
             << exception->ExceptionRecord->ExceptionAddress << L'\n';
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  void* frames[32] = {};
  const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
  for (USHORT index = 0; index < count; ++index) {
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[index]),
                    &displacement, symbol))
      std::cerr << "  " << symbol->Name << "+" << displacement << '\n';
    else
      std::wcerr << L"  " << frames[index] << L'\n';
  }
  std::wcerr.flush();
  return EXCEPTION_CONTINUE_SEARCH;
}

using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID,
                                                     LPVOID*);
static const GUID kInputModeItem = {
    0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};

static void PrintResult(const wchar_t* step, HRESULT result) {
  std::wcout << step << L" = 0x" << std::hex << std::setw(8)
             << std::setfill(L'0') << static_cast<unsigned long>(result)
             << std::dec << L'\n';
}

static bool InspectResources(HMODULE module) {
  bool valid = true;
  for (int id : {IDR_MENU_POPUP, IDR_MENU_POPUP_HANT, IDR_MENU_POPUP_HANS}) {
    HMENU menu = LoadMenuW(module, MAKEINTRESOURCEW(id));
    HMENU popup = menu ? GetSubMenu(menu, 0) : nullptr;
    const bool present = popup && GetMenuState(
        popup, ID_WEASELTRAY_CORRECTION_PROBE, MF_BYCOMMAND) != UINT(-1);
    std::wcout << L"Probe menu resource " << id << L" present = " << present
               << L'\n';
    valid = valid && present;
    if (menu) DestroyMenu(menu);
  }
  for (int id : {IDI_EN, IDI_ZH}) {
    HICON icon = static_cast<HICON>(LoadImageW(
        module, MAKEINTRESOURCEW(id), IMAGE_ICON, 16, 16, LR_SHARED));
    std::wcout << L"Icon resource " << id << L" present = " << (icon != nullptr)
               << L'\n';
    valid = valid && icon != nullptr;
  }
  return valid;
}

static bool InspectProbeExports(HMODULE module) {
  const char* names[] = {
      "WeaselProbeGetActivationStage", "WeaselProbeGetActivationResult",
      "WeaselProbeGetLifecycleCounts", "WeaselProbeGetCandidate",
      "WeaselProbeCopyLanguageBarTrace", "WeaselProbeExerciseLanguageBar",
      "WeaselProbeCaptureResultsInMemory", "WeaselProbeGetCapturedResult"};
  bool valid = true;
  for (const char* name : names) {
    if (!GetProcAddress(module, name)) {
      std::cerr << "Required probe export missing: " << name << '\n';
      valid = false;
    }
  }
  std::wcout << L"Required probe diagnostic exports present = " << valid << L'\n';
  return valid;
}

#include "LocalProfile.h"
#include "LanguageBarOwnership.h"
#include "TemporaryKeyboardCategory.h"
#include "TemporaryHiddenProfile.h"
#include "ClassFactoryTests.h"

// Category registration and profile activation must not share an already
// initialized TSF process when testing category visibility/cache behavior.
static int RunTemporaryCategoryHost(REFCLSID service, bool input, bool hiddenProfile) {
  TemporaryKeyboardCategory category;
  TemporaryHiddenProfile profile;
  int result = 1;
  if (category.Register(service, hiddenProfile) == S_OK) {
    WCHAR path[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
    if (length && length < _countof(path)) {
      const std::wstring executable(path, length);
      const std::wstring directory = executable.substr(0, executable.find_last_of(L"\\/") + 1);
      const bool profileReady = !hiddenProfile ||
          profile.Register(service, directory + L"weaselProbe.dll") == S_OK;
      std::wstring command = L"\"" + executable + L"\" \"" + directory +
          L"weaselProbe.dll\" {B749B45E-7DCE-4496-8414-9EE10970260A} " +
          (hiddenProfile ? (input ? L"--classified-hidden-input" : L"--classified-hidden-cycles") :
                           (input ? L"--classified-input" : L"--classified-cycles"));
      STARTUPINFOW startup = {};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES;
      // PowerShell's transcript handles are not reliable across a native child
      // launch. Use an owned file so a failed child still leaves diagnostics.
      SECURITY_ATTRIBUTES inheritable = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
      HANDLE log = CreateFileW((directory + L"FreshCategoryChild.log").c_str(),
          GENERIC_WRITE, FILE_SHARE_READ, &inheritable, CREATE_ALWAYS,
          FILE_ATTRIBUTE_NORMAL, nullptr);
      HANDLE inputHandle = CreateFileW(L"NUL", GENERIC_READ,
          FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);
      startup.hStdInput = inputHandle;
      startup.hStdOutput = log;
      startup.hStdError = log;
      PROCESS_INFORMATION child = {};
      std::wcout << L"Starting fresh diagnostic after category registration...\n" << std::flush;
      if (profileReady && log != INVALID_HANDLE_VALUE && inputHandle != INVALID_HANDLE_VALUE &&
          CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &child)) {
        CloseHandle(child.hThread);
        const DWORD waited = WaitForSingleObject(child.hProcess, input ? 420000 : 30000);
        DWORD childExit = 1;
        if (waited == WAIT_OBJECT_0 && GetExitCodeProcess(child.hProcess, &childExit)) {
          result = static_cast<int>(childExit);
        } else {
          // Only the process created above may be ended. Never stop user apps.
          std::wcout << L"Fresh diagnostic did not finish within its deadline.\n";
          if (TerminateProcess(child.hProcess, 1)) WaitForSingleObject(child.hProcess, 5000);
        }
        CloseHandle(child.hProcess);
        std::wcout << L"Fresh diagnostic exit = " << result << L'\n';
      } else {
        std::wcout << L"CreateProcess diagnostic error = " << GetLastError() << L'\n';
      }
      if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
      if (inputHandle != INVALID_HANDLE_VALUE) CloseHandle(inputHandle);
    }
  }
  if (!profile.Close()) result = 1;
  if (!category.Close()) result = 1;
  return result;
}

static bool TestRetainedCandidate(HMODULE module, IClassFactory* factory) {
  auto getCandidate = reinterpret_cast<HRESULT(WINAPI*)(ITfTextInputProcessorEx*, IUnknown**)>(
      GetProcAddress(module, "WeaselProbeGetCandidate"));
  auto lifecycle = reinterpret_cast<void(WINAPI*)(LONG*, LONG*, LONG*, LONG*)>(
      GetProcAddress(module, "WeaselProbeGetLifecycleCounts"));
  if (!getCandidate || !lifecycle) return false;
  LONG destroyedBefore = 0, destroyedAfter = 0;
  lifecycle(nullptr, &destroyedBefore, nullptr, nullptr);
  ComPtr<ITfTextInputProcessorEx> service;
  ComPtr<IUnknown> candidate;
  ComPtr<ITfCandidateListUIElementBehavior> behavior;
  HRESULT hr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(service.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = getCandidate(service.Get(), &candidate);
  if (SUCCEEDED(hr)) hr = candidate.As(&behavior);
  service.Reset();
  lifecycle(nullptr, &destroyedAfter, nullptr, nullptr);
  ComPtr<ITfDocumentMgr> document;
  bool valid = SUCCEEDED(hr) && behavior &&
      destroyedAfter == destroyedBefore + 1 &&
      behavior->GetDocumentMgr(&document) == TF_E_DISCONNECTED && !document &&
      behavior->Abort() == TF_E_DISCONNECTED && behavior->Show(FALSE) == S_OK;
  if (behavior) {
    for (int request = 0; request < 2; ++request) {
      BSTR description = nullptr;
      hr = behavior->GetDescription(&description);
      valid = valid && hr == S_OK && description &&
          std::wstring(description, SysStringLen(description)) == L"Candidate List";
      SysFreeString(description);
    }
  }
  std::wcout << L"Retained candidate after service destruction safe = " << valid << L'\n';
  return valid;
}

int wmain(int argc, wchar_t** argv) {
  if(argc==2 && std::wstring(argv[1])==L"--scope-policy")return TestCorrectionScope()?0:1;
  if (argc == 2 && std::wstring(argv[1]) == L"--registration-checks")
    return RegistrationTests::Run() ? 0 : 1;
  if (argc < 3 || argc > 4 ||
      (argc == 4 && std::wstring(argv[3]) != L"--activate" &&
       std::wstring(argv[3]) != L"--profile" &&
       std::wstring(argv[3]) != L"--load-only" &&
       std::wstring(argv[3]) != L"--class-factory" &&
       std::wstring(argv[3]) != L"--composition-checks" &&
       std::wstring(argv[3]) != L"--input" &&
       std::wstring(argv[3]) != L"--scope" &&
       std::wstring(argv[3]) != L"--scope-unknown" &&
       std::wstring(argv[3]) != L"--scope-password" &&
       std::wstring(argv[3]) != L"--cycles" &&
       std::wstring(argv[3]) != L"--keyboard-cycles" &&
       std::wstring(argv[3]) != L"--keyboard-input" &&
       std::wstring(argv[3]) != L"--classified-cycles" &&
       std::wstring(argv[3]) != L"--classified-input" &&
       std::wstring(argv[3]) != L"--keyboard-hidden-cycles" &&
       std::wstring(argv[3]) != L"--keyboard-hidden-input" &&
       std::wstring(argv[3]) != L"--classified-hidden-cycles" &&
       std::wstring(argv[3]) != L"--classified-hidden-input" &&
       std::wstring(argv[3]) != L"--documents")) {
    std::wcerr << L"Usage: ProbeActivation.exe --registration-checks\n";
    std::wcerr << L"Usage: ProbeActivation.exe <DLL path> <service CLSID> --class-factory\n";
    std::wcerr << L"Usage: ProbeActivation.exe <probe DLL path> <service CLSID> --composition-checks\n";
    std::wcerr << L"Usage: ProbeActivation.exe <DLL path> <service CLSID> [--load-only|--activate|--profile|--input|--scope|--scope-unknown|--scope-password|--cycles|--documents|--keyboard-cycles|--keyboard-input]\n";
    return 2;
  }

  CLSID serviceClsid;
  HRESULT hr = CLSIDFromString(argv[2], &serviceClsid);
  PrintResult(L"CLSIDFromString", hr);
  if (FAILED(hr)) return 2;

  if (argc == 4 && std::wstring(argv[3]) == L"--class-factory")
    return RunClassFactoryChecks(argv[1], serviceClsid);

  if (argc == 4 && std::wstring(argv[3]) == L"--composition-checks") {
    // Automated failure injection must not leave a modal Windows error dialog.
    // These settings apply only to this standalone diagnostic process.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1, ReportDiagnosticException);
    return RunCompositionChecks(argv[1], serviceClsid);
  }

  if (argc == 4 && std::wstring(argv[3]) == L"--load-only")
    return TestHostExceptionFilter(argv[1]) ? 0 : 1;

  hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  PrintResult(L"CoInitializeEx", hr);
  if (FAILED(hr)) return 2;

  if (argc == 4 && (std::wstring(argv[3]) == L"--keyboard-cycles" ||
                    std::wstring(argv[3]) == L"--keyboard-input" ||
                    std::wstring(argv[3]) == L"--keyboard-hidden-cycles" ||
                    std::wstring(argv[3]) == L"--keyboard-hidden-input")) {
    const int result = RunTemporaryCategoryHost(serviceClsid,
        std::wstring(argv[3]) == L"--keyboard-input" ||
            std::wstring(argv[3]) == L"--keyboard-hidden-input",
        std::wstring(argv[3]).find(L"--keyboard-hidden-") == 0);
    CoUninitialize();
    return result;
  }

  int exitCode = 1;
  HMODULE module = LoadLibraryW(argv[1]);
  if (!module) {
    std::wcerr << L"LoadLibraryW error = " << GetLastError() << L'\n';
    CoUninitialize();
    return exitCode;
  }
  std::wcout << L"LoadLibraryW = success\n";
  const CLSID probeClsid = {0xb749b45e, 0x7dce, 0x4496,
      {0x84, 0x14, 0x9e, 0xe1, 0x09, 0x70, 0x26, 0x0a}};
  if (IsEqualCLSID(serviceClsid, probeClsid) && !InspectProbeExports(module)) {
    FreeLibrary(module);
    CoUninitialize();
    return 1;
  }
  PVOID exceptionReporter = AddVectoredExceptionHandler(1, ReportDiagnosticException);

  {
    auto getClassObject = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(module, "DllGetClassObject"));
    if (!getClassObject) {
      std::wcerr << L"DllGetClassObject export missing\n";
    } else {
      ComPtr<IClassFactory> factory;
      hr = getClassObject(serviceClsid, __uuidof(IClassFactory),
                          reinterpret_cast<void**>(factory.GetAddressOf()));
      PrintResult(L"DllGetClassObject", hr);
      if (SUCCEEDED(hr)) {
        ComPtr<ITfTextInputProcessorEx> processor;
        hr = factory->CreateInstance(
            nullptr, __uuidof(ITfTextInputProcessorEx),
            reinterpret_cast<void**>(processor.GetAddressOf()));
        PrintResult(L"CreateInstance(ITfTextInputProcessorEx)", hr);
        if (SUCCEEDED(hr)) {
          const bool resourcesValid = InspectResources(module);
          // Negative lifecycle checks target the new probe DLL only. Older
          // installed DLLs may not support Deactivate before activation.
          const bool lifecycleValid =
              !GetProcAddress(module, "WeaselProbeGetActivationStage") ||
              TestFailedActivation(processor.Get());
          const bool retentionValid = !GetProcAddress(module, "WeaselProbeGetCandidate") ||
              TestRetainedCandidate(module, factory.Get());
          const bool ownershipValid = TestLanguageBarOwnership(module, processor.Get());
          exitCode = resourcesValid && lifecycleValid && retentionValid && ownershipValid ? 0 : 1;
          if (argc == 4 && (std::wstring(argv[3]) == L"--profile" ||
                            std::wstring(argv[3]) == L"--input" ||
                            std::wstring(argv[3]).find(L"--scope") == 0 ||
                            std::wstring(argv[3]) == L"--cycles" ||
                            std::wstring(argv[3]) == L"--classified-cycles" ||
                            std::wstring(argv[3]) == L"--classified-input" ||
                            std::wstring(argv[3]) == L"--classified-hidden-cycles" ||
                            std::wstring(argv[3]) == L"--classified-hidden-input" ||
                            std::wstring(argv[3]) == L"--documents")) {
            const bool keyboardCategory = std::wstring(argv[3]) == L"--classified-cycles" ||
                std::wstring(argv[3]) == L"--classified-input" ||
                std::wstring(argv[3]).find(L"--classified-hidden-") == 0;
            const bool profileValid = TestLocalProfile(
                serviceClsid, std::wstring(argv[3]) == L"--input" ||
                    std::wstring(argv[3]) == L"--classified-input" ||
                    std::wstring(argv[3]) == L"--classified-hidden-input",
                std::wstring(argv[3]).find(L"--scope") == 0 ||
                    std::wstring(argv[3]) == L"--cycles" ||
                    std::wstring(argv[3]) == L"--classified-cycles" ||
                    std::wstring(argv[3]) == L"--classified-hidden-cycles" ||
                    std::wstring(argv[3]) == L"--documents",
                std::wstring(argv[3]) == L"--scope-unknown" ? 1 :
                    (std::wstring(argv[3]) == L"--scope-password" ? 2 :
                        (std::wstring(argv[3]) == L"--cycles" ||
                         std::wstring(argv[3]) == L"--classified-cycles" ||
                         std::wstring(argv[3]) == L"--classified-hidden-cycles" ? 3 :
                            (std::wstring(argv[3]) == L"--documents" ? 4 : 0))), keyboardCategory,
                std::wstring(argv[3]).find(L"--classified-hidden-") == 0);
            exitCode = resourcesValid && lifecycleValid && retentionValid && ownershipValid &&
                profileValid ? 0 : 1;
          }
          if (argc == 4 && std::wstring(argv[3]) == L"--activate") {
            exitCode = 1;
            ComPtr<ITfThreadMgr> threadMgr;
            hr = CoCreateInstance(
                CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                __uuidof(ITfThreadMgr),
                reinterpret_cast<void**>(threadMgr.GetAddressOf()));
            PrintResult(L"CoCreateInstance(ITfThreadMgr)", hr);
            if (SUCCEEDED(hr)) {
              TfClientId clientId = TF_CLIENTID_NULL;
              hr = threadMgr->Activate(&clientId);
              PrintResult(L"ITfThreadMgr::Activate", hr);
              if (SUCCEEDED(hr)) {
                std::wcout << L"Application ClientId = " << clientId << L'\n';
                ComPtr<ITfClientId> clientIds;
                hr = threadMgr.As(&clientIds);
                PrintResult(L"QueryInterface(ITfClientId)", hr);
                if (SUCCEEDED(hr)) {
                  hr = clientIds->GetClientId(serviceClsid, &clientId);
                  PrintResult(L"ITfClientId::GetClientId", hr);
                }
                if (SUCCEEDED(hr)) {
                  std::wcout << L"Service ClientId = " << clientId << L'\n';
                  hr = processor->ActivateEx(threadMgr.Get(), clientId, 0);
                  PrintResult(L"ITfTextInputProcessorEx::ActivateEx", hr);
                  auto getStage = reinterpret_cast<LONG(WINAPI*)()>(
                      GetProcAddress(module, "WeaselProbeGetActivationStage"));
                  if (getStage) {
                    std::wcout << L"Probe activation stage = " << getStage()
                               << L'\n';
                  }
                  auto getResult = reinterpret_cast<HRESULT(WINAPI*)()>(
                      GetProcAddress(module, "WeaselProbeGetActivationResult"));
                  if (getResult) {
                    PrintResult(L"Probe stage result", getResult());
                  }
                  if (SUCCEEDED(hr)) {
                    bool languageBarValid = false;
                    ComPtr<ITfLangBarItemMgr> langBarMgr;
                    HRESULT queryHr = threadMgr.As(&langBarMgr);
                    PrintResult(L"QueryInterface(ITfLangBarItemMgr)", queryHr);
                    if (SUCCEEDED(queryHr)) {
                      ComPtr<ITfLangBarItem> item;
                      queryHr = langBarMgr->GetItem(
                          kInputModeItem, item.GetAddressOf());
                      PrintResult(L"ITfLangBarItemMgr::GetItem", queryHr);
                      if (queryHr == S_OK) {
                        TF_LANGBARITEMINFO info = {};
                        HRESULT infoHr = item->GetInfo(&info);
                        PrintResult(L"ITfLangBarItem::GetInfo", infoHr);
                        std::wcout << L"Language bar description = "
                                   << info.szDescription << L'\n';
                        ComPtr<ITfLangBarItemButton> button;
                        queryHr = item.As(&button);
                        PrintResult(L"QueryInterface(ITfLangBarItemButton)",
                                    queryHr);
                        if (SUCCEEDED(queryHr)) {
                          HICON icon = nullptr;
                          queryHr = button->GetIcon(&icon);
                          PrintResult(L"ITfLangBarItemButton::GetIcon", queryHr);
                          std::wcout << L"Icon present = " << (icon != nullptr)
                                     << L'\n';
                          languageBarValid = SUCCEEDED(infoHr) &&
                              IsEqualCLSID(info.clsidService, serviceClsid) &&
                              IsEqualGUID(info.guidItem, kInputModeItem) &&
                              SUCCEEDED(queryHr) && icon != nullptr;
                        }
                      }
                    }
                    exitCode = resourcesValid && lifecycleValid &&
                        languageBarValid ? 0 : 1;
                    PrintResult(L"ITfTextInputProcessor::Deactivate",
                                processor->Deactivate());
                  }
                }
                PrintResult(L"ITfThreadMgr::Deactivate",
                            threadMgr->Deactivate());
              }
            }
          }
        }
      }
    }
  }

  CoUninitialize();
  if (exceptionReporter) RemoveVectoredExceptionHandler(exceptionReporter);
  auto canUnload = reinterpret_cast<HRESULT(WINAPI*)()>(
      GetProcAddress(module, "DllCanUnloadNow"));
  auto lifecycle = reinterpret_cast<void(WINAPI*)(LONG*, LONG*, LONG*, LONG*)>(
      GetProcAddress(module, "WeaselProbeGetLifecycleCounts"));
  if (lifecycle) {
    LONG created = 0, destroyed = 0, activated = 0, deactivated = 0;
    lifecycle(&created, &destroyed, &activated, &deactivated);
    std::wcout << L"Lifecycle: created = " << created << L", destroyed = "
               << destroyed << L", activated = " << activated
               << L", deactivated = " << deactivated << L'\n';
    if (created != destroyed || activated != deactivated) exitCode = 1;
  }
  if (canUnload) PrintResult(L"DllCanUnloadNow after cleanup", canUnload());
  if (lifecycle && (!canUnload || canUnload() != S_OK)) exitCode = 1;
  if (exitCode) {
    auto copyTrace = reinterpret_cast<ULONG(WINAPI*)(ProbeLanguageBarEvent*, ULONG)>(
        GetProcAddress(module, "WeaselProbeCopyLanguageBarTrace"));
    if (copyTrace) {
      ProbeLanguageBarEvent events[128] = {};
      const ULONG count = copyTrace(events, _countof(events));
      for (ULONG index = 0; index < count; ++index) {
        const auto& event = events[index];
        WCHAR existingService[40] = {};
        StringFromGUID2(event.existingService, existingService, _countof(existingService));
        std::wcout << L"Language bar trace " << index << L", service = " << std::hex
                   << event.service << L", operation = " << std::dec << event.operation
                   << L", owner = " << event.existingOwner << L", HRESULT = "
                   << std::hex << static_cast<unsigned long>(event.result) << std::dec
                   << L", existing service = " << existingService << L'\n';
      }
    }
  }
  if (canUnload && canUnload() == S_OK) {
    FreeLibrary(module);
  }
  // Remaining module references are released when this diagnostic process exits.
  return exitCode;
}
