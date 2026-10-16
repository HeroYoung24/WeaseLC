#include "stdafx.h"
#include "WeaselTSF.h"
#include <CorrectionProbe.h>
#include <initguid.h>
#include <inputscope.h>
#include <CorrectionCapture.h>

#ifdef WEASEL_CORRECTION_PROBE_BUILD
namespace correction {
bool g_captureProbeResults = false;
ProbeResult g_capturedProbeResult;
}

extern "C" __declspec(dllexport) void WINAPI
WeaselProbeCaptureResultsInMemory(BOOL capture) {
  correction::g_captureProbeResults = capture != FALSE;
  correction::g_capturedProbeResult = {};
}

extern "C" __declspec(dllexport) HRESULT WINAPI
WeaselProbeGetCapturedResult(correction::ProbeResult* result) {
  if (!result)
    return E_POINTER;
  *result = correction::g_capturedProbeResult;
  return S_OK;
}
#endif

namespace {
bool IsOrdinaryTextScope(ITfContext* context, TfEditCookie cookie,
                         ITfRange* selected, correction::ProbeStatus& status) {
  com_ptr<ITfContextView> view;
  HWND window = nullptr;
  if (context->GetActiveView(&view) == S_OK && view &&
      view->GetWnd(&window) == S_OK && window &&
      (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD)) {
    status = correction::ProbeStatus::ProtectedField;
    return false;
  }

  com_ptr<ITfReadOnlyProperty> property;
  if (context->GetAppProperty(GUID_PROP_INPUTSCOPE, &property) != S_OK ||
      !property) {
    status = correction::ProbeStatus::ScopeUnavailable;
    return false;
  }
  VARIANT value;
  VariantInit(&value);
  const HRESULT read_property = property->GetValue(cookie, selected, &value);
  com_ptr<ITfInputScope> input_scope;
  if (read_property == S_OK && value.vt == VT_UNKNOWN && value.punkVal)
    value.punkVal->QueryInterface(
        IID_ITfInputScope, reinterpret_cast<void**>(&input_scope));
  VariantClear(&value);
  if (!input_scope) {
    status = correction::ProbeStatus::ScopeUnavailable;
    return false;
  }

  InputScope* scopes = nullptr;
  UINT count = 0;
  if (input_scope->GetInputScopes(&scopes, &count) != S_OK || !scopes ||
      count == 0) {
    CoTaskMemFree(scopes);
    status = correction::ProbeStatus::ScopeUnavailable;
    return false;
  }
  bool ordinary = true;
  for (UINT i = 0; i < count; ++i) {
    if (scopes[i] == IS_PASSWORD || scopes[i] == IS_NUMERIC_PASSWORD ||
        scopes[i] == IS_PRIVATE) {
      status = correction::ProbeStatus::ProtectedField;
      ordinary = false;
      break;
    }
    if (scopes[i] != IS_TEXT) {
      status = correction::ProbeStatus::ScopeUnavailable;
      ordinary = false;
    }
  }
  CoTaskMemFree(scopes);
  return ordinary;
}

bool ReadSide(ITfRange* selected, TfEditCookie cookie, bool before,
              DWORD& count) {
  com_ptr<ITfRange> range;
  if (selected->Clone(&range) != S_OK || !range ||
      range->Collapse(cookie, before ? TF_ANCHOR_START : TF_ANCHOR_END) !=
          S_OK)
    return false;
  LONG shifted = 0;
  const HRESULT shift = before
                            ? range->ShiftStart(cookie,
                                                -correction::kProbeCharactersEachSide,
                                                &shifted, nullptr)
                            : range->ShiftEnd(cookie,
                                              correction::kProbeCharactersEachSide,
                                              &shifted, nullptr);
  if (shift != S_OK)
    return false;
  WCHAR text[correction::kProbeCharactersEachSide] = {};
  ULONG read = 0;
  const HRESULT result = range->GetText(cookie, 0, text, _countof(text), &read);
  SecureZeroMemory(text, sizeof(text));
  if (result != S_OK)
    return false;
  count = read;
  return true;
}

correction::ProbeResult ProbeContext(ITfContext* context,
                                    TfEditCookie cookie) {
  correction::ProbeResult result;
  result.status = correction::ProbeStatus::SelectionUnavailable;
  TF_SELECTION selection = {};
  ULONG fetched = 0;
  if (context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection,
                            &fetched) != S_OK || fetched != 1 ||
      !selection.range)
    return result;
  com_ptr<ITfRange> selected;
  selected.Attach(selection.range);
  if (!IsOrdinaryTextScope(context, cookie, selected, result.status))
    return result;
  result.status = correction::ProbeStatus::ReadFailed;
  if (!ReadSide(selected, cookie, true, result.before) ||
      !ReadSide(selected, cookie, false, result.after)) {
    result.before = result.after = 0;
    return result;
  }
  result.status = correction::ProbeStatus::Readable;
  return result;
}
}  // namespace

bool WeaselTSF::_CorrectionSafeScope(ITfContext* context, TfEditCookie cookie, ITfRange* range) {
  correction::ProbeStatus status = correction::ProbeStatus::ScopeUnavailable;
  return context && range && IsOrdinaryTextScope(context, cookie, range, status);
}

#ifdef WEASEL_CORRECTION_PROBE_BUILD
namespace {
correction::CommitCaptureSink commitSink = nullptr;
void* commitSinkContext = nullptr;
ULONGLONG captureSequence = 0;
}
extern "C" __declspec(dllexport) void WINAPI WeaselProbeSetCommitSink(
    correction::CommitCaptureSink sink, void* context) {
  commitSink = sink;
  commitSinkContext = sink ? context : nullptr;
}
void WeaselTSF::ProbeCancelCapture() {
  if (!commitSink) return;
  correction::CommitCapture event;
  event.generation = _GetEditGeneration(); event.sequence = ++captureSequence;
  try { commitSink(&event, commitSinkContext); } catch (...) {}
}
void WeaselTSF::ProbeCaptureCommit(ITfContext* context, TfEditCookie cookie,
                                 const std::wstring& text) {
  if (!commitSink) return;
  const auto generation = _GetEditGeneration();
  ProbeCancelCapture();
  if (!context || text.empty() || text.size() > correction::kCommitCaptureCapacity) return;
  TF_SELECTION selection = {};
  ULONG fetched = 0;
  const HRESULT hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
  com_ptr<ITfRange> range;
  range.Attach(selection.range);
  correction::ProbeStatus scope = correction::ProbeStatus::ScopeUnavailable;
  if (hr != S_OK || fetched != 1 || !range || !IsOrdinaryTextScope(context, cookie, range, scope) ||
      !_IsEditSessionCurrent(generation) || !commitSink) return;
  correction::CommitCapture event;
  event.kind = correction::CaptureKind::Commit;
  event.generation = _GetEditGeneration(); event.sequence = ++captureSequence;
  event.units = static_cast<DWORD>(text.size());
  memcpy(event.text, text.data(), text.size() * sizeof(WCHAR));
  try { commitSink(&event, commitSinkContext); } catch (...) {}
  SecureZeroMemory(event.text, sizeof(event.text));
}
#endif

static BOOL IsRangeCovered(TfEditCookie ec,
                           ITfRange* pRangeTest,
                           ITfRange* pRangeCover) {
  LONG lResult;

  if (pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult) !=
          S_OK ||
      lResult > 0)
    return FALSE;
  if (pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult) !=
          S_OK ||
      lResult < 0)
    return FALSE;
  return TRUE;
}

STDAPI WeaselTSF::OnEndEdit(ITfContext* pContext,
                            TfEditCookie ecReadOnly,
                            ITfEditRecord* pEditRecord) {
  BOOL fSelectionChanged;
  IEnumTfRanges* pEnumTextChanges;
  ITfRange* pRange;

  /* did the selection change? */
  if (pEditRecord->GetSelectionStatus(&fSelectionChanged) == S_OK &&
      fSelectionChanged) {
    if (_IsComposing()) {
      /* if the caret moves out of composition range, stop the composition */
      TF_SELECTION tfSelection = {};
      ULONG cFetched;

      if (pContext->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1,
                                 &tfSelection, &cFetched) == S_OK &&
          cFetched == 1 && tfSelection.range) {
        ITfRange* pRangeComposition;
        if (_pComposition->GetRange(&pRangeComposition) == S_OK) {
          if (!IsRangeCovered(ecReadOnly, tfSelection.range, pRangeComposition))
            _EndComposition(pContext, true);
          pRangeComposition->Release();
        }
        tfSelection.range->Release();
      }
    }
  }

  /* text modification? */
  _automatic->Observe(pContext, ecReadOnly);
  bool text_changed = false;
  if (pEditRecord->GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT, NULL, 0,
                                             &pEnumTextChanges) == S_OK) {
    if (pEnumTextChanges->Next(1, &pRange, NULL) == S_OK) {
      text_changed = true;
      pRange->Release();
    }
    pEnumTextChanges->Release();
  }
  if (_correctionProbePending && text_changed &&
      pContext == _correctionProbeContext) {
    _correctionProbePending = false;
    _correctionProbeContext = nullptr;
    com_ptr<ITfDocumentMgr> focusedDocument;
    com_ptr<ITfContext> focusedContext;
    if (_pThreadMgr && _pThreadMgr->GetFocus(&focusedDocument) == S_OK &&
        focusedDocument && focusedDocument->GetTop(&focusedContext) == S_OK &&
        focusedContext == pContext)
      correction::SaveProbeResult(ProbeContext(pContext, ecReadOnly));
    else
      correction::SaveProbeResult({1, correction::ProbeStatus::FocusChanged});
  }
  return S_OK;
}

STDAPI WeaselTSF::OnLayoutChange(ITfContext* pContext,
                                 TfLayoutCode lcode,
                                 ITfContextView* pContextView) {
  _automatic->LayoutChanged(pContext, lcode == TF_LC_DESTROY);
  if (!_IsComposing())
    return S_OK;

  if (pContext != _pTextEditSinkContext)
    return S_OK;

  if (lcode == TF_LC_CHANGE)
    _UpdateCompositionWindow(pContext);
  return S_OK;
}

BOOL WeaselTSF::_InitTextEditSink(com_ptr<ITfDocumentMgr> pDocMgr) {
  if (_correctionProbePending) {
    _correctionProbePending = false;
    _correctionProbeContext = nullptr;
    correction::SaveProbeResult(
        {1, correction::ProbeStatus::FocusChanged});
  }
  com_ptr<ITfSource> pSource;
  BOOL fRet;

  /* clear out any previous sink first */
  if (_pTextEditSinkContext) {
    _pTextEditSinkContext->QueryInterface(&pSource);
    if (pSource != nullptr) {
      if (_dwTextEditSinkCookie != TF_INVALID_COOKIE)
        pSource->UnadviseSink(_dwTextEditSinkCookie);
      if (_dwTextLayoutSinkCookie != TF_INVALID_COOKIE)
        pSource->UnadviseSink(_dwTextLayoutSinkCookie);
    }
    _pTextEditSinkContext = nullptr;
  }
  _dwTextEditSinkCookie = TF_INVALID_COOKIE;
  _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  if (pDocMgr == NULL)
    return TRUE;

  if (pDocMgr->GetTop(&_pTextEditSinkContext) != S_OK)
    return FALSE;

  if (_pTextEditSinkContext == NULL)
    return TRUE;

  fRet = FALSE;

  pSource.Release();

  if (_pTextEditSinkContext->QueryInterface(IID_ITfSource, (void**)&pSource) ==
      S_OK) {
    if (pSource->AdviseSink(IID_ITfTextEditSink, (ITfTextEditSink*)this,
                            &_dwTextEditSinkCookie) == S_OK)
      fRet = TRUE;
    else
      _dwTextEditSinkCookie = TF_INVALID_COOKIE;
    if (pSource->AdviseSink(IID_ITfTextLayoutSink, (ITfTextLayoutSink*)this,
                            &_dwTextLayoutSinkCookie) == S_OK) {
      fRet = TRUE;
    } else
      _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  }
  if (fRet == FALSE) {
    _pTextEditSinkContext = nullptr;
  }

  return fRet;
}
