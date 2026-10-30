#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "ResponseParser.h"
#include "EditSession.h"
#include <ResponseSlot.h>
#include <ResponseParser.h>

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
  if (_pComposition && _pCompositionContext && !correction::SameIdentity(_pCompositionContext, target))
    return TF_E_DISCONNECTED;
  if (!correction::CheckContextFocus(_pThreadMgr, target))
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
  // Optional parser outputs are independent. Missing config must not be
  // dereferenced merely because a caller requested context, and a cursor
  // record must contain all three coordinates before accessing the third.
  weasel::Context context;
  weasel::ResponseParser contextOnly(nullptr,&context);
  contextOnly.Feed(L"action=config,ctx");
  contextOnly.Feed(L"config.inline_preedit=1");
  contextOnly.Feed(L"ctx.preedit.cursor=0,1");
  if(!context.preedit.attributes.empty())return E_FAIL;
  contextOnly.Feed(L"ctx.preedit.cursor=0,1,1");
  if(context.preedit.attributes.size()!=1 || context.preedit.attributes[0].range.cursor!=1)return E_FAIL;
  weasel::Config config;config.inline_preedit=FALSE;
  weasel::ResponseParser configOnly(nullptr,nullptr,nullptr,&config);
  configOnly.Feed(L"action=config");configOnly.Feed(L"config.inline_preedit=1");
  if(!config.inline_preedit)return E_FAIL;
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

extern "C" __declspec(dllexport) HRESULT WINAPI WeaselProbeAutomaticPreeditOptions(
    ITfTextInputProcessorEx* service, ITfContext* context, TfEditCookie cookie,
    const WCHAR* text, BOOL inlinePreedit, LONG caretOffset) {
  if (!service || !context || !text || (inlinePreedit != FALSE && inlinePreedit != TRUE) || caretOffset < 0)
    return E_INVALIDARG;
  auto* tsf = static_cast<WeaselTSF*>(service);
  tsf->_BindEditContext(context);
  ServerResponse response; response.config.inline_preedit = inlinePreedit; response.status.composing = TRUE;
  auto content = std::make_shared<weasel::Context>(); content->preedit.str = text; response.context = content;
  const auto hr = tsf->_ApplyCompositionResponse(cookie,context,response);
  if (hr != S_OK) return hr;
  com_ptr<ITfRange> range;
  if (tsf->ProbeCompositionRange(&range) != S_OK || !range) return E_FAIL;
  WCHAR buffer[1025]{}; ULONG units = 0;
  if (range->GetText(cookie,0,buffer,_countof(buffer),&units) != S_OK || static_cast<ULONG>(caretOffset) > units) return E_INVALIDARG;
  LONG moved = 0;
  if (range->Collapse(cookie,TF_ANCHOR_START) != S_OK ||
      range->ShiftEnd(cookie,caretOffset,&moved,nullptr) != S_OK || moved != caretOffset ||
      range->Collapse(cookie,TF_ANCHOR_END) != S_OK) return E_FAIL;
  TF_SELECTION selection{}; selection.range = range; selection.style.ase = TF_AE_NONE;
  if (context->SetSelection(cookie,1,&selection) != S_OK) return E_FAIL;
  tsf->Automatic().RecordInput(context,cookie); tsf->Automatic().Position(RECT{100,100,110,120});
  SecureZeroMemory(buffer,sizeof(buffer));
  return S_OK;
}
#endif

