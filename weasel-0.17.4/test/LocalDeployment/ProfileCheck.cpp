#include <windows.h>
#include <msctf.h>
#include <atlbase.h>
#include <iostream>
#include <string>
#ifndef WEASELC_LOCAL_TEST_BUILD
#error This diagnostic is only for the independent local identity
#endif
#include <LocalTestIdentity.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
int wmain(int argc, wchar_t** argv) {
  const bool enable = argc == 2 && std::wstring(argv[1]) == L"--enable";
  const bool disable = argc == 2 && std::wstring(argv[1]) == L"--disable";
  const bool machineEnable = argc == 2 && std::wstring(argv[1]) == L"--enable-installed";
  const bool machineDisable = argc == 2 && std::wstring(argv[1]) == L"--disable-installed";
  if (argc > 2 || (argc == 2 && !enable && !disable && !machineEnable && !machineDisable)) return 2;
  if (machineEnable || machineDisable) {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    BOOL admin = FALSE;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) return 9;
    const bool checked = CheckTokenMembership(nullptr, admins, &admin) != FALSE;
    FreeSid(admins);
    if (!checked || !admin) return 9;
  }
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
  int code = 0;
  HRESULT mutationResult = S_OK;
  {
    const CLSID clsid = WEASELC_TEST_CLSID;
    const GUID profile = WEASELC_TEST_PROFILE;
    CComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr = profiles.CoCreateInstance(CLSID_TF_InputProcessorProfiles);
    if (FAILED(hr)) return 4;
    if (machineEnable || machineDisable) {
      // Availability of this single installed profile; never select it as the
      // user's default input method or activate a TIP in this process.
      hr = profiles->EnableLanguageProfileByDefault(clsid, 0x0804, profile, machineEnable);
      std::cout << "EnableLanguageProfileByDefault HRESULT=0x" << std::hex << hr << '\n';
      // The profile manager in this process can retain the pre-write state.
      // The caller verifies persistence using a separate read-only process.
      profiles.Release();
      CoUninitialize();
      return FAILED(hr) ? 10 : 0;
    }
    if (enable || disable) {
      hr = profiles->EnableLanguageProfile(clsid, 0x0804, profile, enable);
      mutationResult = hr;
      std::cout << "EnableLanguageProfile HRESULT=0x" << std::hex << hr << '\n';
      if (FAILED(hr)) code = 5;
    }
    BOOL enabled = FALSE;
    hr = profiles->IsEnabledLanguageProfile(clsid, 0x0804, profile, &enabled);
    std::cout << "IsEnabled HRESULT=0x" << std::hex << hr << " enabled=" << enabled << '\n';
    const bool expectDisabled = disable || machineDisable;
    if (FAILED(hr) || (expectDisabled ? enabled : !enabled)) code = 6;
    CComPtr<ITfInputProcessorProfileMgr> manager;
    hr = profiles.QueryInterface(&manager);
    std::cout << "Profile manager QI=0x" << std::hex << hr << '\n';
    if (manager) {
      CComPtr<IEnumTfInputProcessorProfiles> items;
      const auto enumerated = manager->EnumProfiles(0x0804, &items);
      std::cout << "EnumProfiles HRESULT=0x" << std::hex << enumerated << '\n';
      TF_INPUTPROCESSORPROFILE item{};
      ULONG fetched = 0;
      while (items && items->Next(1, &item, &fetched) == S_OK && fetched) {
        wchar_t id[40]{}; StringFromGUID2(item.clsid, id, 40);
        std::wcout << L"Profile " << id << L" flags=" << item.dwFlags << L" caps=" << item.dwCaps << L'\n';
      }
    }
    TF_INPUTPROCESSORPROFILE value{};
    if (SUCCEEDED(hr)) hr = manager->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, clsid, profile, nullptr, &value);
    std::cout << "GetProfile HRESULT=0x" << std::hex << hr << " flags=0x" << value.dwFlags << " caps=0x" << value.dwCaps << '\n';
    if (FAILED(hr)) code = 7;
    else if (expectDisabled ? (value.dwFlags & TF_IPP_FLAG_ENABLED) : !(value.dwFlags & TF_IPP_FLAG_ENABLED)) code = 8;
    if (enable || disable) {
      DWORD_PTR result = 0;
      const auto sent = SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
          reinterpret_cast<LPARAM>(L"intl"), SMTO_ABORTIFHUNG, 1000, &result);
      std::cout << "Settings notification sent=" << !!sent << '\n';
    }
  }
  CoUninitialize();
  // Write APIs can leave this process's profile enumeration stale. Report the
  // write result here; installation verifies visibility in a fresh process.
  return (enable || disable) ? (FAILED(mutationResult) ? 5 : 0) : code;
}
