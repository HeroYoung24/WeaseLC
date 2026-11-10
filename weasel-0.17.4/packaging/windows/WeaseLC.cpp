#include <windows.h>
#include <msctf.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <LocalTestIdentity.h>
#ifndef WEASELC_DISTRIBUTION_BUILD
#error This launcher requires the distribution identity
#endif
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

static DWORD ReadNumber(HKEY hive, const wchar_t* name, DWORD fallback) {
  DWORD value = fallback, bytes = sizeof(value);
  return RegGetValueW(hive, WEASEL_PRODUCT_REG_KEY, name,
      RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value, &bytes) == ERROR_SUCCESS ? value : fallback;
}

static bool InitializePreferences() {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, WEASEL_PRODUCT_REG_KEY, 0, nullptr, 0,
      KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const DWORD hant = ReadNumber(HKEY_LOCAL_MACHINE, L"DefaultHant", 0);
  bool ok = true;
  DWORD bytes = 0;
  if (RegQueryValueExW(key, L"Hant", nullptr, nullptr, nullptr, &bytes) == ERROR_FILE_NOT_FOUND)
    ok = RegSetValueExW(key, L"Hant", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&hant), sizeof(hant)) == ERROR_SUCCESS;
  wchar_t savedFolder[32768]{};
  bytes = sizeof(savedFolder);
  const LSTATUS readFolder = RegGetValueW(key, nullptr, L"RimeUserDir",
      RRF_RT_REG_SZ, nullptr, savedFolder, &bytes);
  if (readFolder == ERROR_FILE_NOT_FOUND ||
      (readFolder == ERROR_SUCCESS && !wcscmp(savedFolder, L"%APPDATA%\\WeaseLC"))) {
    // RimeUserDir is a literal REG_SZ. Also migrate the first beta's token.
    const DWORD length = ExpandEnvironmentStringsW(L"%APPDATA%\\WeaseLC",
        savedFolder, _countof(savedFolder));
    if (!length || length > _countof(savedFolder)) ok = false;
    const std::wstring folder = savedFolder;
    if (ok) ok = RegSetValueExW(key, L"RimeUserDir", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(folder.c_str()), static_cast<DWORD>((folder.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
  } else if (readFolder != ERROR_SUCCESS) {
    ok = false;
  }
  if (ok) {
    // Reject relative paths and report directory errors before starting Rime.
    const std::filesystem::path folder(savedFolder);
    std::error_code error;
    if (!folder.is_absolute()) ok = false;
    else {
      std::filesystem::create_directories(folder, error);
      ok = !error && std::filesystem::is_directory(folder, error) && !error;
    }
  }
  RegCloseKey(key);
  return ok;
}

static bool SetProfile(bool remove) {
  const CLSID clsid = WEASELC_PRODUCT_CLSID;
  const GUID profile = WEASELC_PRODUCT_PROFILE;
  ITfInputProcessorProfiles* profiles = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)))) return false;
  const DWORD hant = ReadNumber(HKEY_CURRENT_USER, L"Hant",
      ReadNumber(HKEY_LOCAL_MACHINE, L"DefaultHant", 0));
  using Install = BOOL(WINAPI*)(LPCWSTR, DWORD);
  HMODULE input = LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto install = input ? reinterpret_cast<Install>(GetProcAddress(input, "InstallLayoutOrTip")) : nullptr;
  bool ok = install != nullptr;
  for (const LANGID language : {LANGID(0x0804), LANGID(0x0404)}) {
    if (!remove && language != (hant ? 0x0404 : 0x0804)) continue;
    const std::wstring entry = std::wstring(language == 0x0804 ? L"0804:" : L"0404:") +
        WEASELC_PRODUCT_CLSID_TEXT + WEASELC_PRODUCT_PROFILE_TEXT;
    if (install && !install(entry.c_str(), remove ? 1 : 0)) ok = false;
    const HRESULT changed = profiles->EnableLanguageProfile(clsid, language, profile, !remove);
    if (!remove && FAILED(changed)) ok = false;
    // Machine availability is set by RegisterProfile; this is only the user's
    // opt-in list, with no activation or default-profile flag.
  }
  if (input) FreeLibrary(input);
  profiles->Release();
  return ok;
}

static bool Run(const std::wstring& path, const wchar_t* arguments, bool wait) {
  const auto at = path.find_last_of(L'\\');
  const std::wstring directory = path.substr(0, at);
  std::wstring command = L"\"" + path + L"\" " + arguments;
  STARTUPINFOW start{sizeof(start)};
  start.dwFlags = STARTF_USESHOWWINDOW;
  start.wShowWindow = wait ? SW_SHOWNORMAL : SW_HIDE;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
      nullptr, directory.c_str(), &start, &process)) return false;
  CloseHandle(process.hThread);
  bool ok = true;
  if (wait) {
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    ok = GetExitCodeProcess(process.hProcess, &code) && code == 0;
  }
  CloseHandle(process.hProcess);
  return ok;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!args) return 2;
  const std::wstring option = count == 1 ? L"/configure" : count == 2 ? args[1] : L"invalid";
  LocalFree(args);
  if (option == L"/identity") return 0; // Read-only packaging smoke check.
  if (option != L"/configure" && option != L"/start" && option != L"/enable" && option != L"/remove") return 2;
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized)) return 3;
  bool ok = true;
  if (option == L"/remove") {
    SetProfile(true);
  } else {
    ok = InitializePreferences() && SetProfile(false);
    if (ok && option != L"/enable") {
      wchar_t module[32768]{};
      const auto length = GetModuleFileNameW(nullptr, module, _countof(module));
      ok = length && length < _countof(module);
      if (ok) {
        const std::wstring file(module);
        const auto root = file.substr(0, file.find_last_of(L'\\') + 1);
        ok = Run(root + L"WeaselDeployer.exe", option == L"/configure" ? L"/install" : L"/deploy", true);
        if (ok) ok = Run(root + L"WeaselServer.exe", L"", false);
      }
    }
  }
  CoUninitialize();
  if (!ok) MessageBoxW(nullptr,
      L"WeaseLC 未能完成设置。请检查安装是否完整，然后从开始菜单重试。\nWeaseLC setup could not finish. Check the installation and try again from the Start menu.",
      L"WeaseLC", MB_OK | MB_ICONERROR);
  return ok ? 0 : 1;
}
