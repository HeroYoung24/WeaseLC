#pragma once

// RegisterCategory has no LOCALPROCESS flag. This opt-in diagnostic writes only
// the uninstalled probe's category, then removes it. Always run through the
// PowerShell wrapper, which also cleans up after a child process crash.
class TemporaryKeyboardCategory final {
 public:
  TemporaryKeyboardCategory() = default;
  TemporaryKeyboardCategory(const TemporaryKeyboardCategory&) = delete;
  TemporaryKeyboardCategory& operator=(const TemporaryKeyboardCategory&) = delete;
  ~TemporaryKeyboardCategory() { if (attempted_) Close(); }

  HRESULT Register(REFCLSID service, bool registerTipType = false) {
    const CLSID probe = {0xb749b45e, 0x7dce, 0x4496,
        {0x84, 0x14, 0x9e, 0xe1, 0x09, 0x70, 0x26, 0x0a}};
    if (!IsEqualCLSID(service, probe) || attempted_) return E_INVALIDARG;
    service_ = service;
    // Refuse an installed or leftover probe. Never overwrite existing state.
    const WCHAR* roots[] = {
        L"Software\\Microsoft\\CTF\\TIP\\{B749B45E-7DCE-4496-8414-9EE10970260A}",
        L"Software\\Classes\\CLSID\\{B749B45E-7DCE-4496-8414-9EE10970260A}"};
    for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
      for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
        for (const WCHAR* root : roots) {
          HKEY key = nullptr;
          const LSTATUS result = RegOpenKeyExW(hive, root, 0, KEY_READ | view, &key);
          if (key) RegCloseKey(key);
          if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
            return HRESULT_FROM_WIN32(result == ERROR_SUCCESS ? ERROR_ALREADY_EXISTS : result);
        }
      }
    }
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
        reinterpret_cast<void**>(manager_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    ComPtr<IEnumGUID> categories;
    hr = manager_->EnumCategoriesInItem(service_, &categories);
    if (FAILED(hr) || !categories) return FAILED(hr) ? hr : E_FAIL;
    GUID category = GUID_NULL;
    ULONG fetched = 0;
    hr = categories->Next(1, &category, &fetched);
    if (hr != S_FALSE || fetched) return FAILED(hr) ? hr : E_UNEXPECTED;
    attempted_ = true;  // Also undo a partially failed registration.
    if (registerTipType) {
      typeAttempted_ = true;
      hr = manager_->RegisterCategory(service_, GUID_TFCAT_CATEGORY_OF_TIP, service_);
      PrintResult(L"Temporary RegisterCategory(CATEGORY_OF_TIP)", hr);
      if (hr != S_OK) return hr;
    }
    keyboardAttempted_ = true;
    hr = manager_->RegisterCategory(service_, GUID_TFCAT_TIP_KEYBOARD, service_);
    PrintResult(L"Temporary RegisterCategory(KEYBOARD)", hr);
    if (hr == S_OK) {
      GUID closest = GUID_NULL;
      const GUID* choices[] = {&GUID_TFCAT_TIP_KEYBOARD};
      const HRESULT closestHr = manager_->FindClosestCategory(
          service_, &closest, choices, _countof(choices));
      PrintResult(L"Host FindClosestCategory(KEYBOARD)", closestHr);
      std::wcout << L"Host sees keyboard category = "
                 << (closestHr == S_OK && IsEqualGUID(closest, GUID_TFCAT_TIP_KEYBOARD)) << L'\n';
    }
    return hr;
  }

  bool Close() {
    if (!attempted_) return true;
    attempted_ = false;
    bool removed = true;
    if (keyboardAttempted_) {
      const HRESULT hr = manager_->UnregisterCategory(service_, GUID_TFCAT_TIP_KEYBOARD, service_);
      PrintResult(L"Temporary UnregisterCategory(KEYBOARD)", hr);
      removed = removed && hr == S_OK;
    }
    if (typeAttempted_) {
      const HRESULT hr = manager_->UnregisterCategory(service_, GUID_TFCAT_CATEGORY_OF_TIP, service_);
      PrintResult(L"Temporary UnregisterCategory(CATEGORY_OF_TIP)", hr);
      removed = removed && hr == S_OK;
    }
    ComPtr<IEnumGUID> categories;
    HRESULT enumeration = manager_->EnumCategoriesInItem(service_, &categories);
    GUID category = GUID_NULL;
    ULONG fetched = 0;
    if (SUCCEEDED(enumeration) && categories)
      enumeration = categories->Next(1, &category, &fetched);
    const bool cleared = removed && categories && enumeration == S_FALSE && !fetched;
    std::wcout << L"Temporary keyboard category cleared = " << cleared << L'\n';
    return cleared;
  }
 private:
  ComPtr<ITfCategoryMgr> manager_;
  CLSID service_ = CLSID_NULL;
  bool attempted_ = false;
  bool typeAttempted_ = false, keyboardAttempted_ = false;
};
