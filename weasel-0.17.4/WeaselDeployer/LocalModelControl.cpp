#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <atomic>
#include <winhttp.h>
#define BOOST_JSON_NO_LIB
#define BOOST_CONTAINER_NO_LIB
#include <boost/json.hpp>
#include <CorrectionBrokerProcess.h>
#include <CorrectionProbe.h>
#include <LocalModelWindow.h>
#include "LocalModelControl.h"
#include "LocalModelIdentity.h"
#include "Resource.h"
#pragma comment(lib, "winhttp.lib")

namespace {
namespace fs = std::filesystem;
bool ValidModelFolder(const fs::path& path) {
  const auto value = path.wstring();
  return path.is_absolute() && value.size() < 32000 && value.size() > 2 && value[1] == L':' &&
      value.find_first_of(L"\"\r\n") == std::wstring::npos &&
      fs::is_regular_file(path / L"Start-LocalModel.ps1") &&
      fs::is_regular_file(path / L"runtime" / L"llama-server.exe") &&
      fs::is_regular_file(path / L"Qwen3-4B-Q4_K_M.gguf");
}
fs::path FindModelFolder() {
  wchar_t saved[32768]{}; DWORD bytes = sizeof(saved);
  if (RegGetValueW(HKEY_CURRENT_USER,correction::kRegistryKey,L"LocalModelFolderV1",
      RRF_RT_REG_SZ,nullptr,saved,&bytes) == ERROR_SUCCESS && ValidModelFolder(saved)) return saved;
  wchar_t exe[32768]{};
  const auto length = GetModuleFileNameW(nullptr, exe, _countof(exe));
  if (!length || length == _countof(exe)) return {};
  auto parent = fs::path(exe).parent_path();
  for (unsigned i = 0; i < 6 && !parent.empty(); ++i, parent = parent.parent_path())
    if (ValidModelFolder(parent / L"local-model")) return parent / L"local-model";
  return {};
}
fs::path PowerShell7() {
  PWSTR folder = nullptr;
  fs::path root;
  if (SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0, nullptr, &folder) == S_OK) root = folder;
  if (folder) CoTaskMemFree(folder);
  // The x64 known-folder ID can be unavailable to a 32-bit caller. Windows
  // provides the native Program Files location through ProgramW6432 instead.
  if (root.empty() || !fs::is_regular_file(root / L"PowerShell" / L"7" / L"pwsh.exe")) {
    wchar_t native[32768]{}; const auto size = GetEnvironmentVariableW(L"ProgramW6432", native, _countof(native));
    if (size && size < _countof(native) && size > 2 && native[1] == L':') root = native;
  }
  if (root.empty()) return {};
  const fs::path result = root / L"PowerShell" / L"7" / L"pwsh.exe";
  return fs::is_regular_file(result) ? result : fs::path{};
}
bool ValidPowerShell(const fs::path& executable) {
  const auto text = executable.wstring();
  return executable.is_absolute() && text.size() > 2 && text[1] == L':' && text.size() < 32000 &&
      text.find_first_of(L"\"\r\n") == std::wstring::npos &&
      !_wcsicmp(executable.filename().c_str(), L"pwsh.exe") && fs::is_regular_file(executable);
}
fs::path PreferredPowerShell() {
  wchar_t path[32768]{}; DWORD size = sizeof(path);
  if (RegGetValueW(HKEY_CURRENT_USER, correction::kRegistryKey, L"LocalModelPowerShellV1",
      RRF_RT_REG_SZ | RRF_NOEXPAND, nullptr, path, &size) == ERROR_SUCCESS && ValidPowerShell(path)) return path;
  return PowerShell7();
}
bool SavePowerShell(const std::wstring& path) {
  if (!ValidPowerShell(path)) return false;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, correction::kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const auto result = RegSetValueExW(key, L"LocalModelPowerShellV1", 0, REG_SZ,
      reinterpret_cast<const BYTE*>(path.c_str()), static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key); return result == ERROR_SUCCESS;
}
bool SaveModelFolder(const fs::path& folder) {
  if (!ValidModelFolder(folder)) return false;
  const auto value = folder.wstring(); HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER,correction::kRegistryKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr) != ERROR_SUCCESS) return false;
  const auto status = RegSetValueExW(key,L"LocalModelFolderV1",0,REG_SZ,
      reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
  RegCloseKey(key); return status == ERROR_SUCCESS;
}
std::wstring SelectModelFolder(HWND owner, bool folderMode = true) {
  IFileDialog* dialog = nullptr; std::wstring result;
  if (CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)) != S_OK) return result;
  DWORD flags = 0; dialog->GetOptions(&flags);
  dialog->SetOptions(flags | (folderMode ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST) | FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT);
  if (!folderMode) {
    const COMDLG_FILTERSPEC filter{L"PowerShell 7 (pwsh.exe)", L"pwsh.exe"};
    dialog->SetFileTypes(1, &filter); dialog->SetTitle(L"PowerShell 7 (pwsh.exe)");
  }
  if (dialog->Show(owner) == S_OK) {
    IShellItem* item = nullptr; PWSTR path = nullptr;
    if (dialog->GetResult(&item) == S_OK) {
      if (item->GetDisplayName(SIGDN_FILESYSPATH, &path) == S_OK) { result = path; CoTaskMemFree(path); }
      item->Release();
    }
  }
  dialog->Release(); return result;
}
struct Health {
  std::atomic_bool done{false}; bool ready = false;
};
bool LocalHealth() {
  auto session = WinHttpOpen(L"WeaseLC local health", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
  if (!session) return false;
  WinHttpSetTimeouts(session, 400, 400, 400, 400);
  auto connection = WinHttpConnect(session, L"127.0.0.1", 18963, 0);
  auto request = connection ? WinHttpOpenRequest(connection, L"GET", L"/health", nullptr, nullptr, nullptr, 0) : nullptr;
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, status = 0, bytes = sizeof(status);
  const bool valid = request && WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)) &&
      WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr) &&
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes, nullptr) && status == 200;
  if (request) WinHttpCloseHandle(request); if (connection) WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
  return valid;
}
struct Control {
  correction::BrokerProcess launcher;
  fs::path folder;
  fs::path powershell;
  std::shared_ptr<Health> health;
  std::thread healthThread;
  bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
  bool ready = false;
  bool testing = false;
  bool externalReady = false;
  const wchar_t* Text(const wchar_t* zh, const wchar_t* en) const { return chinese ? zh : en; }
  bool Running() const { return launcher.Process() && WaitForSingleObject(launcher.Process(), 0) == WAIT_TIMEOUT; }
  bool OwnedReady(HANDLE file) const {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 8192) return false;
    std::string text(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size()) return false;
    return ValidOwnedModelReady(std::move(text), [&](DWORD pid) { return launcher.ContainsProcess(pid); });
  }
  bool ReadyFile() const {
    HANDLE file = CreateFileW((folder / L"ready.json").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool owned = false; try { owned = OwnedReady(file); } catch (...) {}
    CloseHandle(file); return owned;
  }
  void Stop() {
    // Hold this exact file object, allowing the launcher to delete it during
    // cooperative shutdown. A replacement at the same path is never deleted.
    HANDLE file = folder.empty() ? INVALID_HANDLE_VALUE : CreateFileW((folder / L"ready.json").c_str(),
        GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    bool owned = false;
    if (file != INVALID_HANDLE_VALUE) try { owned = OwnedReady(file); } catch (...) {}
    launcher.Stop(); ready = false; externalReady = false;
    if (file != INVALID_HANDLE_VALUE) {
      if (owned) { FILE_DISPOSITION_INFO remove{TRUE}; SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof(remove)); }
      CloseHandle(file);
    }
    if (healthThread.joinable()) healthThread.join(); health.reset();
  }
  ~Control() { Stop(); }
};
void BeginHealth(Control& state) {
  if (state.health) return;
  auto health=std::make_shared<Health>();state.health=health;
  state.healthThread=std::thread([health]{try{health->ready=LocalHealth();}catch(...){}health->done.store(true);});
}
bool StartFrozenModel(Control& state) {
  return ValidPowerShell(state.powershell) && ValidModelFolder(state.folder) && state.launcher.Start(state.powershell.wstring(),
      L"-NoProfile -NonInteractive -File \"" + (state.folder / L"Start-LocalModel.ps1").wstring() +
      L"\" -Variant 4b -ThinkingBudget 0 -Port 18963 -StopOnInputClose", 2);
}
void ShowState(HWND dialog, Control& state, const wchar_t* text) {
  SetDlgItemTextW(dialog, IDC_MODEL_STATE, text);
  EnableWindow(GetDlgItem(dialog, IDC_MODEL_START), !state.Running() && !state.externalReady);
  EnableWindow(GetDlgItem(dialog, IDC_MODEL_BROWSE), !state.Running());
  EnableWindow(GetDlgItem(dialog, IDC_MODEL_BROWSE_SHELL), !state.Running());
  EnableWindow(GetDlgItem(dialog, IDC_MODEL_STOP), state.launcher.Process() != nullptr);
}
INT_PTR CALLBACK Proc(HWND dialog, UINT message, WPARAM w, LPARAM l) try {
  auto* state = reinterpret_cast<Control*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<Control*>(l); SetWindowLongPtrW(dialog, DWLP_USER, l);
    if (!state->testing) SetPropW(dialog,correction::kModelWindowProperty,reinterpret_cast<HANDLE>(1));
    if (!state->testing) { state->folder = FindModelFolder(); state->powershell = PreferredPowerShell(); }
    SetDlgItemTextW(dialog, IDC_MODEL_FOLDER, state->folder.c_str());
    SetDlgItemTextW(dialog, IDC_MODEL_SHELL, state->powershell.c_str());
    SetDlgItemTextW(dialog, IDC_MODEL_ENDPOINT, L"http://127.0.0.1:18963/v1/chat/completions");
    SetDlgItemTextW(dialog, IDC_MODEL_ALIAS, L"weasel-qwen3-4b");
    ShowState(dialog, *state, state->Text(L"尚未启动。启动后关闭窗口可保持后台运行；从模型管理重新打开。",
        L"Stopped. Close to keep a started model in the background; reopen from Model management."));
    if (!state->testing) SetTimer(dialog, 1, 1000, nullptr); return TRUE;
  }
  if (!state) return FALSE;
  if (message == WM_DESTROY) { RemovePropW(dialog,correction::kModelWindowProperty); return TRUE; }
  if (message == correction::kModelShow) { ShowWindow(dialog,SW_RESTORE); SetForegroundWindow(dialog); return TRUE; }
  if (message == correction::kModelStop || (message == WM_COMMAND && LOWORD(w) == IDC_MODEL_EXIT)) {
    KillTimer(dialog,1); state->Stop(); EndDialog(dialog,IDCANCEL); return TRUE;
  }
  if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
    if (!state->testing && state->Running()) { ShowWindow(dialog,SW_HIDE); return TRUE; }
    KillTimer(dialog, 1); state->Stop(); EndDialog(dialog, IDCANCEL); return TRUE;
  }
  if (message == WM_TIMER) {
    if (state->testing) return TRUE;
    if (!state->Running()) {
      if (state->launcher.Process()) state->Stop();
      if (state->health && state->health->done.load()) {
        if(state->healthThread.joinable()) state->healthThread.join();
        state->externalReady=state->health->ready;state->health.reset();
      }
      ShowState(dialog, *state, state->externalReady ? state->Text(
          L"检测到其他本地服务；当前窗口未接管。请在原启动程序中停止该服务。",
          L"An external local service is running; this window does not own or stop it.") : state->Text(
          L"未运行；启动失败时请检查所选目录的 runtime.stderr.log。",
          L"Not running. If startup failed, check runtime.stderr.log in the selected folder."));
      BeginHealth(*state);return TRUE;
    }
    if (state->health && state->health->done.load()) {
      if (state->healthThread.joinable()) state->healthThread.join();
      state->ready = state->health->ready && state->ReadyFile(); state->health.reset();
      ShowState(dialog, *state, state->ready ? state->Text(L"已就绪；请在管理纠错中使用下方接口，自动提示仍需单独开启。",
          L"Ready. Use the endpoint below in correction settings; enable hints separately.") :
          state->Text(L"正在校验、载入或预热；等待就绪再开启纠错。", L"Verifying, loading or warming up. Wait before enabling correction."));
    }
    BeginHealth(*state);
    return TRUE;
  }
  if (message != WM_COMMAND) return FALSE;
  if (state->testing && LOWORD(w) != IDCANCEL) return TRUE;
  if (LOWORD(w) == IDC_MODEL_BROWSE && !state->Running()) {
    const auto folder = SelectModelFolder(dialog);
    if (!folder.empty()) { state->folder = folder; SetDlgItemTextW(dialog, IDC_MODEL_FOLDER, folder.c_str());
      if (ValidModelFolder(folder)) SaveModelFolder(folder); }
    return TRUE;
  }
  if (LOWORD(w) == IDC_MODEL_BROWSE_SHELL && !state->Running()) {
    const auto file = SelectModelFolder(dialog, false);
    if (!file.empty() && ValidPowerShell(file)) {
      state->powershell = file; SetDlgItemTextW(dialog, IDC_MODEL_SHELL, file.c_str());
      if (!SavePowerShell(file)) ShowState(dialog, *state, state->Text(
          L"当前选择可用，但未能保存供下次使用。", L"This selection is usable but could not be saved for next time."));
    }
    return TRUE;
  }
  if (LOWORD(w) == IDC_MODEL_STOP) { state->Stop(); ShowState(dialog, *state, state->Text(L"已停止本窗口启动的模型。", L"Stopped this window's model.")); return TRUE; }
  if (LOWORD(w) == IDC_MODEL_START && !state->Running()) {
    if (!ValidModelFolder(state->folder) || !ValidPowerShell(state->powershell)) {
      ShowState(dialog, *state, state->Text(L"请选择已有4B模型目录和 PowerShell 7（pwsh.exe）；本功能不会下载模型。",
          L"Select an existing 4B folder and PowerShell 7 (pwsh.exe); no downloads are performed.")); return TRUE;
    }
    if (!SaveModelFolder(state->folder) || !SavePowerShell(state->powershell.wstring())) {
      ShowState(dialog,*state,state->Text(L"无法保存模型位置，请检查账户权限。",L"Could not save model paths. Check account permissions.")); return TRUE;
    }
    if (!StartFrozenModel(*state)) {
      ShowState(dialog, *state, state->Text(L"无法启动；没有接管其他模型进程。", L"Could not start; no external model was taken over.")); return TRUE;
    }
    ShowState(dialog, *state, state->Text(L"正在启动冻结4B；原有服务若占用端口，脚本会退出。",
        L"Starting frozen 4B. If an existing service owns the port, the launcher will exit.")); return TRUE;
  }
  return FALSE;
} catch (...) { return FALSE; }
}  // namespace
int ShowLocalModelControl(HINSTANCE instance) {
  HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\" WEASEL_PIPE_NAME L"-LocalModelControl");
  if (!mutex) return -1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    if (!correction::SignalModelControl(false)) return -1;
    return 0;
  }
  Control state;
  const int result = static_cast<int>(DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_LOCAL_MODEL), nullptr, Proc, reinterpret_cast<LPARAM>(&state)));
  state.Stop();
  CloseHandle(mutex); return result;
}
bool CheckLocalModelControl(HINSTANCE instance, const std::function<void(HWND)>& preview) {
  Control state; state.testing = true;
  HWND dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_LOCAL_MODEL), nullptr, Proc, reinterpret_cast<LPARAM>(&state));
  if (!dialog) return false;
  bool valid = !IsWindowVisible(dialog) && state.folder.empty() && state.powershell.empty() && !state.launcher.Process() && !state.health;
  for (const auto id : {IDC_MODEL_FOLDER, IDC_MODEL_BROWSE, IDC_MODEL_START, IDC_MODEL_STOP, IDC_MODEL_STATE, IDC_MODEL_SHELL, IDC_MODEL_BROWSE_SHELL, IDC_MODEL_EXIT, IDCANCEL})
    valid = GetDlgItem(dialog, id) && valid;
  SendMessageW(dialog, WM_COMMAND, IDC_MODEL_START, 0); SendMessageW(dialog, WM_TIMER, 1, 0);
  valid = valid && !state.launcher.Process() && !state.health && !IsWindowEnabled(GetDlgItem(dialog, IDC_MODEL_STOP));
  if (preview) preview(dialog);
  DestroyWindow(dialog); return valid;
}
bool CheckOwnedLocalModel() try {
  std::ofstream report("ManagedModelChecks.log"); bool valid = true;
  auto check = [&](bool result, const char* label) { valid = valid && result; report << label << " = " << result << '\n'; report.flush(); return result; };
  Control state; state.folder = FindModelFolder();
  state.powershell = PreferredPowerShell();
  wchar_t testShell[32768]{};
  const auto shellSize = GetEnvironmentVariableW(L"WEASELC_MODEL_TEST_PWSH", testShell, _countof(testShell));
  if (shellSize && shellSize < _countof(testShell) && ValidPowerShell(testShell)) state.powershell = testShell;
  if (!check(!state.folder.empty() && !fs::exists(state.folder / L"ready.json") && !LocalHealth(),
      "controlled model test refuses an existing runtime or ready file")) return false;
  if (!check(ValidModelFolder(state.folder), "controlled model folder has the existing launcher runtime and 4B weights")) return false;
  if (!check(ValidPowerShell(state.powershell), "model controller accepts a verified selected PowerShell from either caller architecture")) return false;
  if (!check(StartFrozenModel(state), "same GUI launch path starts only frozen local 4B")) return false;
  const auto deadline = GetTickCount64() + 120000;
  bool ready = false;
  while (state.Running() && GetTickCount64() < deadline) {
    if (state.ReadyFile() && LocalHealth()) { ready = true; break; }
    Sleep(100);
  }
  check(ready, "owned model identity and loopback health both confirm readiness");
  HANDLE process = nullptr;
  if (ready) {
    std::ifstream file(state.folder / L"ready.json", std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    const auto pid = boost::json::value_to<DWORD>(boost::json::parse(text).at("processId"));
    if (state.launcher.ContainsProcess(pid)) process = OpenProcess(SYNCHRONIZE, FALSE, pid);
  }
  state.Stop();
  check(process && WaitForSingleObject(process, 5000) == WAIT_OBJECT_0, "GUI stop retires its own actual model process");
  if (process) CloseHandle(process);
  check(!fs::exists(state.folder / L"ready.json") && !LocalHealth(), "model stop leaves no ready file or healthy endpoint");
  state.Stop(); check(!state.launcher.Process(), "model stop remains safe when repeated");
  return valid;
} catch (...) { return false; }
