// Exercise production opt-in policy, accumulation, composition and Tab using
// real TSF locks. Only the native Word capability proof is a test substitution.
using WordEvidence = HRESULT(WINAPI*)(ITfTextInputProcessorEx*,DWORD);
auto wordEvidence = reinterpret_cast<WordEvidence>(GetProcAddress(module,"WeaselProbeAutomaticWordEvidence"));
check(wordEvidence!=nullptr,"Word capability test hook is probe-only");
if (!wordEvidence) return false;
for(unsigned scenario=0;scenario<9;++scenario) {
  ComPtr<MemoryTextStore> host;host.Attach(new MemoryTextStore(IS_TEXT));
  host->attrsAtPositionResult=E_NOTIMPL;
  ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> context;TfEditCookie ignored=0;
  const bool made=manager->CreateDocumentMgr(&document)==S_OK &&
      document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&ignored)==S_OK &&
      document->Push(context.Get())==S_OK;
  check(made,"Word compatibility owns its synthetic document");if(!made)continue;
  auto options=preferences;options.automatic.wordLocalCompatibility=scenario!=0;
  if(scenario==7)options.mode=Mode::Online;
  if(scenario==8)options.automatic.enabled=false;
  options.timing=0;
  check(configure(service.Get(),&options,client,L"")==S_OK &&
      wordEvidence(service.Get(),WordLocalCommitVerified)==S_OK &&
      Commit(apply,service.Get(),context.Get(),client,L"这里有"),"own commit reaches Word compatibility policy");
  auto units=[&]{DWORD count=MAXDWORD;cachedUnits(service.Get(),&count);return count;};
  auto edit=[&](std::function<HRESULT(TfEditCookie)> body){
    ComPtr<ITfEditSession> session;session.Attach(new AutomaticHostEdit(std::move(body)));HRESULT result=E_FAIL;
    return context->RequestEditSession(client,session.Get(),TF_ES_SYNC|TF_ES_READWRITE,&result)==S_OK && result==S_OK;
  };
  if(scenario>=7) {
    BOOL eaten=TRUE;
    check(units()==0 && inject(service.Get(),&reply)==S_FALSE &&
        key(service.Get(),context.Get(),VK_TAB,0,1,&eaten)==S_FALSE && !eaten,
        "online mode or disabled automatic hints cannot activate Word compatibility");
  } else if(scenario==0) {
    AutomaticDiagnostic observed;memoryDiagnostic(service.Get(),&observed);
    check(units()==3 && observed.stage==AutomaticStage::WaitingScope && inject(service.Get(),&reply)==S_FALSE,
        "without opt-in unavailable IRM remains unverified quarantine");
  } else {
    ULONGLONG deadline=1;scopeDeadline(service.Get(),&deadline);
    check(units()==3 && !deadline && Commit(apply,service.Get(),context.Get(),client,L"错子。") && units()==6,
        "opt-in accumulates own adjacent commits despite persistent unavailable IRM");
    if(scenario==1) {
      check(inject(service.Get(),&reply)==S_OK && edit([&](TfEditCookie ec){return preedit(service.Get(),context.Get(),ec,L"ni");}),
          "compatible full context accepts a suggestion and preserves owned composition tracking");
      BOOL eaten=FALSE;key(service.Get(),context.Get(),VK_TAB,0,1,&eaten);key(service.Get(),context.Get(),VK_TAB,0,0,&eaten);
      check(eaten && host->text==L"这里有错字。ni","compatible guarded Tab corrects text while retaining ni");
      key(service.Get(),context.Get(),VK_TAB,0,2,&eaten);
      check(Commit(apply,service.Get(),context.Get(),client,L"你好") && host->text==L"这里有错字。你好",
          "compatible Tab preserves subsequent candidate commit without duplicates");
    } else {
      if(scenario==2){host->attrsAtPositionResult=S_OK;host->ChangeScope(IS_PRIVATE);}
      if(scenario==3)host->ChangeStatus(TS_SD_READONLY);
      if(scenario==4)host->UserSelect(0,0);
      if(scenario==5)host->UserReplace(0,1,L"那");
      if(scenario==6){wordEvidence(service.Get(),WordRange|WordView|WordPane|WordObject|WordDocument|WordStableFocus);}
      scopeWait(service.Get(),3);poll(service.Get());
      BOOL eaten=TRUE;
      check(units()==0 && inject(service.Get(),&reply)==S_FALSE &&
          key(service.Get(),context.Get(),VK_TAB,0,1,&eaten)==S_FALSE && !eaten,
          "compatibility still retires private readonly selection source changes and lost document capabilities");
    }
  }
  invalidate(service.Get());document->Pop(TF_POPF_ALL);
}
