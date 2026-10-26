#pragma once
#include <vector>
#include "DocumentScopeChecks.h"

class ScopeFixture : public ITfInputScope {
 public:
  ULONG refs = 1;
  HRESULT query = S_OK, result = S_OK;
  bool nullArray = false;
  std::vector<InputScope> scopes{IS_TEXT};
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfInputScope) return E_NOINTERFACE;
    if (query != S_OK) return query;
    *out = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP GetInputScopes(InputScope** out, UINT* count) override {
    *count = static_cast<UINT>(scopes.size()); *out = nullptr;
    if (nullArray || scopes.empty()) return result;
    *out = static_cast<InputScope*>(CoTaskMemAlloc(scopes.size()*sizeof(InputScope)));
    if (!*out) return E_OUTOFMEMORY;
    std::copy(scopes.begin(),scopes.end(),*out); return result;
  }
  STDMETHODIMP GetPhrase(BSTR**,UINT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetRegularExpression(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP GetSRGS(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP GetXML(BSTR*) override { return E_NOTIMPL; }
};
class ScopeProperty : public ITfReadOnlyProperty {
 public:
  ULONG refs = 1;
  HRESULT result = S_OK;
  VARTYPE type = VT_UNKNOWN;
  IUnknown* value = nullptr;
  STDMETHODIMP QueryInterface(REFIID id,void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfReadOnlyProperty) return E_NOINTERFACE;
    *out = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP GetType(GUID*) override { return E_NOTIMPL; }
  STDMETHODIMP EnumRanges(TfEditCookie,IEnumTfRanges**,ITfRange*) override { return E_NOTIMPL; }
  STDMETHODIMP GetValue(TfEditCookie,ITfRange*,VARIANT* out) override {
    VariantInit(out); out->vt = type;
    if (type == VT_UNKNOWN && value) { out->punkVal=value; value->AddRef(); }
    return result;
  }
  STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }
};
class ScopeCompartment : public ITfCompartment {
 public:
  ULONG refs=1;
  HRESULT result=S_FALSE;
  VARTYPE type=VT_EMPTY;
  LONG value=0;
  STDMETHODIMP QueryInterface(REFIID id,void** out) override {
    *out=nullptr;if(id!=IID_IUnknown && id!=IID_ITfCompartment)return E_NOINTERFACE;
    *out=this;AddRef();return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override {return ++refs;}
  STDMETHODIMP_(ULONG) Release() override {return --refs;}
  STDMETHODIMP GetValue(VARIANT* out) override {VariantInit(out);out->vt=type;if(type==VT_I4)out->lVal=value;return result;}
  STDMETHODIMP SetValue(TfClientId,const VARIANT*) override {return E_NOTIMPL;}
};
class ScopeContext : public CompositionContext, public ITfCompartmentMgr {
 public:
  explicit ScopeContext(CompositionRange& range) : CompositionContext(range) {}
  HRESULT result = S_OK;
  HRESULT statusResult=S_OK,compartmentResult=S_OK;
  DWORD flags=0;
  bool provideKeyboard=true,missingCompartment=false;
  ScopeCompartment disabled,empty;
  ITfReadOnlyProperty* property = nullptr;
  STDMETHODIMP QueryInterface(REFIID id,void** out) override {
    if(id==IID_ITfCompartmentMgr){*out=nullptr;if(!provideKeyboard)return E_NOINTERFACE;
      *out=static_cast<ITfCompartmentMgr*>(this);AddRef();return S_OK;}
    return CompositionContext::QueryInterface(id,out);
  }
  STDMETHODIMP_(ULONG) AddRef() override {return CompositionContext::AddRef();}
  STDMETHODIMP_(ULONG) Release() override {return CompositionContext::Release();}
  STDMETHODIMP GetStatus(TF_STATUS* out) override {*out={};out->dwDynamicFlags=flags;return statusResult;}
  STDMETHODIMP GetCompartment(REFGUID id,ITfCompartment** out) override {
    *out=nullptr;if(compartmentResult!=S_OK || missingCompartment)return compartmentResult;
    if(id==GUID_COMPARTMENT_KEYBOARD_DISABLED)*out=&disabled;
    else if(id==GUID_COMPARTMENT_EMPTYCONTEXT)*out=&empty;
    else return E_INVALIDARG;
    (*out)->AddRef();return S_OK;
  }
  STDMETHODIMP ClearCompartment(TfClientId,REFGUID) override {return E_NOTIMPL;}
  STDMETHODIMP EnumCompartments(IEnumGUID**) override {return E_NOTIMPL;}
  STDMETHODIMP GetAppProperty(REFGUID id,ITfReadOnlyProperty** out) override {
    *out=nullptr;
    if (id != GUID_PROP_INPUTSCOPE) return E_INVALIDARG;
    if (result == S_OK && property) { *out=property; property->AddRef(); }
    return result;
  }
};
static bool TestCorrectionScope() {
  using namespace correction;
  unsigned checks=0,failures=0;
  auto check=[&](bool good,const char* name){++checks;if(!good)++failures;std::cout<<(good?"PASS ":"FAIL ")<<name<<'\n';};
  CompositionRange range; ScopeContext context(range); ScopeProperty property; ScopeFixture input;
  context.property=&property; property.value=&input;
  auto read=[&]{return InspectInputScope(&context,1,&range);};
  check(static_cast<bool>(read()),"explicit text scope allowed");
  check(InspectInputScope(nullptr,1,&range).stage==ScopeStage::MissingTarget &&
        InspectInputScope(&context,1,nullptr).error==E_POINTER,"null scope targets rejected");
  context.result=E_ACCESSDENIED;
  check(read().stage==ScopeStage::PropertyQuery && read().error==E_ACCESSDENIED,"property query error preserved");
  context.result=S_FALSE; check(read().stage==ScopeStage::PropertyQuery,"S_FALSE property query rejected");
  context.result=S_OK;context.property=nullptr;
  check(read().stage==ScopeStage::MissingProperty,"missing property distinguished");
  context.property=&property;property.result=TF_E_NOLOCK;
  check(read().stage==ScopeStage::PropertyValue && read().error==TF_E_NOLOCK,"property value failure releases returned interface");
  property.result=S_FALSE;check(read().stage==ScopeStage::PropertyValue,"S_FALSE property value rejected");
  property.result=S_OK;property.type=VT_EMPTY;
  check(read().stage==ScopeStage::ValueType && read().variantType==VT_EMPTY,"empty value distinguished from default scope");
  property.type=VT_I4;check(read().stage==ScopeStage::ValueType,"non-interface variant rejected");
  property.type=VT_UNKNOWN;property.value=nullptr;
  check(read().stage==ScopeStage::ValueType,"null unknown value rejected");
  property.value=&input;input.query=E_NOINTERFACE;
  check(read().stage==ScopeStage::ScopeInterface && read().error==E_NOINTERFACE,"missing scope interface diagnosed");
  input.query=S_OK;input.result=E_FAIL;
  check(read().stage==ScopeStage::ScopeQuery && read().error==E_FAIL,"scope query error distinguished and output freed");
  input.result=S_FALSE;check(read().stage==ScopeStage::ScopeQuery,"S_FALSE scope query rejected");
  input.result=S_OK;input.nullArray=true;
  check(read().stage==ScopeStage::EmptyScopes,"null scope array rejected");
  input.nullArray=false;input.scopes.clear();check(read().stage==ScopeStage::EmptyScopes,"empty scope array rejected");
  input.scopes.assign(65,IS_TEXT);check(read().stage==ScopeStage::TooManyScopes,"oversized list rejected before scanning");
  input.scopes={IS_DEFAULT};
  check(read().stage==ScopeStage::UnsupportedScope && read().scope==IS_DEFAULT,"default scope diagnosed without broadening access");
  auto local=[&]{return InspectInputScope(&context,1,&range,ScopeUse::LocalCommit);};
  check(static_cast<bool>(local()),"explicit default scope allowed for tracked local IME commits only");
  input.scopes={IS_DEFAULT,IS_TEXT};check(static_cast<bool>(local()),"default and text combination allowed for local commits");
  input.scopes={IS_DEFAULT,IS_URL};check(local().stage==ScopeStage::UnsupportedScope,"default cannot override URL scope");
  input.scopes={IS_DEFAULT};
  property.type=VT_EMPTY;check(local().stage==ScopeStage::ValueType,"missing input type is not treated as explicit default");property.type=VT_UNKNOWN;
  context.provideKeyboard=false;check(local().stage==ScopeStage::KeyboardUnavailable,"unknown keyboard manager remains blocked");context.provideKeyboard=true;
  context.compartmentResult=E_FAIL;check(local().stage==ScopeStage::KeyboardUnavailable,"keyboard compartment failure remains blocked");context.compartmentResult=S_OK;
  context.missingCompartment=true;check(local().stage==ScopeStage::KeyboardUnavailable,"null keyboard compartment rejected");context.missingCompartment=false;
  context.disabled.result=S_OK;context.disabled.type=VT_I4;context.disabled.value=1;
  context.empty.result=S_OK;context.empty.type=VT_I4;context.empty.value=0;
  check(local().stage==ScopeStage::KeyboardDisabled,"empty-context false cannot override keyboard-disabled true");
  context.disabled.value=0;context.empty.value=1;
  check(local().stage==ScopeStage::EmptyContext,"empty context independently blocks local commits");context.empty.value=0;
  context.disabled.type=VT_BSTR;check(local().stage==ScopeStage::KeyboardUnavailable,"unexpected keyboard value type remains blocked");
  context.disabled.type=VT_EMPTY;context.disabled.result=E_FAIL;
  check(local().stage==ScopeStage::KeyboardUnavailable,"failed empty keyboard value is not treated as unset");
  context.disabled.result=S_FALSE;context.empty.result=S_FALSE;context.empty.type=VT_EMPTY;
  check(static_cast<bool>(local()),"documented unset keyboard flags allow ordinary local commits");
  context.flags=TF_SD_READONLY;check(local().stage==ScopeStage::ReadOnly,"readonly context rejected before scope read");context.flags=0;
  context.statusResult=E_FAIL;check(local().stage==ScopeStage::StatusUnavailable,"unknown host status remains blocked");context.statusResult=S_OK;
  input.scopes={IS_URL};check(read().stage==ScopeStage::UnsupportedScope && read().scope==IS_URL,"URL scope remains blocked");
  for(auto scope:{IS_PASSWORD,IS_NUMERIC_PASSWORD,IS_PRIVATE,IS_NUMERIC_PIN,IS_ALPHANUMERIC_PIN,IS_ALPHANUMERIC_PIN_SET}) {
    input.scopes={IS_TEXT,IS_DEFAULT,scope};
    check(ProtectedScope(read()) && read().scope==scope && ProtectedScope(local()),"protected scope overrides text and default scopes in both policies");
  }
  input.scopes={static_cast<InputScope>(10000)};
  check(read().stage==ScopeStage::UnsupportedScope,"unknown enum remains blocked");
  input.scopes={IS_TEXT,IS_TEXT};check(static_cast<bool>(read()),"all text multi-scope allowed");
  const auto verified=+[](ITfContext*,ITfRange*){return true;};
  auto verifiedLocal=[&]{return InspectInputScope(&context,1,&range,ScopeUse::LocalCommit,verified);};
  property.result=E_FAIL;property.type=VT_EMPTY;
  check(verifiedLocal().stage==ScopeStage::NativePlainText && static_cast<bool>(verifiedLocal()),
      "independently verified native plaintext allows only local commits after empty E_FAIL");
  check(InspectInputScope(&context,1,&range,ScopeUse::NearbyText,verified).stage==ScopeStage::PropertyValue,
      "native plaintext does not grant nearby or arbitrary history access");
  check(InspectInputScope(&context,1,&range,ScopeUse::LocalCommit,nullptr).stage==ScopeStage::PropertyValue,
      "missing native verifier never broadens an unavailable scope");
  property.result=S_FALSE;check(verifiedLocal().stage==ScopeStage::PropertyValue,"S_FALSE does not enter native fallback");
  property.result=TF_E_NOLOCK;check(verifiedLocal().stage==ScopeStage::PropertyValue,"invalid lock does not enter native fallback");
  property.result=E_FAIL;property.type=VT_UNKNOWN;
  check(verifiedLocal().stage==ScopeStage::PropertyValue,"partial interface from failed query does not enter native fallback");
  property.result=S_OK;input.scopes={IS_PRIVATE};
  check(verifiedLocal().stage==ScopeStage::ProtectedScope,"explicit private scope defeats native verification");
  property.result=E_FAIL;property.type=VT_EMPTY;context.flags=TF_SD_READONLY;
  check(verifiedLocal().stage==ScopeStage::ReadOnly,"readonly status defeats native verification");context.flags=0;
  const auto word=+[](ITfContext*,ITfRange*)->DWORD{return WordVerified;};
  auto verifiedDocument=[&]{return InspectInputScope(&context,1,&range,ScopeUse::LocalCommit,nullptr,word);};
  check(verifiedDocument().stage==ScopeStage::WordDocument,"verified Word metadata allows only tracked local commits on empty E_FAIL");
  check(InspectInputScope(&context,1,&range,ScopeUse::NearbyText,nullptr,word).stage==ScopeStage::PropertyValue,
      "Word verification grants no arbitrary history access");
  const auto incomplete=+[](ITfContext*,ITfRange*)->DWORD{return WordRange|WordView|WordPane|WordObject;};
  const auto rejected=InspectInputScope(&context,1,&range,ScopeUse::LocalCommit,nullptr,incomplete);
  check(!rejected && rejected.stage==ScopeStage::WordMetadataUnavailable && rejected.scope==(WordRange|WordView|WordPane|WordObject),
      "Word metadata failure preserves closed evidence flags instead of another generic scope failure");
  ScopeDiagnostic pending; pending.stage=ScopeStage::WordMetadataUnavailable;pending.error=E_FAIL;
  pending.variantType=VT_EMPTY;pending.scope=WordVerified&~WordUnrestricted;
  check(MayWaitForWordScope(pending) && !static_cast<bool>(pending),"Word quarantine eligibility is not permission to read or correct");
  for(DWORD evidence:{DWORD(WordRange),DWORD(WordView),DWORD(WordPane),DWORD(WordObject),DWORD(WordDocument),DWORD(WordWritable),DWORD(WordUnprotected),DWORD(WordStableFocus)}) {
    auto incompletePending=pending;incompletePending.scope&=~evidence;
    check(!MayWaitForWordScope(incompletePending),"every independent Word metadata capability is required before temporary retention");
  }
  for(auto stage:{ScopeStage::ProtectedScope,ScopeStage::ReadOnly,ScopeStage::PropertyValue,ScopeStage::UnsupportedScope}) {
    auto forbidden=pending;forbidden.stage=stage;check(!MayWaitForWordScope(forbidden),"unknown generic or forbidden input scope cannot enter Word quarantine");
  }
  property.result=TF_E_NOLOCK;check(!verifiedDocument(),"Word metadata cannot override missing edit lock");
  property.result=E_ACCESSDENIED;check(!verifiedDocument(),"Word metadata cannot override denied scope");
  property.result=S_OK;property.type=VT_UNKNOWN;input.scopes={IS_PRIVATE};
  check(verifiedDocument().stage==ScopeStage::ProtectedScope,"explicit private scope defeats Word metadata");
  property.result=E_FAIL;property.type=VT_EMPTY;context.flags=TF_SD_READONLY;
  check(verifiedDocument().stage==ScopeStage::ReadOnly,"readonly TSF context defeats Word metadata");context.flags=0;
  property.result=S_OK;property.type=VT_UNKNOWN;input.scopes={IS_TEXT};
  FocusView view;context.activeView=&view;
  view.window=CreateWindowExW(0,L"EDIT",L"",WS_POPUP|ES_PASSWORD,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
  check(view.window && read().stage==ScopeStage::PasswordStyle,"password window style overrides ordinary text scope");
  if(view.window)DestroyWindow(view.window);context.activeView=nullptr;
  check(input.refs==1 && property.refs==1 && context.references==1 && range.references==1 && view.refs==1 && context.disabled.refs==1 && context.empty.refs==1,
      "scope checks balance all COM references without reading text");
  std::cout<<"Scope checks = "<<checks<<", failures = "<<failures<<'\n';return TestDocumentScope() && failures==0;
}
