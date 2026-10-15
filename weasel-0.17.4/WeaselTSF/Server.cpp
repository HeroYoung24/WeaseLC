#include "stdafx.h"

#include "Globals.h"
#include "Register.h"
#include "RegistrationWorkflow.h"
#include <strsafe.h>
#include "WeaselTSF.h"
#include <VersionHelpers.hpp>
#include <new>

void DllAddRef() {
  InterlockedIncrement(&g_cRefDll);
}

void DllRelease() {
  InterlockedDecrement(&g_cRefDll);
}

class CClassFactory : public IClassFactory {
 public:
  CClassFactory() { DllAddRef(); }
  // IUnknown methods
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  // IClassFactory methods
  STDMETHODIMP CreateInstance(IUnknown* pUnkOuter,
                              REFIID riid,
                              void** ppvObject);
  STDMETHODIMP LockServer(BOOL fLock);

 private:
  ~CClassFactory() { DllRelease(); }
  LONG references_ = 1;
};

STDAPI CClassFactory::QueryInterface(REFIID riid, void** ppvObject) {
  if (!ppvObject) return E_POINTER;
  *ppvObject = nullptr;
  if (IsEqualIID(riid, IID_IClassFactory) || IsEqualIID(riid, IID_IUnknown)) {
    *ppvObject = static_cast<IClassFactory*>(this);
    AddRef();
    return NOERROR;
  }
  *ppvObject = NULL;
  return E_NOINTERFACE;
}

STDAPI_(ULONG) CClassFactory::AddRef() {
  return InterlockedIncrement(&references_);
}

STDAPI_(ULONG) CClassFactory::Release() {
  const LONG remaining = InterlockedDecrement(&references_);
  if (!remaining) delete this;
  return remaining;
}

STDAPI CClassFactory::CreateInstance(IUnknown* pUnkOuter,
                                     REFIID riid,
                                     void** ppvObject) {
  WeaselTSF* pCase;
  HRESULT hr;
  if (ppvObject == NULL)
    return E_INVALIDARG;
  *ppvObject = NULL;
  if (pUnkOuter != NULL)
    return CLASS_E_NOAGGREGATION;
  try {
    pCase = new (std::nothrow) WeaselTSF();
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  }
  if (!pCase) return E_OUTOFMEMORY;
  hr = pCase->QueryInterface(riid, ppvObject);
  pCase->Release();  // caller still holds ref if hr == S_OK
  return hr;
}

STDAPI CClassFactory::LockServer(BOOL fLock) {
  if (fLock)
    DllAddRef();
  else
    DllRelease();
  return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObject) {
  if (!ppvObject) return E_INVALIDARG;
  *ppvObject = nullptr;
  if (!IsEqualCLSID(rclsid, c_clsidTextService)) return CLASS_E_CLASSNOTAVAILABLE;
  CClassFactory* factory = new (std::nothrow) CClassFactory();
  if (!factory) return E_OUTOFMEMORY;
  const HRESULT result = factory->QueryInterface(riid, ppvObject);
  factory->Release();
  return result;
}

STDAPI DllCanUnloadNow() {
  if (g_cRefDll >= 0)
    return S_FALSE;
  return S_OK;
}

STDAPI DllRegisterServer() {
  HRESULT (*const steps[])() = {RegisterServer, RegisterProfiles, RegisterCategories};
  const auto registration = ApplyRegistration(_countof(steps),
      [&](unsigned index) { return steps[index](); },
      [] { return DllUnregisterServer(); });
  if (FAILED(registration.result)) {
    WCHAR message[160] = {};
    StringCchPrintfW(message, _countof(message),
        L"Weasel TSF registration step %u failed: 0x%08lX; rollback: 0x%08lX\n",
        registration.failedStep, static_cast<unsigned long>(registration.result),
        static_cast<unsigned long>(registration.rollback));
    OutputDebugStringW(message);
  }
  return registration.result;
}

STDAPI DllUnregisterServer() {
  HRESULT (*const steps[])() = {UnregisterProfiles, UnregisterCategories, UnregisterServer};
  return RemoveRegistrationItems(_countof(steps),
      [&](unsigned index) { return steps[index](); });
}
