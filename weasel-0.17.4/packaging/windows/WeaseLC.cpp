#include <windows.h>
#include <msctf.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <LocalTestIdentity.h>
#include "UninstallGuard.h"
#include "DesktopLaunch.h"
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

static bool SeedFirstSchema(const std::filesystem::path& folder, DWORD hant) {
  const auto file = folder / L"default.custom.yaml";
  HANDLE output = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_EXISTS;
  // No customization map: the native first-run scheme/theme wizard still runs.
  // CREATE_NEW preserves every existing dictionary and configuration file.
  const std::string content = std::string("patch:\n  schema_list:\n    - schema: ") +
      (hant ? "luna_pinyin_tw\n" : "luna_pinyin_simp\n");
  DWORD written = 0;
  const bool ok = WriteFile(output, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
      written == content.size();
  CloseHandle(output);
  if (!ok) DeleteFileW(file.c_str());
  return ok;
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
      if (ok) ok = SeedFirstSchema(folder, ReadNumber(HKEY_CURRENT_USER, L"Hant", hant));
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
    if (remove) {
      IEnumTfLanguageProfiles* enumeration = nullptr;
      if (FAILED(profiles->EnumLanguageProfiles(language, &enumeration)) || !enumeration) { ok = false; continue; }
      TF_LANGUAGEPROFILE item{}; ULONG count = 0; bool present = false;
      while (enumeration->Next(1, &item, &count) == S_OK && count == 1) {
        if (IsEqualCLSID(item.clsid, clsid) && IsEqualGUID(item.guidProfile, profile)) { present = true; break; }
      }
      enumeration->Release();
      // Removing a nonexistent language can create an empty Windows language entry.
      if (!present) continue;
    }
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

static bool RegistrationAbsent() {
  for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
    for (const auto prefix : {L"Software\\Classes\\CLSID\\", L"Software\\Microsoft\\CTF\\TIP\\"}) {
      const std::wstring path = std::wstring(prefix) + WEASELC_PRODUCT_CLSID_TEXT;
      HKEY key = nullptr;
      const auto status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE | view, &key);
      if (key) RegCloseKey(key);
      if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND) return false;
    }
  }
  ITfCategoryMgr* categories = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&categories)))) return false;
  const GUID clsid = WEASELC_PRODUCT_CLSID;
  IEnumGUID* enumeration = nullptr;
  const HRESULT opened = categories->EnumCategoriesInItem(clsid, &enumeration);
  categories->Release();
  if (FAILED(opened) || !enumeration) return false;
  GUID category{}; ULONG count = 0;
  const HRESULT next = enumeration->Next(1, &category, &count);
  enumeration->Release();
  return next == S_FALSE && count == 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!args) return 2;
  const std::wstring option = count == 1 ? L"/configure" : count == 2 ? args[1] : L"invalid";
  LocalFree(args);
  if (option == L"/identity") return 0; // Read-only packaging smoke check.
  if (option == L"/launch-setup") {
    if (!distribution::InstalledRoot()) return 2;
    if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 3;
    const bool launched=distribution::LaunchFromDesktop((distribution::ModuleRoot()/L"WeaseLC.exe").wstring());
    CoUninitialize();return launched ? 0 : 1;
  }
  if (option == L"/guard-destination") {
    wchar_t destination[32768]{};
    const auto size=GetEnvironmentVariableW(L"WEASELC_INSTALL_ROOT",destination,_countof(destination));
    if(!size || size>=_countof(destination) || !distribution::SafeProgramRoot(destination)) return 1;
    if(distribution::PendingFileOperation(destination)) return 3;
    std::error_code error;
    if(std::filesystem::exists(destination,error)) {
      const bool empty=std::filesystem::is_empty(destination,error);
      return error ? 1 : empty ? 0 : 2;
    }
    return error ? 1 : 0;
  }
  if (option == L"/guard-selftest") {
    const auto root=distribution::DefaultProgramRoot();
    return distribution::SafeProgramRoot(root) && !distribution::SafeProgramRoot(root.root_path()) &&
        distribution::SafeProgramRoot(root.root_path()/L"WeaseLC-safe-boundary-test") &&
        !distribution::SafeProgramRoot(root.parent_path()) && !distribution::SafeProgramRoot(root/L"..") &&
        !distribution::SafeProgramRoot(L"C:\\Windows") && !distribution::InstalledRoot() ? 0 : 1;
  }
  if (option == L"/validate-install" || option == L"/prepare-uninstall") {
    if (!distribution::InstalledRoot()) return 2;
    if (option == L"/validate-install") return 0;
    const auto root=distribution::ModuleRoot();
    if(!Run((root/L"WeaselDeployer.exe").wstring(),L"/stop-local-model",true) ||
        !Run((root/L"WeaselServer.exe").wstring(),L"/q",true)) return 3;
    return distribution::ConfirmBlockers() ? 0 : 1;
  }
  if (option != L"/configure" && option != L"/start" && option != L"/enable" && option != L"/remove" && option != L"/unregistered") return 2;
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized)) return 3;
  if (option == L"/unregistered") {
    const bool absent = RegistrationAbsent();
    CoUninitialize(); return absent ? 0 : 1;
  }
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
