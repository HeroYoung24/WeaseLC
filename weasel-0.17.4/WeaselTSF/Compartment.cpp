#include "stdafx.h"
#include "WeaselTSF.h"
#include "Compartment.h"
#include <resource.h>
#include <functional>
#include "ResponseParser.h"
#include "CandidateList.h"
#include "LanguageBar.h"

STDAPI CCompartmentEventSink::QueryInterface(REFIID riid,
                                             _Outptr_ void** ppvObj) {
  if (ppvObj == nullptr)
    return E_INVALIDARG;

  *ppvObj = nullptr;

  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
    *ppvObj = (CCompartmentEventSink*)this;
  }

  if (*ppvObj) {
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

STDAPI_(ULONG) CCompartmentEventSink::AddRef() {
  return ++_refCount;
}

STDAPI_(ULONG) CCompartmentEventSink::Release() {
  LONG cr = --_refCount;

  assert(_refCount >= 0);

  if (_refCount == 0) {
    delete this;
  }

  return cr;
}

STDAPI CCompartmentEventSink::OnChange(_In_ REFGUID guidCompartment) {
  return _callback(guidCompartment);
}

HRESULT CCompartmentEventSink::_Advise(_In_ com_ptr<IUnknown> punk,
                                       _In_ REFGUID guidCompartment) {
  if (!punk)
    return E_INVALIDARG;
  if (_compartment)
    return E_UNEXPECTED;
  HRESULT hr = S_OK;
  ITfCompartmentMgr* pCompartmentMgr = nullptr;
  ITfSource* pSource = nullptr;

  hr = punk->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompartmentMgr);
  if (FAILED(hr)) {
    return hr;
  }

  hr = pCompartmentMgr->GetCompartment(guidCompartment, &_compartment);
  if (SUCCEEDED(hr)) {
    hr = _compartment->QueryInterface(IID_ITfSource, (void**)&pSource);
    if (SUCCEEDED(hr)) {
      hr = pSource->AdviseSink(IID_ITfCompartmentEventSink, this, &_cookie);
      pSource->Release();
    }
  }

  pCompartmentMgr->Release();

  if (FAILED(hr)) {
    _cookie = TF_INVALID_COOKIE;
    _compartment = nullptr;
  }

  return hr;
}
HRESULT CCompartmentEventSink::_Unadvise() {
  if (!_compartment || _cookie == TF_INVALID_COOKIE) {
    _compartment = nullptr;
    _cookie = TF_INVALID_COOKIE;
    return S_OK;
  }
  HRESULT hr = S_OK;
  ITfSource* pSource = nullptr;

  hr = _compartment->QueryInterface(IID_ITfSource, (void**)&pSource);
  if (SUCCEEDED(hr)) {
    hr = pSource->UnadviseSink(_cookie);
    pSource->Release();
  }

  _compartment = nullptr;
  _cookie = TF_INVALID_COOKIE;

  return hr;
}

BOOL WeaselTSF::_IsKeyboardDisabled() {
  ITfCompartmentMgr* pCompMgr = NULL;
  ITfDocumentMgr* pDocMgrFocus = NULL;
  ITfContext* pContext = NULL;
  BOOL fDisabled = FALSE;

  if ((_pThreadMgr->GetFocus(&pDocMgrFocus) != S_OK) ||
      (pDocMgrFocus == NULL)) {
    fDisabled = TRUE;
    goto Exit;
  }

  if ((pDocMgrFocus->GetTop(&pContext) != S_OK) || (pContext == NULL)) {
    fDisabled = TRUE;
    goto Exit;
  }

  if (pContext->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompMgr) ==
      S_OK) {
    ITfCompartment* pCompartmentDisabled;
    ITfCompartment* pCompartmentEmptyContext;

    /* Check GUID_COMPARTMENT_KEYBOARD_DISABLED */
    if (pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED,
                                 &pCompartmentDisabled) == S_OK) {
      VARIANT var;
      if (pCompartmentDisabled->GetValue(&var) == S_OK) {
        if (var.vt == VT_I4)  // Even VT_EMPTY, GetValue() can succeed
          fDisabled = (BOOL)var.lVal;
      }
      pCompartmentDisabled->Release();
    }

    /* Check GUID_COMPARTMENT_EMPTYCONTEXT */
    if (pCompMgr->GetCompartment(GUID_COMPARTMENT_EMPTYCONTEXT,
                                 &pCompartmentEmptyContext) == S_OK) {
      VARIANT var;
      if (pCompartmentEmptyContext->GetValue(&var) == S_OK) {
        if (var.vt == VT_I4)  // Even VT_EMPTY, GetValue() can succeed
          fDisabled = (BOOL)var.lVal;
      }
      pCompartmentEmptyContext->Release();
    }
    pCompMgr->Release();
  }

Exit:
  if (pContext)
    pContext->Release();
  if (pDocMgrFocus)
    pDocMgrFocus->Release();
  return fDisabled;
}

BOOL WeaselTSF::_IsKeyboardOpen() {
  com_ptr<ITfCompartmentMgr> pCompMgr;
  BOOL fOpen = FALSE;

  if (_pThreadMgr->QueryInterface(&pCompMgr) == S_OK) {
    com_ptr<ITfCompartment> pCompartment;
    if (pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                 &pCompartment) == S_OK) {
      VARIANT var;
      if (pCompartment->GetValue(&var) == S_OK) {
        if (var.vt == VT_I4)  // Even VT_EMPTY, GetValue() can succeed
          fOpen = (BOOL)var.lVal;
      }
    }
  }
  return fOpen;
}

HRESULT WeaselTSF::_SetKeyboardOpen(BOOL fOpen) {
  HRESULT hr = E_FAIL;
  com_ptr<ITfCompartmentMgr> pCompMgr;

  if (_pThreadMgr->QueryInterface(&pCompMgr) == S_OK) {
    com_ptr<ITfCompartment> pCompartment;
    if (pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                 &pCompartment) == S_OK) {
      VARIANT var;
      VariantInit(&var);
      var.vt = VT_I4;
      var.lVal = fOpen;
      hr = pCompartment->SetValue(_tfClientId, &var);
    }
  }

  return hr;
}

HRESULT WeaselTSF::_GetCompartmentDWORD(DWORD& value, const GUID guid) {
  value = 0;
  com_ptr<ITfCompartmentMgr> pComMgr;
  HRESULT hr = _pThreadMgr->QueryInterface(&pComMgr);
  if (FAILED(hr))
    return hr;
  com_ptr<ITfCompartment> pCompartment;
  hr = pComMgr->GetCompartment(guid, &pCompartment);
  if (FAILED(hr))
    return hr;
  VARIANT var;
  VariantInit(&var);
  hr = pCompartment->GetValue(&var);
  if (SUCCEEDED(hr)) {
    if (var.vt == VT_I4)
      value = var.lVal;
    else
      hr = S_FALSE;
  }
  VariantClear(&var);
  return hr;
}

HRESULT WeaselTSF::_SetCompartmentDWORD(const DWORD& value, const GUID guid) {
  com_ptr<ITfCompartmentMgr> pComMgr;
  HRESULT hr = _pThreadMgr->QueryInterface(&pComMgr);
  if (FAILED(hr))
    return hr;
  com_ptr<ITfCompartment> pCompartment;
  hr = pComMgr->GetCompartment(guid, &pCompartment);
  if (FAILED(hr))
    return hr;
  VARIANT var;
  VariantInit(&var);
  var.vt = VT_I4;
  var.lVal = value;
  return pCompartment->SetValue(_tfClientId, &var);
}

BOOL WeaselTSF::_InitCompartment() {
  using namespace std::placeholders;

  auto callback = std::bind(&WeaselTSF::_HandleCompartment, this, _1);
  _pKeyboardCompartmentSink.Attach(new CCompartmentEventSink(callback));
  if (!_pKeyboardCompartmentSink)
    return FALSE;
  HRESULT hr = _pKeyboardCompartmentSink->_Advise(
      (IUnknown*)_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(hr);
#endif
  if (FAILED(hr))
    return FALSE;

  _pConvertionCompartmentSink.Attach(new CCompartmentEventSink(callback));
  if (!_pConvertionCompartmentSink)
    return FALSE;
  hr = _pConvertionCompartmentSink->_Advise(
      (IUnknown*)_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeActivationResult(hr);
#endif
  return SUCCEEDED(hr);
}

void WeaselTSF::_UninitCompartment() {
  if (_pKeyboardCompartmentSink) {
    _pKeyboardCompartmentSink->_Unadvise();
    _pKeyboardCompartmentSink = NULL;
  }
  if (_pConvertionCompartmentSink) {
    _pConvertionCompartmentSink->_Unadvise();
    _pConvertionCompartmentSink = NULL;
  }
}

HRESULT WeaselTSF::_HandleCompartment(REFGUID guidCompartment) {
  if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
    if (_isToOpenClose) {
      BOOL isOpen = _IsKeyboardOpen();
      // clear composition when close keyboard
      if (!isOpen && _pEditSessionContext) {
        m_client.ClearComposition();
        _EndComposition(_pEditSessionContext, true);
      }
      _EnableLanguageBar(isOpen);
      _UpdateLanguageBar(_status);
    } else {
      _status.ascii_mode = !_status.ascii_mode;
      _SetKeyboardOpen(true);
      if (_pLangBarButton && _pLangBarButton->IsLangBarDisabled())
        _EnableLanguageBar(true);
      _HandleLangBarMenuSelect(_status.ascii_mode
                                   ? ID_WEASELTRAY_ENABLE_ASCII
                                   : ID_WEASELTRAY_DISABLE_ASCII);
      if (_pEditSessionContext)
        m_client.ClearComposition();
      _UpdateLanguageBar(_status);
    }
  } else if (IsEqualGUID(guidCompartment,
                         GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
    BOOL isOpen = _IsKeyboardOpen();
    if (isOpen) {
      weasel::ResponseParser parser(NULL, NULL, &_status, NULL,
                                    &_cand->style());
      bool ok = m_client.GetResponseData(std::ref(parser));
      _UpdateLanguageBar(_status);
    }
  }
  return S_OK;
}
