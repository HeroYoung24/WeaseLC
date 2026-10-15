#include "stdafx.h"

#include "WeaselTSF.h"

void WeaselTSF::_ClearCompositionDisplayAttributes(TfEditCookie ec,
                                                   _In_ ITfContext* pContext,
                                                   ITfComposition* composition) {
  if (!composition || !pContext) return;
  com_ptr<ITfRange> pRangeComposition;
  com_ptr<ITfProperty> pDisplayAttributeProperty;

  if (FAILED(composition->GetRange(&pRangeComposition)) ||
      !pRangeComposition) {
    return;
  }

  if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE,
                                      &pDisplayAttributeProperty)) &&
      pDisplayAttributeProperty) {
    pDisplayAttributeProperty->Clear(ec, pRangeComposition);
  }
}

BOOL WeaselTSF::_SetCompositionDisplayAttributes(TfEditCookie ec,
                                                 _In_ ITfContext* pContext,
                                                 ITfRange* pRangeComposition) {
  if (!pContext) return FALSE;
  com_ptr<ITfProperty> pDisplayAttributeProperty;
  com_ptr<ITfRange> acquiredRange;
  HRESULT hr = S_OK;

  if (pRangeComposition == nullptr) {
    if (!_pComposition) return FALSE;
    hr = _pComposition->GetRange(&acquiredRange);
    pRangeComposition = acquiredRange;
  }
  if (FAILED(hr) || !pRangeComposition) {
    return FALSE;
  }

  hr = E_FAIL;

  if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE,
                                      &pDisplayAttributeProperty)) &&
      pDisplayAttributeProperty) {
    VARIANT var;
    var.vt = VT_I4;  // we're going to set a TfGuidAtom
    var.lVal = _gaDisplayAttributeInput;

    hr = pDisplayAttributeProperty->SetValue(ec, pRangeComposition, &var);
  }

  // A caller-supplied range is borrowed; only acquiredRange owns a reference.
  return (hr == S_OK);
}

BOOL WeaselTSF::_InitDisplayAttributeGuidAtom() {
  ITfCategoryMgr* pCategoryMgr = nullptr;
  HRESULT hr =
      CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                       IID_ITfCategoryMgr, (void**)&pCategoryMgr);

  if (FAILED(hr)) {
    return FALSE;
  }

  hr = pCategoryMgr->RegisterGUID(c_guidDisplayAttributeInput,
                                  &_gaDisplayAttributeInput);
  if (FAILED(hr)) {
    goto Exit;
  }

Exit:
  pCategoryMgr->Release();

  return (hr == S_OK);
}
