#pragma once

// Used only after the category host's preflight has verified this probe is
// uninstalled. Keep the registry profile alive until the fresh child exits.
class TemporaryHiddenProfile final {
 public:
  TemporaryHiddenProfile() = default;
  TemporaryHiddenProfile(const TemporaryHiddenProfile&) = delete;
  TemporaryHiddenProfile& operator=(const TemporaryHiddenProfile&) = delete;
  ~TemporaryHiddenProfile() { if (attempted_) Close(); }

  HRESULT Register(REFCLSID service, const std::wstring& dll) {
    const CLSID probe = {0xb749b45e, 0x7dce, 0x4496,
        {0x84, 0x14, 0x9e, 0xe1, 0x09, 0x70, 0x26, 0x0a}};
    if (!IsEqualCLSID(service, probe) || attempted_) return E_INVALIDARG;
    service_ = service;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(manager_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    const WCHAR description[] = L"Weasel Probe Process Test";
    attempted_ = true;
    hr = manager_->RegisterProfile(service_, 0x0804, profile_, description,
        _countof(description) - 1, dll.c_str(), static_cast<ULONG>(dll.size()),
        0, nullptr, 0, TRUE, TF_RP_HIDDENINSETTINGUI);
    PrintResult(L"Host RegisterProfile(HIDDEN, temporary registry profile)", hr);
    if (hr == S_OK) {
      // S_OK alone does not prove that another process can read a registry
      // profile. Report only metadata belonging to this synthetic identity.
      const WCHAR* keyPath = L"Software\\Microsoft\\CTF\\TIP\\"
          L"{B749B45E-7DCE-4496-8414-9EE10970260A}\\LanguageProfile\\"
          L"0x00000804\\{A2F702D2-DD84-4C9E-9C18-66FEBA75C4E3}";
      for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
          HKEY key = nullptr;
          const LSTATUS status = RegOpenKeyExW(hive, keyPath, 0, KEY_READ | view, &key);
          std::wcout << L"Hidden profile registry metadata: hive = "
                     << (hive == HKEY_LOCAL_MACHINE ? L"HKLM" : L"HKCU")
                     << L", view = " << (view == KEY_WOW64_32KEY ? 32 : 64)
                     << L", status = " << status << L'\n';
          if (key) RegCloseKey(key);
        }
      }
      TF_INPUTPROCESSORPROFILE registered = {};
      const HRESULT readHr = manager_->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,
          0x0804, service_, profile_, nullptr, &registered);
      PrintResult(L"Host read profile after registration", readHr);
    }
    return hr;
  }

  bool Close() {
    if (!attempted_) return true;
    attempted_ = false;
    const HRESULT hr = manager_->UnregisterProfile(service_, 0x0804, profile_, 0);
    PrintResult(L"Host UnregisterProfile(temporary registry profile)", hr);
    return hr == S_OK;
  }
 private:
  ComPtr<ITfInputProcessorProfileMgr> manager_;
  CLSID service_ = CLSID_NULL;
  const GUID profile_ = {0xa2f702d2, 0xdd84, 0x4c9e,
      {0x9c, 0x18, 0x66, 0xfe, 0xba, 0x75, 0xc4, 0xe3}};
  bool attempted_ = false;
};
