// WeaselDeployer.cpp : Defines the entry point for the application.
//
#include "stdafx.h"
#include <WeaselUtility.h>
#include <fstream>
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "CorrectionSettingsDialog.h"
#include "RimeLearningChecks.h"
#include "CorrectionWorker.h"
#include "LocalModelControl.h"
#include <WeaselCommandLine.h>
#include <LocalModelWindow.h>
#include "CorrectionPreferences.h"

CAppModule _Module;

static int Run(LPTSTR lpCmdLine);
int RunCorrectionBroker();

int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR lpCmdLine,
                       int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  std::wstring option;
  if (!weasel::ParseSingleOption(GetCommandLineW(), option) || !weasel::DeployerOption(option)) return 2;
  lpCmdLine = option.data();
  if (option == L"/stop-local-model") return correction::SignalModelControl(true) ? 0 : 1;
  if (!wcscmp(L"/correction-broker", lpCmdLine)) return RunCorrectionBroker();
  if (!wcscmp(L"/correction-worker", lpCmdLine)) return RunCorrectionWorker();
  if (!wcscmp(L"/correction-worker-selftest", lpCmdLine)) return RunCorrectionWorker(true);
  if (!wcscmp(L"/rime-learning-selftest", lpCmdLine)) {
    try { return CheckRimeLearning() ? 0 : 1; } catch (...) { return 1; }
  }
  if (!wcscmp(L"/local-model-selftest", lpCmdLine)) return CheckOwnedLocalModel() ? 0 : 1;

  if (!wcscmp(L"/correction-selftest", lpCmdLine)) {
    return CheckCorrectionDialogResources(hInstance) ? 0 : 1;
  }
  if (!wcscmp(L"/correction-local-selftest", lpCmdLine)) {
    return CheckCorrectionLocalDialog(hInstance) ? 0 : 1;
  }
  LANGID langId = get_language_id();
  SetThreadUILanguage(langId);
  SetThreadLocale(langId);

  HRESULT hRes = ::CoInitialize(NULL);
  // If you are running on NT 4.0 or higher you can use the following call
  // instead to make the EXE free threaded. This means that calls come in on a
  // random RPC thread.
  // HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
  ATLASSERT(SUCCEEDED(hRes));

  // this resolves ATL window thunking problem when Microsoft Layer for Unicode
  // (MSLU) is used
  ::DefWindowProc(NULL, 0, 0, 0L);

  AtlInitCommonControls(
      ICC_BAR_CLASSES);  // add flags to support other controls

  hRes = _Module.Init(NULL, hInstance);
  ATLASSERT(SUCCEEDED(hRes));

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);
  if (option == L"/toggle-correction") {
    auto preferences = correction::LoadPreferences();
    const bool enabled = preferences.mode == correction::Mode::Local && preferences.automatic.enabled;
    preferences.mode = enabled ? correction::Mode::Off : correction::Mode::Local;
    preferences.automatic.enabled = !enabled;
    if (!enabled && !correction::ValidateConfig(preferences.local))
      { const INT_PTR result = ShowCorrectionSettings(hInstance,false); _Module.Term(); ::CoUninitialize(); return result == IDOK ? 0 : 1; }
    if (correction::SavePreferences(preferences)) { _Module.Term(); ::CoUninitialize(); return 0; }
    MessageBoxW(nullptr,L"无法保存纠错开关，请从管理纠错中检查设置。 / Could not save correction settings.",L"WeaseLC",MB_OK|MB_ICONERROR); _Module.Term(); ::CoUninitialize(); return 1;
  }


  int ret = 0;
  if (!wcscmp(L"/local-model", lpCmdLine)) {
    ret = ShowLocalModelControl(hInstance);
    _Module.Term(); ::CoUninitialize(); return ret == -1 ? 1 : 0;
  }
  HANDLE hMutex = CreateMutex(NULL, TRUE, WEASEL_DEPLOYER_EXCLUSIVE_MUTEX);
  if (!hMutex) {
    ret = 1;
  } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
    ret = 1;
  } else {
    ret = Run(lpCmdLine);
  }

  if (hMutex) {
    CloseHandle(hMutex);
  }
  _Module.Term();
  ::CoUninitialize();

  return ret;
}

static int Run(LPTSTR lpCmdLine) {
  if (!wcscmp(L"/correction", lpCmdLine) || !wcscmp(L"/correction-demo", lpCmdLine)) {
    return ShowCorrectionSettings(_Module.GetResourceInstance(),
        !wcscmp(L"/correction-demo", lpCmdLine)) == IDOK
               ? 0
               : 1;
  }
  Configurator configurator;
  configurator.Initialize();

  if (!wcscmp(L"/?", lpCmdLine) || !wcscmp(L"/help", lpCmdLine)) {
    WCHAR msg[1024] = {0};
    if (LoadString(GetModuleHandle(NULL), IDS_STR_HELP, msg,
                   sizeof(msg) / sizeof(TCHAR))) {
      MessageBox(NULL, msg, L"Weasel Deployer", MB_ICONINFORMATION | MB_OK);
    } else {
      MessageBox(NULL,
                 L"Usage: WeaselDeployer.exe [options]\n"
                 L"/? or /help		- Show this help message\n"
                 L"/deploy		- Update Workspace\n"
                 L"/dict		- Manage dictionary\n"
                 L"/correction  - Manage correction\n"
                 L"/correction-demo  - Offline correction demo\n"
                 L"/sync		- Sync user data\n"
                 L"/install		- Install Weasel (Initial deployment)",
                 L"Weasel Deployer", MB_ICONINFORMATION | MB_OK);
    }
    return 0;
  }

  bool deployment_scheduled = !wcscmp(L"/deploy", lpCmdLine);
  if (deployment_scheduled) {
    return configurator.UpdateWorkspace();
  }

  bool dict_management = !wcscmp(L"/dict", lpCmdLine);
  if (dict_management) {
    return configurator.DictManagement();
  }

  bool sync_user_dict = !wcscmp(L"/sync", lpCmdLine);
  if (sync_user_dict) {
    return configurator.SyncUserData();
  }

  bool installing = !wcscmp(L"/install", lpCmdLine);
  return configurator.Run(installing);
}

