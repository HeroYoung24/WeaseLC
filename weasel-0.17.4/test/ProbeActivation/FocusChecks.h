#pragma once
#include "../../include/CorrectionFocus.h"
#include "../../include/CorrectionDiagnostics.h"

class FocusDocument : public ITfDocumentMgr {
 public:
  ULONG refs = 1;
  HRESULT result = S_OK;
  ITfContext* context = nullptr;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER; *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfDocumentMgr) return E_NOINTERFACE;
    *out = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP GetTop(ITfContext** out) override {
    *out = nullptr;
    if (result == S_OK && context) { *out = context; context->AddRef(); }
    return result;
  }
  STDMETHODIMP GetBase(ITfContext** out) override { return GetTop(out); }
  STDMETHODIMP CreateContext(TfClientId,DWORD,IUnknown*,ITfContext**,TfEditCookie*) override { return E_NOTIMPL; }
  STDMETHODIMP Push(ITfContext*) override { return E_NOTIMPL; }
  STDMETHODIMP Pop(DWORD) override { return E_NOTIMPL; }
  STDMETHODIMP EnumContexts(IEnumTfContexts**) override { return E_NOTIMPL; }
};
class FocusManager : public FailingThreadMgr {
 public:
  FocusManager() : FailingThreadMgr(Failure::ThreadSink) {}
  HRESULT result = S_OK;
  ITfDocumentMgr* document = nullptr;
  STDMETHODIMP GetFocus(ITfDocumentMgr** out) override {
    *out = nullptr;
    if (result == S_OK && document) { *out = document; document->AddRef(); }
    return result;
  }
};
// A legal second ITfContext address with the same controlling IUnknown.
class ContextAlias : public CompositionContext {
 public:
  ContextAlias(CompositionRange& range, CompositionContext& owner) : CompositionContext(range), owner_(owner) {}
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    if (id == IID_IUnknown) return owner_.QueryInterface(id, out);
    return CompositionContext::QueryInterface(id, out);
  }
  STDMETHODIMP GetActiveView(ITfContextView** view) override { return owner_.GetActiveView(view); }
  STDMETHODIMP GetSelection(TfEditCookie c,ULONG a,ULONG b,TF_SELECTION* s,ULONG* n) override { return owner_.GetSelection(c,a,b,s,n); }
  STDMETHODIMP SetSelection(TfEditCookie c,ULONG n,const TF_SELECTION* s) override { return owner_.SetSelection(c,n,s); }
 private:
  CompositionContext& owner_;
};
class FocusView : public ITfContextView {
 public:
  ULONG refs = 1;
  HWND window = nullptr;
  HRESULT result = S_OK;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER; *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfContextView) return E_NOINTERFACE;
    *out = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP GetWnd(HWND* out) override { *out = window; return result; }
  STDMETHODIMP GetRangeFromPoint(TfEditCookie,const POINT*,DWORD,ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetTextExt(TfEditCookie,ITfRange*,RECT*,BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetScreenExt(RECT*) override { return E_NOTIMPL; }
};

static bool TestCorrectionFocus(HMODULE module, REFCLSID clsid) {
  using namespace correction;
  unsigned checks=0, failures=0;
  auto check=[&](bool good,const char* name){++checks;if(!good)++failures;std::cout<<(good?"PASS ":"FAIL ")<<name<<'\n';};
  CompositionRange range, otherRange;
  CompositionContext context(range), other(otherRange);
  ContextAlias alias(range, context);
  FocusDocument document; document.context=&alias;
  ComPtr<FocusManager> manager; manager.Attach(new FocusManager()); manager->document=&document;
  check(static_cast<ITfContext*>(&alias) != static_cast<ITfContext*>(&context) && SameIdentity(&alias,&context),
      "distinct context interface addresses have one COM identity");
  check(static_cast<bool>(CheckContextFocus(manager.Get(),&context)),"production focus check accepts context alias");
  check(CheckContextFocus(manager.Get(),&other).stage==FocusStage::DifferentContext,"different document context remains rejected");
  check(CheckContextFocus(nullptr,&context).stage==FocusStage::MissingManager,"missing thread manager diagnosed");
  check(CheckContextFocus(manager.Get(),nullptr).stage==FocusStage::MissingContext,"missing context diagnosed");
  manager->result=E_FAIL;
  auto failed=CheckContextFocus(manager.Get(),&context);
  check(failed.stage==FocusStage::QueryFailed && failed.error==E_FAIL,"GetFocus HRESULT retained");
  manager->result=S_FALSE;
  check(CheckContextFocus(manager.Get(),&context).stage==FocusStage::NoDocument,"no focused document is not treated as safe");
  manager->result=S_OK; manager->document=nullptr;
  check(CheckContextFocus(manager.Get(),&context).stage==FocusStage::NoDocument,"null focus output rejected");
  manager->document=&document; document.result=TF_E_DISCONNECTED;
  check(CheckContextFocus(manager.Get(),&context).stage==FocusStage::TopFailed,"GetTop failure distinguished");
  document.result=S_OK; document.context=nullptr;
  check(CheckContextFocus(manager.Get(),&context).stage==FocusStage::NoTop,"empty context stack rejected");
  document.context=&alias;
  check(CheckContextFocus(manager.Get(),&context,true).stage==FocusStage::ViewFailed,"unavailable view distinguished");
  FocusView view; context.activeView=&view;
  check(CheckContextFocus(manager.Get(),&context,true).stage==FocusStage::WindowFailed,"null host window rejected");
  HWND root=CreateWindowExW(0,L"STATIC",L"owned focus fixture",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
  HWND child=CreateWindowExW(0,L"STATIC",L"owned child",WS_CHILD,0,0,10,10,root,nullptr,nullptr,nullptr);
  HWND foreign=CreateWindowExW(0,L"STATIC",L"other fixture",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
  view.window=child;
  check(root && child && static_cast<bool>(CheckContextFocus(manager.Get(),&context,true,root)),"owned child and foreground root match without activating a TIP");
  check(CheckContextFocus(manager.Get(),&context,true,foreign).stage==FocusStage::DifferentWindow,"foreign foreground window rejected");
  check(CheckContextFocus(manager.Get(),&context,true,nullptr).stage==FocusStage::DifferentWindow,"missing foreground cannot pass through null-root equality");
  DestroyWindow(child);DestroyWindow(root);DestroyWindow(foreign); context.activeView=nullptr;
  check(view.refs==1 && document.refs==1 && context.references==1 && alias.references==1,"focus query references balanced");

  using Bound=HRESULT(WINAPI*)(ITfTextInputProcessorEx*,ITfContext*,HRESULT(WINAPI*)(ITfContext*,TfEditCookie),ITfEditSession**);
  auto bound=reinterpret_cast<Bound>(GetProcAddress(module,"WeaselProbeCreateBoundEditSession"));
  auto get=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(module,"DllGetClassObject"));
  ComPtr<IClassFactory> factory; ComPtr<ITfTextInputProcessorEx> service;
  if(!bound || !get || get(clsid,IID_IClassFactory,reinterpret_cast<void**>(factory.GetAddressOf()))!=S_OK ||
      factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf()))!=S_OK) return false;
  ComPtr<ITfEditSession> old, next;
  check(bound(service.Get(),&context,ObserveBoundContext,&old)==S_OK && bound(service.Get(),&alias,ObserveBoundContext,&next)==S_OK &&
      old->DoEditSession(1)==S_OK && next->DoEditSession(2)==S_OK,"context alias cannot invalidate queued work from the same document");
  using SetKeys=HRESULT(WINAPI*)(ITfTextInputProcessorEx*,void(WINAPI*)(WPARAM,LPARAM,BOOL*,void*),void*);
  auto setKeys=reinterpret_cast<SetKeys>(GetProcAddress(module,"WeaselProbeSetKeySink"));
  ComPtr<ITfKeyEventSink> keys; service.As(&keys);
  unsigned calls=0;BOOL eaten=FALSE;
  if(!setKeys) return false;
  setKeys(service.Get(),CountSyntheticKey,&calls);
  keys->OnTestKeyDown(&context,'A',1,&eaten);keys->OnKeyDown(&alias,'A',1,&eaten);
  check(calls==1 && eaten,"query and actual key callbacks through aliases dispatch only once");
  setKeys(service.Get(),nullptr,nullptr);
  service->Deactivate();old.Reset();next.Reset();keys.Reset();service.Reset();
  check(context.references==1 && alias.references==1,"alias binding teardown releases both interface references");

  const auto key=std::wstring(L"Software\\WeaseLC-Hint-Diagnostics-Test-")+std::to_wstring(GetCurrentProcessId());
  const auto childKey=key+L"\\Diagnostics";
  AutomaticDiagnostic sample;sample.stage=AutomaticStage::Captured;
  SaveAutomaticDiagnostic(sample,childKey.c_str());
  HKEY parent=nullptr; RegOpenKeyExW(HKEY_CURRENT_USER,key.c_str(),0,KEY_QUERY_VALUE,&parent);
  FILETIME before{},after{};
  if(parent) RegQueryInfoKeyW(parent,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&before);
  Sleep(20);sample.stage=AutomaticStage::WorkerStarted;SaveAutomaticDiagnostic(sample,childKey.c_str());
  if(parent){RegQueryInfoKeyW(parent,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&after);RegCloseKey(parent);}
  ScopeDiagnostic scope;scope.stage=ScopeStage::UnsupportedScope;scope.scope=IS_DEFAULT;scope.variantType=VT_UNKNOWN;
  SaveScopeDiagnostic(scope,childKey.c_str());
  const auto loadedScope=LoadScopeDiagnostic(childKey.c_str());
  check(sizeof(ScopeDiagnostic)==20 && loadedScope.stage==ScopeStage::UnsupportedScope && loadedScope.scope==IS_DEFAULT,
      "fixed-size scope metadata round trips without text");
  HKEY diagnostics=nullptr;
  RegOpenKeyExW(HKEY_CURRENT_USER,childKey.c_str(),0,KEY_SET_VALUE,&diagnostics);
  auto corrupt=scope;corrupt.version=2;
  if(diagnostics) RegSetValueExW(diagnostics,L"LastScope",0,REG_BINARY,reinterpret_cast<const BYTE*>(&corrupt),sizeof(corrupt));
  check(LoadScopeDiagnostic(childKey.c_str()).stage==ScopeStage::NotRun,"unknown scope diagnostic version rejected");
  corrupt=scope;corrupt.stage=static_cast<ScopeStage>(999);
  if(diagnostics) RegSetValueExW(diagnostics,L"LastScope",0,REG_BINARY,reinterpret_cast<const BYTE*>(&corrupt),sizeof(corrupt));
  check(LoadScopeDiagnostic(childKey.c_str()).stage==ScopeStage::NotRun,"invalid scope diagnostic enum rejected");
  if(diagnostics) { RegSetValueExW(diagnostics,L"LastScope",0,REG_BINARY,reinterpret_cast<const BYTE*>(&scope),sizeof(scope)-1);RegCloseKey(diagnostics); }
  check(LoadScopeDiagnostic(childKey.c_str()).stage==ScopeStage::NotRun,"truncated scope diagnostic rejected");
  SaveScopeDiagnostic(scope,childKey.c_str());
  HKEY settings=nullptr;RegOpenKeyExW(HKEY_CURRENT_USER,key.c_str(),0,KEY_QUERY_VALUE,&settings);
  if(settings){RegQueryInfoKeyW(settings,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&after);RegCloseKey(settings);}
  const auto loaded=LoadAutomaticDiagnostic(childKey.c_str());
  check(loaded.stage==AutomaticStage::WorkerStarted && CompareFileTime(&before,&after)==0,"diagnostic writes cannot invalidate settings timestamp");
  check(sizeof(AutomaticDiagnostic)==24 && (loaded.time.dwLowDateTime || loaded.time.dwHighDateTime),"diagnostic contains only enums HRESULT and timestamp");
  RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());
  HKEY remaining=nullptr;
  check(RegOpenKeyExW(HKEY_CURRENT_USER,key.c_str(),0,KEY_READ,&remaining)==ERROR_FILE_NOT_FOUND,"temporary diagnostic registry data removed");
  if(remaining) RegCloseKey(remaining);
  std::cout<<"Focus/diagnostic checks = "<<checks<<", failures = "<<failures<<'\n';
  return failures==0;
}
