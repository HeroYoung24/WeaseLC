#pragma once

#include "stdafx.h"
#include "WeaselTSF.h"
#include "ServerResponse.h"

// RequestEditSession has two result channels. A failed request may not write
// the session output; TF_S_ASYNC means accepted for later execution only.
inline HRESULT RequestCompositionEditSession(ITfContext* context, TfClientId id,
    ITfEditSession* session, DWORD flags) {
  if (!context || !session) return E_INVALIDARG;
  HRESULT sessionResult = E_FAIL;
  const HRESULT requestResult = context->RequestEditSession(id, session, flags,
                                                            &sessionResult);
  return FAILED(requestResult) ? requestResult : sessionResult;
}

class CEditSession : public ITfEditSession {
 public:
  CEditSession(com_ptr<WeaselTSF> pTextService, com_ptr<ITfContext> pContext)
      : _pTextService(pTextService), _pContext(pContext) {
    _cRef = 1;
    _generation = _pTextService->_GetEditGeneration();
  }

  virtual ~CEditSession() {}

  /* IUnknown */
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == NULL)
      return E_INVALIDARG;

    *ppvObject = NULL;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
      *ppvObject = (ITfEditSession*)this;

    if (*ppvObject) {
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() { return ++_cRef; }

  STDMETHODIMP_(ULONG) Release() {
    LONG cr = --_cRef;
    assert(_cRef >= 0);
    if (_cRef == 0)
      delete this;
    return cr;
  }

  /* ITfEditSession */
  virtual STDMETHODIMP DoEditSession(TfEditCookie ec) = 0;

 protected:
  bool _IsCurrent() const {
    return _pTextService->_IsEditSessionCurrent(_generation);
  }
  com_ptr<WeaselTSF> _pTextService;
  com_ptr<ITfContext> _pContext;

 private:
  LONG _cRef; /* COM reference count */
  ULONGLONG _generation;
};

class CServerResponseEditSession : public CEditSession {
 public:
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  using Observer = HRESULT(WINAPI*)(ITfContext*, TfEditCookie);
  CServerResponseEditSession(com_ptr<WeaselTSF> service,
      com_ptr<ITfContext> context, Observer observer = nullptr)
      : CEditSession(service, context), _observer(observer) {}
#endif
  CServerResponseEditSession(com_ptr<WeaselTSF> service,
      com_ptr<ITfContext> context, std::shared_ptr<const ServerResponse> response)
      : CEditSession(service, context), _response(std::move(response)) {}
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    if (!_IsCurrent()) return TF_E_DISCONNECTED;
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    // Test-owned callback substitutes only the body, after the production gate.
    if (_observer) return _observer(_pContext, cookie);
#endif
    const HRESULT result = _response ? _pTextService->_ApplyServerResponse(cookie, _pContext, *_response) : E_UNEXPECTED;
    if (FAILED(result) && _IsCurrent()) _pTextService->_RetireComposition();
    return result;
  }
 private:
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  Observer _observer = nullptr;
#endif
  std::shared_ptr<const ServerResponse> _response;
};
