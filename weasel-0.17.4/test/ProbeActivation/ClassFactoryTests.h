#pragma once

static bool TestClassFactory(HMODULE module, REFCLSID service) {
  using GetFactory = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
  auto get = reinterpret_cast<GetFactory>(GetProcAddress(module, "DllGetClassObject"));
  auto canUnload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(module, "DllCanUnloadNow"));
  if (!get || !canUnload) return false;
  unsigned checked = 0, failures = 0;
  const auto check = [&](const WCHAR* name, bool valid) {
    ++checked;
    if (!valid) ++failures;
    std::wcout << L"Class factory " << name << L" = " << valid << L'\n';
  };
  int placeholder = 0;
  const auto reject = [&](REFCLSID clsid, REFIID iid, HRESULT expected) {
    void* object = &placeholder;
    const HRESULT result = get(clsid, iid, &object);
    const bool valid = result == expected && !object;
    if (SUCCEEDED(result) && object && object != &placeholder)
      static_cast<IUnknown*>(object)->Release();
    return valid;
  };
  CLSID other = service;
  other.Data1 ^= 0x01010101;
  check(L"reject wrong CLSID", reject(other, IID_IClassFactory, CLASS_E_CLASSNOTAVAILABLE));
  check(L"reject unsupported factory IID", reject(service, IID_IStream, E_NOINTERFACE));
  // Old DLLs fail these harmless gates. Do not pass null pointers to a DLL
  // already known to violate its class/interface contract.
  if (failures) {
    std::wcout << L"Class factory null-pointer checks skipped after contract failure.\n";
    return false;
  }
  check(L"reject null export output", get(service, IID_IClassFactory, nullptr) == E_INVALIDARG);

  LONG createdBefore = 0, destroyedBefore = 0, activatedBefore = 0, deactivatedBefore = 0;
  auto counts = reinterpret_cast<void(WINAPI*)(LONG*, LONG*, LONG*, LONG*)>(
      GetProcAddress(module, "WeaselProbeGetLifecycleCounts"));
  if (counts) counts(&createdBefore, &destroyedBefore, &activatedBefore, &deactivatedBefore);
  {
    ComPtr<IClassFactory> factory;
    const HRESULT factoryHr = get(service, IID_IClassFactory,
        reinterpret_cast<void**>(factory.GetAddressOf()));
    check(L"create correct factory", factoryHr == S_OK && factory);
    if (factoryHr != S_OK || !factory) return false;
    check(L"factory keeps DLL alive", canUnload() == S_FALSE);
    check(L"reject null factory QI output", factory->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    void* object = &placeholder;
    const HRESULT queryHr = factory->QueryInterface(IID_IStream, &object);
    check(L"unsupported factory QI clears output", queryHr == E_NOINTERFACE && !object);
    if (SUCCEEDED(queryHr) && object && object != &placeholder)
      static_cast<IUnknown*>(object)->Release();
    const ULONG added = factory->AddRef();
    const ULONG remaining = factory->Release();
    check(L"factory owns its reference count", added == 2 && remaining == 1);
    {
      ComPtr<IUnknown> first, second;
      ComPtr<IClassFactory> queried;
      const bool identity = factory.As(&first) == S_OK && first.As(&queried) == S_OK &&
          queried.As(&second) == S_OK && first.Get() == second.Get();
      check(L"factory canonical IUnknown", identity);
    }
    {
      ComPtr<IUnknown> unknown;
      ComPtr<IClassFactory> queried;
      const HRESULT unknownHr = get(service, IID_IUnknown,
          reinterpret_cast<void**>(unknown.GetAddressOf()));
      check(L"get factory through IUnknown", unknownHr == S_OK && unknown && unknown.As(&queried) == S_OK);
    }
    check(L"reject null instance output",
        factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx, nullptr) == E_INVALIDARG);
    object = &placeholder;
    const HRESULT aggregationHr = factory->CreateInstance(factory.Get(), IID_IUnknown, &object);
    check(L"reject aggregation and clear output", aggregationHr == CLASS_E_NOAGGREGATION && !object);
    if (SUCCEEDED(aggregationHr) && object && object != &placeholder)
      static_cast<IUnknown*>(object)->Release();
    object = &placeholder;
    const HRESULT unsupportedHr = factory->CreateInstance(nullptr, IID_IStream, &object);
    check(L"unsupported instance IID clears output", unsupportedHr == E_NOINTERFACE && !object);
    if (SUCCEEDED(unsupportedHr) && object && object != &placeholder)
      static_cast<IUnknown*>(object)->Release();
    ComPtr<ITfTextInputProcessorEx> processor;
    const HRESULT instanceHr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
        reinterpret_cast<void**>(processor.GetAddressOf()));
    check(L"create correct service", instanceHr == S_OK && processor);
    if (instanceHr != S_OK || !processor) return false;
    check(L"reject null service QI output", processor->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    object = &placeholder;
    const HRESULT serviceQueryHr = processor->QueryInterface(IID_IStream, &object);
    check(L"unsupported service QI clears output", serviceQueryHr == E_NOINTERFACE && !object);
    if (SUCCEEDED(serviceQueryHr) && object && object != &placeholder)
      static_cast<IUnknown*>(object)->Release();
    {
      ComPtr<IUnknown> first, second;
      ComPtr<ITfTextInputProcessor> basic;
      const bool identity = processor.As(&first) == S_OK && processor.As(&basic) == S_OK &&
          basic.As(&second) == S_OK && first.Get() == second.Get();
      check(L"service canonical IUnknown", identity);
    }
    factory.Reset();
    check(L"service survives factory release", canUnload() == S_FALSE);
    processor.Reset();
    check(L"objects released DLL can unload", canUnload() == S_OK);
  }
  {
    ComPtr<IClassFactory> first, second;
    const HRESULT firstHr = get(service, IID_IClassFactory, reinterpret_cast<void**>(first.GetAddressOf()));
    const HRESULT secondHr = get(service, IID_IClassFactory, reinterpret_cast<void**>(second.GetAddressOf()));
    check(L"multiple factories retrieved", firstHr == S_OK && first && secondHr == S_OK && second);
    first.Reset();
    check(L"remaining factory keeps DLL alive", second && canUnload() == S_FALSE);
    second.Reset();
    check(L"all factories released DLL can unload", canUnload() == S_OK);
  }
  {
    ComPtr<IClassFactory> factory;
    const HRESULT factoryHr = get(service, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf()));
    if (factoryHr != S_OK || !factory) return false;
    const HRESULT locked = factory->LockServer(TRUE);
    check(L"lock server", locked == S_OK);
    factory.Reset();
    check(L"server lock survives factory release", locked == S_OK && canUnload() == S_FALSE);
    const HRESULT unlockFactory = get(service, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf()));
    if (unlockFactory != S_OK || !factory) return false;
    check(L"unlock server", factory->LockServer(FALSE) == S_OK);
    factory.Reset();
    check(L"server unlocked DLL can unload", canUnload() == S_OK);
  }
  if (counts) {
    LONG created = 0, destroyed = 0, activated = 0, deactivated = 0;
    counts(&created, &destroyed, &activated, &deactivated);
    check(L"service lifecycle balanced without activation",
        created - createdBefore == 2 && destroyed - destroyedBefore == 2 &&
        activated == activatedBefore && deactivated == deactivatedBefore);
    std::wcout << L"Class factory synthetic services created/destroyed = "
               << created - createdBefore << L"/" << destroyed - destroyedBefore << L'\n';
  }
  std::wcout << L"Class factory checks = " << checked << L", failures = " << failures << L'\n';
  return !failures;
}

static int RunClassFactoryChecks(const WCHAR* dll, REFCLSID service) {
  HMODULE module = LoadLibraryW(dll);
  if (!module) {
    std::wcout << L"Class factory LoadLibrary error = " << GetLastError() << L'\n';
    return 1;
  }
  bool valid = TestClassFactory(module, service);
  auto canUnload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(module, "DllCanUnloadNow"));
  if (canUnload && canUnload() == S_OK && FreeLibrary(module)) {
    HMODULE resident = nullptr;
    const bool stillLoaded = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(module), &resident) != FALSE;
    const DWORD lookupError = stillLoaded ? ERROR_SUCCESS : GetLastError();
    const bool unloaded = !stillLoaded && lookupError == ERROR_MOD_NOT_FOUND;
    std::wcout << L"Class factory DLL actually unloaded = " << unloaded
               << L", module lookup error = " << lookupError << L'\n';
    valid = unloaded && valid;
  } else {
    std::wcout << L"Class factory DLL unload refused or failed.\n";
    valid = false;
  }
  return valid ? 0 : 1;
}
