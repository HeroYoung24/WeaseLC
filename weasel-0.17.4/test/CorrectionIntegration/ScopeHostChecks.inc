// Use real Windows TSF over an owned text store. No DLL, TIP registration,
// foreground window, document text capture or model is involved.
#include <CorrectionScope.h>
#include <CorrectionScopeTrace.h>
static bool RunScopeHostChecks() {
  unsigned checks=0, failures=0;
  auto check=[&](bool ok,const char* name){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
  ComPtr<ITfThreadMgrEx> manager; TfClientId client=TF_CLIENTID_NULL;
  if(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager))!=S_OK ||
      manager->ActivateEx(&client,TF_TMAE_NOACTIVATETIP)!=S_OK) return false;
  for(unsigned fault=0;fault<4;++fault) for(unsigned shape=0;shape<4;++shape) {
    ComPtr<MemoryTextStore> host;host.Attach(new MemoryTextStore(IS_TEXT));host->text=L"scope-test";
    ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> context;TfEditCookie createdCookie=0;
    const bool made=manager->CreateDocumentMgr(&document)==S_OK &&
        document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&createdCookie)==S_OK &&
        document->Push(context.Get())==S_OK;
    check(made,"scope diagnostic host created without TIP activation");
    if(!made)continue;
    if(fault==1)host->attrTransitionResult=E_NOTIMPL;
    if(fault==2)host->attrsAtPositionResult=E_NOTIMPL;
    if(fault==3)host->retrieveAttrsResult=E_FAIL;
    ComPtr<ITfEditSession> session;
    session.Attach(new AutomaticHostEdit([&](TfEditCookie cookie) {
      ComPtr<ITfRange> range,end;HRESULT hr=context->GetStart(cookie,&range);
      if(hr!=S_OK || context->GetEnd(cookie,&end)!=S_OK)return E_FAIL;
      if(shape==0)hr=range->ShiftEndToRange(cookie,end.Get(),TF_ANCHOR_END);
      if(shape==2)hr=range->ShiftStartToRange(cookie,end.Get(),TF_ANCHOR_END);
      if(shape==3){LONG moved=0;hr=range->ShiftEnd(cookie,1,&moved,nullptr);if(moved!=1)return E_FAIL;}
      if(hr!=S_OK)return hr;
      // Let TSF initialize its selection/cache before measuring trace effects.
      TF_SELECTION warm{};ULONG fetched=0;
      const auto warmed=context->GetSelection(cookie,TF_DEFAULT_SELECTION,1,&warm,&fetched);
      if(warm.range)warm.range->Release();
      if(warmed!=S_OK || fetched!=1)return E_FAIL;
      ComPtr<ITfContext> rangeContext;range->GetContext(&rangeContext);
      ComPtr<ITfReadOnlyProperty> property;hr=context->GetAppProperty(GUID_PROP_INPUTSCOPE,&property);
      if(hr!=S_OK || !property)return E_FAIL;
      VARIANT value;VariantInit(&value);const HRESULT queried=property->GetValue(cookie,range.Get(),&value);
      const auto type=value.vt;VariantClear(&value);
      const auto scope=InspectInputScope(context.Get(),cookie,range.Get(),ScopeUse::LocalCommit);
      const auto reads=host->textReads;
      const auto trace=TraceInputScope(context.Get(),cookie,range.Get(),1);
      std::cout<<"TRACE flags="<<trace.flags<<" context="<<std::hex<<static_cast<unsigned>(trace.rangeContext)
        <<" lock="<<static_cast<unsigned>(trace.selectionLock)<<" original="<<static_cast<unsigned>(trace.original)
        <<" start="<<static_cast<unsigned>(trace.start)<<" end="<<static_cast<unsigned>(trace.end)
        <<" selection="<<static_cast<unsigned>(trace.selection)<<std::dec<<" textReadsBefore="<<reads<<" after="<<host->textReads<<'\n';
      check(trace.version==1 && trace.phase==1 && (trace.flags&1) && trace.selectionLock==S_OK &&
          trace.original==queried && trace.start==queried && trace.end==queried && trace.selection==queried &&
          sizeof(trace)==40 && host->text==L"scope-test",
          "scope trace checks identity lock and point queries without changing or recording document text");
      ComPtr<IEnumTfRanges> ranges;const auto enumerated=property->EnumRanges(cookie,&ranges,range.Get());
      std::cout<<"SCOPE_HOST fault="<<fault<<" shape="<<shape<<" sameContext="<<SameIdentity(context.Get(),rangeContext.Get())
        <<" value=0x"<<std::hex<<static_cast<unsigned>(queried)<<" enum=0x"<<static_cast<unsigned>(enumerated)<<std::dec
        <<" vt="<<type<<" stage="<<static_cast<unsigned>(scope.stage)<<" attrs="<<host->attrsAtPositionCalls
        <<" retrieve="<<host->retrieveAttrsCalls<<" transitions="<<host->attrTransitionCalls<<" scopeCalls="<<host->inputScopeCalls<<'\n';
      check(SameIdentity(context.Get(),rangeContext.Get()),"target range belongs to the granted edit context");
      if(!fault)check(queried==S_OK && type==VT_UNKNOWN && static_cast<bool>(scope),"valid host scope works for full point and unit ranges");
      else if(fault==1)check(queried==S_OK && static_cast<bool>(scope) && enumerated!=S_OK,
          "missing transition enumeration does not explain a GetValue failure");
      else check(!scope && queried==E_FAIL && type==VT_EMPTY,
          "position or retrieval failure reproduces GetValue E_FAIL without scope or text access");
      return S_OK;
    }));
    HRESULT edited=E_FAIL;const HRESULT requested=context->RequestEditSession(client,session.Get(),TF_ES_SYNC|TF_ES_READWRITE,&edited);
    check(requested==S_OK && edited==S_OK,"scope queries run under a fresh granted edit cookie");
    document->Pop(TF_POPF_ALL);
  }
  check(manager->Deactivate()==S_OK,"scope-only manager deactivates");
  std::cout<<"Scope host checks = "<<checks<<", failures = "<<failures<<'\n';return !failures;
}
