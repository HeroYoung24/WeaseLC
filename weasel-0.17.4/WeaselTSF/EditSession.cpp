#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "ResponseParser.h"
#include "EditSession.h"
#include <ResponseSlot.h>

STDAPI WeaselTSF::DoEditSession(TfEditCookie ec) {
  // Internal requests now own their context in CServerResponseEditSession.
  // An unbound callback cannot pair this cookie with a mutable context safely.
  return E_UNEXPECTED;
}

HRESULT WeaselTSF::_ApplyServerResponse(TfEditCookie ec, ITfContext* target,
                                      const ServerResponse& response) {
  const ULONGLONG generation = _editGeneration;
  if (!_pThreadMgr || _tfClientId == TF_CLIENTID_NULL || !target)
    return TF_E_DISCONNECTED;
  if (_pComposition && _pCompositionContext && _pCompositionContext != target)
    return TF_E_DISCONNECTED;
  com_ptr<ITfDocumentMgr> focusedDocument;
  com_ptr<ITfContext> focusedContext;
  if (_pThreadMgr->GetFocus(&focusedDocument) != S_OK || !focusedDocument ||
      focusedDocument->GetTop(&focusedContext) != S_OK || focusedContext != target)
    return TF_E_DISCONNECTED;
  // Keep the captured context alive if callbacks change service state.
  com_ptr<ITfContext> editContext = target;
  if (!_IsEditSessionCurrent(generation)) return TF_E_DISCONNECTED;
  _status = response.status;
  _cand->style() = response.style;
  _UpdateLanguageBar(_status);
  if (!_IsEditSessionCurrent(generation)) return TF_E_DISCONNECTED;
  // Use the granted write cookie; never queue nested composition mutations.
  const HRESULT applied = _ApplyCompositionResponse(ec, editContext, response);
  if (FAILED(applied)) return applied;
  if (!_IsEditSessionCurrent(generation)) return TF_E_DISCONNECTED;
  _UpdateCompositionWindow(editContext);

  if (!_IsEditSessionCurrent(generation)) return TF_E_DISCONNECTED;
  _UpdateUI(*response.context, _status);

  return S_OK;
}

#ifdef WEASEL_CORRECTION_PROBE_BUILD
extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeSnapshotChecks() {
  std::wstring buffer = L"action=commit,ctx\ncommit=甲\nctx.preedit=甲\n.\n";
  weasel::ResponseSlot slot;
  auto pull = [&](const weasel::ResponseHandler& parser) {
    const auto units = slot.TakeUnits();
    return units && parser(buffer.data(), static_cast<UINT>(units));
  };
  weasel::Status status;
  weasel::UIStyle style;
  slot.Complete(buffer.size() * sizeof(WCHAR), buffer.size() * sizeof(WCHAR));
  auto first = CaptureServerResponse(status, style, pull);
  if (!first || first->commit != L"甲" || first->context->preedit.str != L"甲") return E_FAIL;
  if (CaptureServerResponse(status, style, pull)) return E_FAIL;
  buffer = L"action=commit,ctx\ncommit=乙\nctx.preedit=乙\n.\n";
  slot.Complete(buffer.size() * sizeof(WCHAR), buffer.size() * sizeof(WCHAR));
  auto second = CaptureServerResponse(status, first->style, pull);
  buffer.assign(2048, L'X');
  if (!second || second->commit != L"乙" || second->context->preedit.str != L"乙" ||
      first->commit != L"甲" || first->context->preedit.str != L"甲") return E_FAIL;
  return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeApplySyntheticResponse(
    ITfTextInputProcessorEx* service, ITfContext* context, TfEditCookie cookie,
    const WCHAR* commit) {
  if (!service || !context || !commit) return E_INVALIDARG;
  auto textService = static_cast<WeaselTSF*>(service);
  textService->_BindEditContext(context);
  ServerResponse response;
  response.commit = commit; response.config.inline_preedit = TRUE;
  response.status.composing = FALSE;
  return textService->_ApplyCompositionResponse(cookie, context, response);
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeCreateBoundEditSession(
    ITfTextInputProcessorEx* service, ITfContext* context,
    CServerResponseEditSession::Observer observer, ITfEditSession** session) {
  if (!session) return E_POINTER;
  *session = nullptr;
  if (!service || !context || !observer) return E_INVALIDARG;
  auto textService = static_cast<WeaselTSF*>(service);
  textService->_BindEditContext(context);
  *session = new CServerResponseEditSession(textService, context, observer);
  return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticPreedit(
    ITfTextInputProcessorEx* service, ITfContext* context, TfEditCookie cookie, const WCHAR* text) {
  if (!service || !context || !text) return E_INVALIDARG;
  auto* tsf = static_cast<WeaselTSF*>(service);
  tsf->_BindEditContext(context);
  ServerResponse response;
  response.config.inline_preedit = TRUE; response.status.composing = TRUE;
  auto content = std::make_shared<weasel::Context>(); content->preedit.str = text;
  response.context = content;
  const HRESULT hr = tsf->_ApplyCompositionResponse(cookie, context, response);
  tsf->Automatic().Position(RECT{100, 100, 110, 120});
  return hr;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeInvalidateEditSessions(
    ITfTextInputProcessorEx* service) {
  if (!service) return E_POINTER;
  static_cast<WeaselTSF*>(service)->_InvalidateEditSessions();
  return S_OK;
}
#endif

