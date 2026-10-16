#include "stdafx.h"
#include "WeaselTSF.h"
#include "EditSession.h"
#include "ResponseParser.h"
#include "CandidateList.h"

/* Start Composition */
class CStartCompositionEditSession : public CEditSession {
 public:
  CStartCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                               com_ptr<ITfContext> pContext,
                               BOOL fCUASWorkaroundEnabled,
                               BOOL inlinePreeditEnabled)
      : CEditSession(pTextService, pContext),
        _inlinePreeditEnabled(inlinePreeditEnabled) {
    _fCUASWorkaroundEnabled = fCUASWorkaroundEnabled;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  BOOL _fCUASWorkaroundEnabled;
  BOOL _inlinePreeditEnabled;
};

STDAPI CStartCompositionEditSession::DoEditSession(TfEditCookie ec) {
  if (!_IsCurrent()) return TF_E_DISCONNECTED;
  HRESULT hr;
  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  com_ptr<ITfRange> pRangeComposition;
  hr = _pContext->QueryInterface(IID_ITfInsertAtSelection,
                                (LPVOID*)&pInsertAtSelection);
  if (FAILED(hr)) return hr;
  if (!pInsertAtSelection) return E_FAIL;
  hr = pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0,
                                                &pRangeComposition);
  if (FAILED(hr)) return hr;
  if (!pRangeComposition) return E_FAIL;

  com_ptr<ITfContextComposition> pContextComposition;
  com_ptr<ITfComposition> pComposition;
  hr = _pContext->QueryInterface(IID_ITfContextComposition,
                                (LPVOID*)&pContextComposition);
  if (FAILED(hr)) return hr;
  if (!pContextComposition) return E_FAIL;
  hr = pContextComposition->StartComposition(
      ec, pRangeComposition, _pTextService, &pComposition);
  if (FAILED(hr)) return hr;
  // TSF permits S_OK with a null composition when the context owner rejects it.
  if (!pComposition) return E_FAIL;
  if (!_IsCurrent()) {
    pComposition->EndComposition(ec);
    return TF_E_DISCONNECTED;
  }
  {
    _pTextService->_SetComposition(pComposition, _pContext);

    /* WORKAROUND:
     *   CUAS does not provide a correct GetTextExt() position unless the
     * composition is filled with characters. So we insert a zero width space
     * here. The workaround is only needed when inline preedit is not enabled.
     *   See https://github.com/rime/weasel/pull/883#issuecomment-1567625762
     */
    if (!_inlinePreeditEnabled) {
      hr = pRangeComposition->SetText(ec, TF_ST_CORRECTION, L" ", 1);
      if (FAILED(hr)) return hr;
    }

    /* set selection */
    TF_SELECTION tfSelection = {};
    hr = pRangeComposition->Collapse(
        ec, _inlinePreeditEnabled ? TF_ANCHOR_END : TF_ANCHOR_START);
    if (FAILED(hr)) return hr;
    tfSelection.range = pRangeComposition;
    tfSelection.style.ase = TF_AE_NONE;
    tfSelection.style.fInterimChar = FALSE;
    hr = _pContext->SetSelection(ec, 1, &tfSelection);
  }

  return hr;
}

void WeaselTSF::_StartComposition(com_ptr<ITfContext> pContext,
                                  BOOL fCUASWorkaroundEnabled) {
  com_ptr<CStartCompositionEditSession> pStartCompositionEditSession;
  pStartCompositionEditSession.Attach(new CStartCompositionEditSession(
      this, pContext, fCUASWorkaroundEnabled, _cand->style().inline_preedit));
  _cand->StartUI();
  if (pStartCompositionEditSession != nullptr) {
    const HRESULT hr = RequestCompositionEditSession(pContext, _tfClientId,
        pStartCompositionEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
    if (FAILED(hr)) _cand->EndUI();
  }
}

/* End Composition */
class CEndCompositionEditSession : public CEditSession {
 public:
  CEndCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                             com_ptr<ITfContext> pContext,
                             com_ptr<ITfComposition> pComposition,
                             BOOL clear = TRUE)
      : CEditSession(pTextService, pContext), _clear(clear) {
    _pComposition = pComposition;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  BOOL _clear;
};

STDAPI CEndCompositionEditSession::DoEditSession(TfEditCookie ec) {
  if (!_IsCurrent()) return TF_E_DISCONNECTED;
  /* Clear the dummy text we set before, if any. */
  if (_pComposition == nullptr)
    return S_OK;

  _pTextService->_ClearCompositionDisplayAttributes(ec, _pContext, _pComposition);

  com_ptr<ITfRange> pCompositionRange;
  HRESULT clearResult = S_OK;
  if (_clear) {
    clearResult = _pComposition->GetRange(&pCompositionRange);
    if (SUCCEEDED(clearResult)) {
      clearResult = pCompositionRange
          ? pCompositionRange->SetText(ec, 0, L"", 0) : E_FAIL;
    }
  }
  // Attempt termination even if clearing fails, preserving the first error.
  const HRESULT endResult = _pComposition->EndComposition(ec);
  if (SUCCEEDED(endResult))
    _pTextService->_FinalizeComposition(_pComposition);
  return FAILED(clearResult) ? clearResult : endResult;
}

void WeaselTSF::_EndComposition(com_ptr<ITfContext> pContext, BOOL clear) {
  CEndCompositionEditSession* pEditSession;

  _cand->EndUI();
  if ((pEditSession = new CEndCompositionEditSession(
           this, pContext, _pComposition, clear)) != NULL) {
    RequestCompositionEditSession(pContext, _tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
    pEditSession->Release();
  }
}

/* Get Text Extent */
class CGetTextExtentEditSession : public CEditSession {
 public:
  CGetTextExtentEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfContextView> pContextView,
                            com_ptr<ITfComposition> pComposition,
                            bool enhancedPosition)
      : CEditSession(pTextService, pContext) {
    _pContextView = pContextView;
    _pComposition = pComposition;
    _enhancedPosition = enhancedPosition;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfContextView> _pContextView;
  com_ptr<ITfComposition> _pComposition;
  bool _enhancedPosition;
};

STDAPI CGetTextExtentEditSession::DoEditSession(TfEditCookie ec) {
  if (!_IsCurrent()) return TF_E_DISCONNECTED;
  com_ptr<ITfRange> pRange;
  RECT rc;
  BOOL fClipped;
  TF_SELECTION selection = {};
  ULONG nSelection = 0;

  const HRESULT selectionResult = _pContext->GetSelection(
      ec, TF_DEFAULT_SELECTION, 1, &selection, &nSelection);
  com_ptr<ITfRange> selected;
  selected.Attach(selection.range);
  if (FAILED(selectionResult) || nSelection != 1 || !selected)
    return E_FAIL;

  if (_pComposition != nullptr && _pComposition->GetRange(&pRange) == S_OK &&
      pRange) {
    pRange->Collapse(ec, TF_ANCHOR_START);
  } else {
    // composition end
    // note: selection.range is always an empty range
    pRange = selected;
  }

  if ((_pContextView->GetTextExt(ec, pRange, &rc, &fClipped)) == S_OK &&
      (rc.left != 0 || rc.top != 0)) {
    // get the foreground window pos and check if rc from GetTextExt is out of
    // window
    if (_enhancedPosition) {
      HWND hwnd;
      RECT rcForegroundWindow;
      hwnd = GetForegroundWindow();
      ::GetWindowRect(hwnd, &rcForegroundWindow);

      if (rc.left < rcForegroundWindow.left ||
          rc.left > rcForegroundWindow.right ||
          rc.top < rcForegroundWindow.top ||
          rc.top > rcForegroundWindow.bottom) {
        POINT pt;
        bool hasCaret = ::GetCaretPos(&pt);
        int offsetx = rcForegroundWindow.left - rc.left + (hasCaret ? pt.x : 0);
        int offsety = rcForegroundWindow.top - rc.top + (hasCaret ? pt.y : 0);
        rc.left += offsetx;
        rc.right += offsetx;
        rc.top += offsety;
        rc.bottom += offsety;
      }
    }
    _pTextService->_SetCompositionPosition(rc);
  }
  return S_OK;
}

#ifdef WEASEL_CORRECTION_PROBE_BUILD
// Exercise the real edit-session code with diagnostic-owned COM objects only.
// The supplied service must be created by this DLL's class factory.
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeExerciseComposition(
    ITfTextInputProcessorEx* service, ITfContext* context,
    ITfContextView* view, ITfComposition* composition, TfEditCookie cookie,
    DWORD operation, BOOL clear) {
  if (!service || !context || (operation == 1 && !view)) return E_INVALIDARG;
  auto textService = static_cast<WeaselTSF*>(service);
  if (operation == 0) {
    textService->_SetComposition(composition);
    CEndCompositionEditSession session(textService, context, composition, clear);
    const HRESULT result = session.DoEditSession(cookie);
    textService->_FinalizeComposition();
    return result;
  }
  if (operation == 1) {
    CGetTextExtentEditSession session(textService, context, view, composition, false);
    return session.DoEditSession(cookie);
  }
  if (operation == 2) {
    textService->_SetComposition(composition);
    const BOOL result = textService->_SetCompositionDisplayAttributes(cookie, context, nullptr);
    textService->_FinalizeComposition();
    return result ? S_OK : E_FAIL;
  }
  if (operation == 4) {
    textService->_SetComposition(composition);
    return S_OK;
  }
  if (operation == 5)
    return textService->_IsCurrentComposition(composition) ? S_OK : S_FALSE;
  if (operation == 6)
    return textService->OnCompositionTerminated(cookie, composition);
  if (operation == 7) {
    textService->_SetComposition(composition, context);
    return S_OK;
  }
  return E_INVALIDARG;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeCreateCompositionSession(
    ITfTextInputProcessorEx* service, ITfContext* context,
    ITfComposition* composition, DWORD operation, BOOL option,
    ITfEditSession** session) {
  if (!session) return E_POINTER;
  *session = nullptr;
  if (!service || !context) return E_INVALIDARG;
  auto textService = static_cast<WeaselTSF*>(service);
  if (operation == 0)
    *session = new CEndCompositionEditSession(textService, context, composition, option);
  else if (operation == 3)
    *session = new CStartCompositionEditSession(textService, context, FALSE, option);
  else
    return E_INVALIDARG;
  return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeRequestCompositionSession(
    ITfContext* context, ITfEditSession* session, DWORD flags) {
  return RequestCompositionEditSession(context, 123, session, flags);
}
#endif

/* Composition Window Handling */
BOOL WeaselTSF::_UpdateCompositionWindow(com_ptr<ITfContext> pContext) {
  if (!pContext) return FALSE;
  com_ptr<ITfContextView> pContextView;
  if (pContext->GetActiveView(&pContextView) != S_OK || !pContextView)
    return FALSE;
  com_ptr<CGetTextExtentEditSession> pEditSession;
  pEditSession.Attach(
      new CGetTextExtentEditSession(this, pContext, pContextView, _pComposition,
                                    _cand->style().enhanced_position));
  if (pEditSession == NULL) {
    return FALSE;
  }
  return SUCCEEDED(RequestCompositionEditSession(pContext, _tfClientId,
      pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READ));
}

void WeaselTSF::_SetCompositionPosition(const RECT& rc) {
  /* Test if rect is valid.
   * If it is invalid during CUAS test, we need to apply CUAS workaround */
  if (!_fCUASWorkaroundTested) {
    _fCUASWorkaroundTested = TRUE;
    if (rc.top == rc.bottom) {
      _fCUASWorkaroundEnabled = TRUE;
      return;
    }
  }
  RECT _rc;
  _rc.left = _rc.right = rc.left;
  _rc.top = _rc.bottom = rc.bottom;
  m_client.UpdateInputPosition(rc);
  _cand->UpdateInputPosition(rc);
  _automatic->Position(rc);
}

/* Inline Preedit */
class CInlinePreeditEditSession : public CEditSession {
 public:
  CInlinePreeditEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfComposition> pComposition,
                            const std::shared_ptr<weasel::Context> context)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _context(context) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  const std::shared_ptr<weasel::Context> _context;
};

STDAPI CInlinePreeditEditSession::DoEditSession(TfEditCookie ec) {
  if (!_IsCurrent()) return TF_E_DISCONNECTED;
  std::wstring preedit = _context->preedit.str;

  com_ptr<ITfRange> pRangeComposition;
  if (_pComposition == nullptr)
    return E_FAIL;
  HRESULT hr = _pComposition->GetRange(&pRangeComposition);
  if (FAILED(hr)) return hr;
  if (!pRangeComposition) return E_FAIL;
  hr = pRangeComposition->SetText(ec, 0, preedit.c_str(), static_cast<LONG>(preedit.length()));
  if (FAILED(hr)) return hr;
  if (!_IsCurrent()) return TF_E_DISCONNECTED;

  /* TODO: Check the availability and correctness of these values */
  int sel_cursor = -1;
  for (size_t i = 0; i < _context->preedit.attributes.size(); i++) {
    if (_context->preedit.attributes.at(i).type == weasel::HIGHLIGHTED) {
      sel_cursor = _context->preedit.attributes.at(i).range.cursor;
      break;
    }
  }

  _pTextService->_SetCompositionDisplayAttributes(ec, _pContext,
                                                  pRangeComposition);

  /* Set caret */
  LONG cch = 0;
  TF_SELECTION tfSelection = {};
  if (sel_cursor < 0) {
    hr = pRangeComposition->Collapse(ec, TF_ANCHOR_END);
  } else {
    if (static_cast<size_t>(sel_cursor) > preedit.size()) return E_INVALIDARG;
    hr = pRangeComposition->Collapse(ec, TF_ANCHOR_START);
    if (SUCCEEDED(hr)) hr = pRangeComposition->ShiftStart(ec, sel_cursor, &cch, NULL);
    if (SUCCEEDED(hr) && cch != sel_cursor) hr = E_FAIL;
  }
  if (FAILED(hr)) return hr;
  if (!_IsCurrent()) return TF_E_DISCONNECTED;
  tfSelection.range = pRangeComposition;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;
  return _pContext->SetSelection(ec, 1, &tfSelection);
}

BOOL WeaselTSF::_ShowInlinePreedit(
    com_ptr<ITfContext> pContext,
    const std::shared_ptr<weasel::Context> context) {
  com_ptr<CInlinePreeditEditSession> pEditSession;
  pEditSession.Attach(
      new CInlinePreeditEditSession(this, pContext, _pComposition, context));
  if (pEditSession != NULL) {
    return SUCCEEDED(RequestCompositionEditSession(pContext, _tfClientId,
        pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE));
  }
  return FALSE;
}

/* Update Composition */
class CInsertTextEditSession : public CEditSession {
 public:
  CInsertTextEditSession(com_ptr<WeaselTSF> pTextService,
                         com_ptr<ITfContext> pContext,
                         com_ptr<ITfComposition> pComposition,
                         const std::wstring& text)
      : CEditSession(pTextService, pContext),
        _text(text),
        _pComposition(pComposition) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  std::wstring _text;
  com_ptr<ITfComposition> _pComposition;
};

STDMETHODIMP CInsertTextEditSession::DoEditSession(TfEditCookie ec) {
  if (!_IsCurrent()) return TF_E_DISCONNECTED;
  com_ptr<ITfRange> pRange;
  TF_SELECTION tfSelection;
  HRESULT hRet = S_OK;

  if (_pComposition == nullptr)
    return E_FAIL;
  hRet = _pComposition->GetRange(&pRange);
  if (FAILED(hRet)) return hRet;
  if (!pRange) return E_FAIL;

  hRet = pRange->SetText(ec, 0, _text.c_str(), static_cast<LONG>(_text.length()));
  if (FAILED(hRet)) return hRet;
  if (!_IsCurrent()) return TF_E_DISCONNECTED;

  /* update the selection to an insertion point just past the inserted text. */
  hRet = pRange->Collapse(ec, TF_ANCHOR_END);
  if (FAILED(hRet)) return hRet;
  if (!_IsCurrent()) return TF_E_DISCONNECTED;

  tfSelection.range = pRange;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;

  hRet = _pContext->SetSelection(ec, 1, &tfSelection);

  return hRet;
}

HRESULT WeaselTSF::_ApplyCompositionResponse(TfEditCookie cookie, ITfContext* context,
                                            const ServerResponse& response) {
  if (!context) return E_INVALIDARG;
  const auto generation = _GetEditGeneration();
  auto current = [&]() { return _IsEditSessionCurrent(generation); };
  auto start = [&]() {
    _cand->StartUI();
    CStartCompositionEditSession session(this, context,
        _fCUASWorkaroundEnabled && !response.config.inline_preedit, response.config.inline_preedit);
    const auto result = session.DoEditSession(cookie);
    if (FAILED(result)) _cand->EndUI();
    return result;
  };
  auto end = [&](BOOL clear) {
    CEndCompositionEditSession session(this, context, _pComposition, clear);
    const auto result = session.DoEditSession(cookie);
    if (!_IsComposing()) _cand->EndUI();
    return result;
  };
  _committed = FALSE;
  if (!response.commit.empty()) {
    HRESULT hr = _IsComposing() ? S_OK : start();
    if (FAILED(hr)) return hr;
    if (!current()) return TF_E_DISCONNECTED;
    CInsertTextEditSession insert(this, context, _pComposition, response.commit);
    hr = insert.DoEditSession(cookie);
    if (FAILED(hr)) return hr;
    com_ptr<ITfRange> committedRange;
    if (_pComposition) _pComposition->GetRange(&committedRange);
    _committed = TRUE;
    if (!current()) return TF_E_DISCONNECTED;
    hr = end(FALSE);
    if (FAILED(hr)) return hr;
    if (!current()) return TF_E_DISCONNECTED;
    _automatic->Capture(context, cookie, committedRange, response.commit);
#ifdef WEASEL_CORRECTION_PROBE_BUILD
    ProbeCaptureCommit(context, cookie, response.commit);
#endif
  }
  if (!current()) return TF_E_DISCONNECTED;
  HRESULT hr = S_OK;
  if (response.status.composing && !_IsComposing()) hr = start();
  else if (!response.status.composing && _IsComposing()) hr = end(TRUE);
  if (FAILED(hr)) return hr;
  if (!current()) return TF_E_DISCONNECTED;
  if (_IsComposing() && response.config.inline_preedit) {
    CInlinePreeditEditSession inlineSession(this, context, _pComposition, response.context);
    hr = inlineSession.DoEditSession(cookie);
  }
  if (SUCCEEDED(hr) && current()) _automatic->RecordInput(context, cookie);
  return hr;
}

#ifdef WEASEL_CORRECTION_PROBE_BUILD
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeCreateTextSession(
    ITfTextInputProcessorEx* service, ITfContext* context, ITfComposition* composition,
    DWORD operation, const WCHAR* text, ITfEditSession** session) {
  if (!session) return E_POINTER;
  *session = nullptr;
  if (!service || !context || !text) return E_INVALIDARG;
  auto textService = static_cast<WeaselTSF*>(service);
  if (operation == 0) *session = new CInsertTextEditSession(textService, context, composition, text);
  else if (operation == 1) {
    auto content = std::make_shared<weasel::Context>();
    content->preedit.str = text;
    *session = new CInlinePreeditEditSession(textService, context, composition, content);
  } else return E_INVALIDARG;
  return S_OK;
}
#endif

BOOL WeaselTSF::_InsertText(com_ptr<ITfContext> pContext,
                            const std::wstring& text) {
  CInsertTextEditSession* pEditSession;

  if ((pEditSession = new CInsertTextEditSession(this, pContext, _pComposition,
                                                 text)) != NULL) {
    const HRESULT hr = RequestCompositionEditSession(pContext, _tfClientId,
        pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
    pEditSession->Release();
    return SUCCEEDED(hr);
  }

  return FALSE;
}

void WeaselTSF::_UpdateComposition(com_ptr<ITfContext> pContext) {
  if (!pContext) { _async_edit = FALSE; return; }
  // Capture before binding/queuing: callbacks and later IPC calls can overwrite
  // the shared receive buffer. A complete body is consumed exactly once.
  auto response = CaptureServerResponse(_status,
      _lastCapturedStyle ? *_lastCapturedStyle : _cand->style(),
      [&](const weasel::ResponseHandler& parser) { return m_client.TakeResponseData(parser); });
  if (!response) return;
  _BindEditContext(pContext);
  _lastCapturedStyle = std::make_shared<const weasel::UIStyle>(response->style);
  com_ptr<CServerResponseEditSession> session;
  session.Attach(new CServerResponseEditSession(this, pContext, response));

  const HRESULT hr = RequestCompositionEditSession(pContext,
      _tfClientId, session, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
  _async_edit = !!(hr == TF_S_ASYNC);
  if (SUCCEEDED(hr)) _UpdateCompositionWindow(pContext);
}

/* Composition State */
void WeaselTSF::_InvalidateEditSessions() {
  _automatic->ResetKeys();
  _automatic->Cancel();
  ++_editGeneration;
  _lastCapturedStyle.reset();
#ifdef WEASEL_CORRECTION_PROBE_BUILD
  ProbeCancelCapture();
#endif
  _pEditSessionContext = nullptr;
  _async_edit = FALSE;
  _fTestKeyDownPending = _fTestKeyUpPending = FALSE;
}

void WeaselTSF::_BindEditContext(ITfContext* context) {
  if (_pEditSessionContext != context) {
    _InvalidateEditSessions();
    // Retire the old document's composition using its own context. Detach it
    // now so a new document cannot reuse the old range while cleanup is queued.
    if (_pComposition && _pCompositionContext && _pCompositionContext != context) {
      com_ptr<ITfComposition> previous = _pComposition;
      _EndComposition(_pCompositionContext, TRUE);
      _FinalizeComposition(previous);
    }
    _pEditSessionContext = context;
  }
}

STDAPI WeaselTSF::OnCompositionTerminated(TfEditCookie ecWrite,
                                          ITfComposition* pComposition) {
  // A delayed termination notification must not abort a newer composition.
  if (!pComposition || !_IsCurrentComposition(pComposition)) return S_OK;
  // NOTE:
  // This will be called when an edit session ended up with an empty composition
  // string, Even if it is closed normally. Silly M$.

  _AbortComposition();
  return S_OK;
}

void WeaselTSF::_AbortComposition(bool clear) {
  m_client.ClearComposition();
  if (_IsComposing()) {
    _EndComposition(_pEditSessionContext, clear);
  }
  _committed = TRUE;
  _cand->Destroy();
}

void WeaselTSF::_FinalizeComposition(ITfComposition* expected) {
  if (!expected || _pComposition == expected) {
    _pComposition = nullptr;
    _pCompositionContext = nullptr;
  }
}

void WeaselTSF::_SetComposition(com_ptr<ITfComposition> pComposition,
                              ITfContext* context) {
  _pComposition = pComposition;
  _pCompositionContext = pComposition ? context : nullptr;
}

BOOL WeaselTSF::_IsComposing() {
  return _pComposition != NULL;
}
