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
  if (argc > 2 || (argc == 2 && !enable && !disable)) return 2;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
  int code = 0;
  {
    const CLSID clsid = WEASELC_TEST_CLSID;
    const GUID profile = WEASELC_TEST_PROFILE;
    CComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr = profiles.CoCreateInstance(CLSID_TF_InputProcessorProfiles);
    if (FAILED(hr)) return 4;
    if (enable || disable) {
      hr = profiles->EnableLanguageProfile(clsid, 0x0804, profile, enable);
      std::cout << "EnableLanguageProfile HRESULT=0x" << std::hex << hr << '\n';
      if (FAILED(hr)) code = 5;
    }
    BOOL enabled = FALSE;
    hr = profiles->IsEnabledLanguageProfile(clsid, 0x0804, profile, &enabled);
    std::cout << "IsEnabled HRESULT=0x" << std::hex << hr << " enabled=" << enabled << '\n';
    if (FAILED(hr) || (disable ? enabled : !enabled)) code = 6;
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
    else if (disable ? (value.dwFlags & TF_IPP_FLAG_ENABLED) : !(value.dwFlags & TF_IPP_FLAG_ENABLED)) code = 8;
    if ((enable || disable) && code == 0) {
      DWORD_PTR result = 0;
      const auto sent = SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
          reinterpret_cast<LPARAM>(L"intl"), SMTO_ABORTIFHUNG, 1000, &result);
      std::cout << "Settings notification sent=" << !!sent << '\n';
    }
  }
  CoUninitialize();
  return code;
}
