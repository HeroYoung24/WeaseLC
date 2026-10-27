// Simulate the product host decision only. Production cache, polling, hint,
// preedit and key paths still execute over our owned TSF document.
for(unsigned scenario=0;scenario<5;++scenario) {
  ComPtr<MemoryTextStore> host;host.Attach(new MemoryTextStore(scenario==1?IS_DEFAULT:IS_TEXT));
  if(scenario==2)host->attrsAtPositionResult=E_NOTIMPL;
  ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> context;TfEditCookie ignored=0;
  const bool made=manager->CreateDocumentMgr(&document)==S_OK &&
      document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&ignored)==S_OK &&
      document->Push(context.Get())==S_OK;
  check(made,"input-only policy uses its own synthetic document");if(!made)continue;
  auto options=preferences;options.automatic.wordLocalCompatibility=true;options.timing=1;
  check(configure(service.Get(),&options,client,L"")==S_OK,"input-only fixture includes retired opt-in data");
  if(scenario==3 || scenario==4) {
    check(Commit(apply,service.Get(),context.Get(),client,L"这里有错子。") && inject(service.Get(),&reply)==S_OK,
        "policy transition fixture starts with a valid older hint");
    if(scenario==4){BOOL eaten=FALSE;key(service.Get(),context.Get(),VK_TAB,0,1,&eaten);}
  }
  check(scopeWait(service.Get(),4)==S_OK,"host decision switches to ordinary-input-only");
  if(scenario<3)check(Commit(apply,service.Get(),context.Get(),client,L"这里有错子。"),
      "normal Chinese commits still reach the host in input-only mode");
  const auto documentText=host->text;const auto reads=host->textReads;const auto attrs=host->attrsAtPositionCalls;
  poll(service.Get());
  DWORD units=MAXDWORD;cachedUnits(service.Get(),&units);
  DWORD ready=1;HWND hint=nullptr;status(service.Get(),&ready,&hint);
  DWORD child=1;environment(service.Get(),TRUE,&child);
  AutomaticDiagnostic diagnostic;memoryDiagnostic(service.Get(),&diagnostic);
  check(!units && !ready && !IsWindowVisible(hint) && !child &&
      diagnostic.stage==AutomaticStage::InputOnlyHost && host->text==documentText,
      "Word fallback clears only correction state, with no cache model hint or document mutation");
  bool passThrough=true;
  for(auto code:{VK_TAB,VK_ESCAPE,VK_BACK,VK_LEFT})for(DWORD operation:{1U,0U,2U}) {
    BOOL eaten=TRUE;
    passThrough=key(service.Get(),context.Get(),code,0,operation,&eaten)==S_FALSE && !eaten && passThrough;
  }
  check(passThrough && host->textReads==reads && host->attrsAtPositionCalls==attrs,
      "correction does not read scope/text or consume normal input and navigation keys");
  check(inject(service.Get(),&reply)==S_FALSE,"fixed/model suggestion injection cannot bypass input-only policy");
  check(Commit(apply,service.Get(),context.Get(),client,L"你好") && host->text==documentText+L"你好",
      "following Chinese candidate remains intact with correction disabled");
  invalidate(service.Get());document->Pop(TF_POPF_ALL);
}
