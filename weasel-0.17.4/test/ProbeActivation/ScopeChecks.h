#pragma once
#include <vector>

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
class ScopeContext : public CompositionContext {
 public:
  explicit ScopeContext(CompositionRange& range) : CompositionContext(range) {}
  HRESULT result = S_OK;
  ITfReadOnlyProperty* property = nullptr;
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
  input.scopes={IS_URL};check(read().stage==ScopeStage::UnsupportedScope && read().scope==IS_URL,"URL scope remains blocked");
  for(auto scope:{IS_PASSWORD,IS_NUMERIC_PASSWORD,IS_PRIVATE,IS_NUMERIC_PIN,IS_ALPHANUMERIC_PIN,IS_ALPHANUMERIC_PIN_SET}) {
    input.scopes={IS_TEXT,IS_DEFAULT,scope};
    check(ProtectedScope(read()) && read().scope==scope,"protected scope overrides text and unsupported scopes");
  }
  input.scopes={static_cast<InputScope>(10000)};
  check(read().stage==ScopeStage::UnsupportedScope,"unknown enum remains blocked");
  input.scopes={IS_TEXT,IS_TEXT};check(static_cast<bool>(read()),"all text multi-scope allowed");
  FocusView view;context.activeView=&view;
  view.window=CreateWindowExW(0,L"EDIT",L"",WS_POPUP|ES_PASSWORD,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
  check(view.window && read().stage==ScopeStage::PasswordStyle,"password window style overrides ordinary text scope");
  if(view.window)DestroyWindow(view.window);context.activeView=nullptr;
  check(input.refs==1 && property.refs==1 && context.references==1 && range.references==1 && view.refs==1,
      "scope checks balance all COM references without reading text");
  std::cout<<"Scope checks = "<<checks<<", failures = "<<failures<<'\n';return failures==0;
}
