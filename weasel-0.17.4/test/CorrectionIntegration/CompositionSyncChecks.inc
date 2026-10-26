// Behavioral final-composition synchronization model, NOT real Edge.
// Exercise production staging, not a test-only repair prototype.
for (const InputScope scope : {IS_TEXT, IS_DEFAULT}) for (const bool composing : {false, true}) {
  for (const std::wstring replacement : {L"字", L"错字", L"错误字词"}) {
    ComPtr<ITfTextInputProcessorEx> subject;
    ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore(scope));
    ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie ignored = 0;
    const bool made = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
        reinterpret_cast<void**>(subject.GetAddressOf())) == S_OK && manager->CreateDocumentMgr(&document) == S_OK &&
        document->CreateContext(client, 0, static_cast<ITextStoreACP*>(host.Get()), &context, &ignored) == S_OK &&
        document->Push(context.Get()) == S_OK;
    check(made, "projection owns its synthetic document and service");
    if (!made) continue;
    auto editOwned = [&](std::function<HRESULT(TfEditCookie)> body) {
      ComPtr<ITfEditSession> session; session.Attach(new AutomaticHostEdit(std::move(body)));
      HRESULT result = E_FAIL;
      return context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &result) == S_OK && result == S_OK;
    };
    Preferences options; options.mode = Mode::Local; options.timing = composing ? 0 : 1; options.automatic.enabled = true;
    auto suggestion = reply; auto& proposed = suggestion.items[0];
    proposed.replacementUnits = static_cast<unsigned>(replacement.size()); wcscpy_s(proposed.replacement, replacement.c_str());
    check(configure(subject.Get(), &options, client, L"") == S_OK &&
        Commit(apply, subject.Get(), context.Get(), client, L"这里有错子。") &&
        (!composing || editOwned([&](TfEditCookie ec) { return preedit(subject.Get(), context.Get(), ec, L"ni"); })) &&
        inject(subject.Get(), &suggestion) == S_OK, "projection prepares a production correction with or without preedit");
    {
      CompositionProjection projection(*host.Get(), context.Get());
      const auto expected = L"这里有" + replacement + L"。" + (composing ? L"ni" : L"");
      BOOL handled = FALSE;
      key(subject.Get(), context.Get(), VK_TAB, 0, 1, &handled);
      key(subject.Get(), context.Get(), VK_TAB, 0, 0, &handled);
      check(handled && projection.Valid() && projection.Visible() == expected && host->text == expected,
          "production correction survives final-composition synchronization, including length changes");
      key(subject.Get(), context.Get(), VK_TAB, 0, 0, &handled);
      check(handled && projection.Visible() == expected && host->text == expected, "repeat Tab never reapplies the correction");
      key(subject.Get(), context.Get(), VK_TAB, 0, 2, &handled);
      check(Commit(apply,subject.Get(),context.Get(),client,L"你好") && projection.Valid() &&
          projection.Visible() == L"这里有" + replacement + L"。你好",
          "next candidate replaces resumed preedit rather than duplicating or moving it");
    }
    invalidate(subject.Get()); document->Pop(TF_POPF_ALL);
  }
}

// Faults at actual lock boundaries. Never queue a later write: source, preedit,
// selection, focus and permissions must still match on each synchronous step.
for (unsigned fault=0; fault<10; ++fault) {
  ComPtr<ITfTextInputProcessorEx> subject;
  ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore(IS_TEXT));
  ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie ignored=0;
  const bool made=factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(subject.GetAddressOf()))==S_OK &&
      manager->CreateDocumentMgr(&document)==S_OK && document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&ignored)==S_OK &&
      document->Push(context.Get())==S_OK;
  check(made,"staged fault owns its document");if(!made)continue;
  Preferences options;options.mode=Mode::Local;options.timing=0;options.automatic.enabled=true;
  ComPtr<ITfEditSession> setup;setup.Attach(new AutomaticHostEdit([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"ni");}));
  HRESULT setupResult=E_FAIL;
  check(configure(subject.Get(),&options,client,L"")==S_OK && Commit(apply,subject.Get(),context.Get(),client,L"这里有错子。") &&
      context->RequestEditSession(client,setup.Get(),TF_ES_SYNC|TF_ES_READWRITE,&setupResult)==S_OK && setupResult==S_OK &&
      inject(subject.Get(),&reply)==S_OK,"staged fault prepares preedit and visible hint");
  const auto original=host->text;
  ComPtr<ITfEditSession> retainedStep;
  ComPtr<ITfContext> aliasContext;
  if(fault==9) {
    auto* alias=new OwnedContextAlias(context.Get());aliasContext.Attach(alias);
    alias->editInterceptor=[&,calls=0U](TfClientId c,ITfEditSession* s,DWORD flags,HRESULT* result) mutable -> HRESULT {
      if(++calls==2){retainedStep=s;*result=TF_S_ASYNC;return S_OK;}
      return context->RequestEditSession(c,s,flags,result);
    };
  }
  BOOL handled=FALSE;key(subject.Get(),context.Get(),VK_TAB,0,1,&handled);
  host->onAfterLock=[&] {
    if(fault==0)host->refuseWrites=true;
    if(fault==1){DWORD child=0;environment(subject.Get(),FALSE,&child);}
    if(fault==2)host->ChangeScope(IS_PRIVATE);
    if(fault==3)host->UserSelect(0,0);
    if(fault==4)host->UserReplace(3,5,L"原文");
    if(fault==5)host->onAfterLock=[&]{host->refuseWrites=true;};
    if(fault==6)host->denyWriteLocks=1;
    if(fault==7)invalidate(subject.Get());
    if(fault==8)host->dynamicStatus=TS_SD_READONLY;
  };
  key(subject.Get(),aliasContext?aliasContext.Get():context.Get(),VK_TAB,0,0,&handled);
  const auto expected=fault==4?std::wstring(L"这里有原文。ni"):fault==5?std::wstring(L"这里有错字。ni"):original;
  check(handled && host->text==expected,"stage failure consumes once and performs no late or stale replacement");
  if(fault==5) {
    using Diagnostic=HRESULT(WINAPI*)(ITfTextInputProcessorEx*,AutomaticDiagnostic*);
    const auto diagnostic=reinterpret_cast<Diagnostic>(GetProcAddress(module,"WeaselProbeAutomaticDiagnostic"));
    AutomaticDiagnostic observed;
    check(diagnostic && diagnostic(subject.Get(),&observed)==S_OK && observed.stage==AutomaticStage::TabRestoreUnconfirmed,
        "retained correction with refused preedit recovery is not reported as full Tab success");
  }
  host->refuseWrites=false;host->onAfterLock={};
  DWORD ignoredProcess=0;environment(subject.Get(),TRUE,&ignoredProcess);
  key(subject.Get(),context.Get(),VK_TAB,0,2,&handled);
  const auto final=host->text;key(subject.Get(),context.Get(),VK_TAB,0,0,&handled);
  check(!handled && host->text==final,"failed stage releases Tab after key-up without retrying");
  if(fault==9) {
    ComPtr<ITfEditSession> late;late.Attach(new AutomaticHostEdit([&](TfEditCookie ec){
      return retainedStep && retainedStep->DoEditSession(ec)==TF_E_DISCONNECTED?S_OK:E_FAIL;
    }));
    HRESULT ran=E_FAIL;
    check(context->RequestEditSession(client,late.Get(),TF_ES_SYNC|TF_ES_READWRITE,&ran)==S_OK && ran==S_OK && host->text==final,
        "retained asynchronous step is retired: late callback cannot use expired captures or write text");
  }
  host->dynamicStatus=0;
  if(fault==6)check(Commit(apply,subject.Get(),context.Get(),client,L"你好") && host->text==L"这里有错子。你好",
      "one refused write recovers original pinyin so the next candidate replaces it exactly once");
  invalidate(subject.Get());document->Pop(TF_POPF_ALL);
}
