#pragma once
#include "CorrectionScope.h"
#include "CorrectionFocus.h"
#include "CorrectionBrokerWire.h"

namespace correction {
// Local-test-only, closed schema: no characters, addresses, HWNDs or paths.
struct ScopeTrace {
  DWORD version=1, phase=0, flags=0;
  HRESULT rangeContext=E_PENDING, selectionLock=E_PENDING, original=E_PENDING,
      start=E_PENDING, end=E_PENDING, selection=E_PENDING, enumeration=E_PENDING;
};
static_assert(sizeof(ScopeTrace)==40,"bounded scope trace metadata");
inline ScopeTrace TraceInputScope(ITfContext* context,TfEditCookie cookie,ITfRange* range,DWORD phase) {
  ScopeTrace trace;trace.phase=phase;
  if(!context || !range)return trace;
  CComPtr<ITfContext> rangeContext;
  trace.rangeContext=range->GetContext(&rangeContext);
  if(trace.rangeContext==S_OK && SameIdentity(context,rangeContext))trace.flags|=1;
  BOOL empty=FALSE;
  if(range->IsEmpty(cookie,&empty)==S_OK){trace.flags|=2;if(empty)trace.flags|=4;}
  CComPtr<ITfReadOnlyProperty> property;
  const HRESULT got=context->GetAppProperty(GUID_PROP_INPUTSCOPE,&property);
  if(got!=S_OK || !property){trace.original=got==S_OK?E_POINTER:got;return trace;}
  CComPtr<ITfContext> propertyContext;
  if(property->GetContext(&propertyContext)==S_OK) {
    trace.flags|=16;if(SameIdentity(context,propertyContext))trace.flags|=8;
  }
  auto value=[&](ITfRange* target)->HRESULT {
    VARIANT result;VariantInit(&result);const HRESULT hr=property->GetValue(cookie,target,&result);
    VariantClear(&result);return hr;
  };
  trace.original=value(range);
  CComPtr<ITfRange> start,end;
  if(range->Clone(&start)==S_OK && start && start->Collapse(cookie,TF_ANCHOR_START)==S_OK)trace.start=value(start);
  if(range->Clone(&end)==S_OK && end && end->Collapse(cookie,TF_ANCHOR_END)==S_OK)trace.end=value(end);
  TF_SELECTION selected{};ULONG count=0;
  trace.selectionLock=context->GetSelection(cookie,TF_DEFAULT_SELECTION,1,&selected,&count);
  CComPtr<ITfRange> selection;selection.Attach(selected.range);
  if(trace.selectionLock==S_OK && count==1 && selection)trace.selection=value(selection);
  CComPtr<IEnumTfRanges> ranges;
  trace.enumeration=property->EnumRanges(cookie,&ranges,range);
  return trace;
}
#ifdef WEASELC_LOCAL_TEST_BUILD
inline void SaveScopeTrace(const ScopeTrace& trace) {
  if(trace.phase>2)return;
#if defined(WEASELTSF_EXPORTS) && !defined(WEASEL_CORRECTION_PROBE_BUILD)
  RelayCorrectionMetadata(BrokerMetadata::ScopeTrace,&trace,sizeof(trace));
#endif
}
#endif
}
