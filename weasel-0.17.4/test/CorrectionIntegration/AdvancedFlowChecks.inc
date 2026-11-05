// Exercise the actual opt-in TSF controller, not just the pure policy headers.
auto flowPumpUntil=[&](std::function<bool()> done, ULONGLONG timeout=1500) {
  const auto deadline=GetTickCount64()+timeout;
  do {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    if(done())return true;Sleep(5);
  } while(GetTickCount64()<deadline);
  return done();
};
for(unsigned scenario=0;scenario<13;++scenario) {
  ComPtr<ITfTextInputProcessorEx> subject;
  ComPtr<MemoryTextStore> host;host.Attach(new MemoryTextStore(IS_TEXT));
  ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> context;TfEditCookie ignored=0;
  const bool made=factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(subject.GetAddressOf()))==S_OK && manager->CreateDocumentMgr(&document)==S_OK &&
      document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&ignored)==S_OK && document->Push(context.Get())==S_OK;
  check(made,"advanced flow owns a real TSF context");if(!made)continue;
  auto isReady=[&]{DWORD value=0;HWND window=nullptr;status(subject.Get(),&value,&window);return value!=0;};
  auto edit=[&](std::function<HRESULT(TfEditCookie)> body){
    ComPtr<ITfEditSession> session;session.Attach(new AutomaticHostEdit(std::move(body)));HRESULT result=E_FAIL;
    return context->RequestEditSession(client,session.Get(),TF_ES_SYNC|TF_ES_READWRITE,&result)==S_OK && result==S_OK;
  };
  const std::wstring text=L"这里有错子，也在图书管。";
  AutomaticReply answer;answer.count=2;
  answer.items[0].start=3;answer.items[0].originalUnits=answer.items[0].replacementUnits=2;answer.items[0].score=.99;
  wcscpy_s(answer.items[0].original,L"错子");wcscpy_s(answer.items[0].replacement,L"错字");
  answer.items[1].start=static_cast<DWORD>(text.find(L"图书管"));
  answer.items[1].originalUnits=answer.items[1].replacementUnits=3;answer.items[1].score=.99;
  wcscpy_s(answer.items[1].original,L"图书管");wcscpy_s(answer.items[1].replacement,L"图书馆");
  if(scenario==7) {answer.items[0].replacementUnits=3;wcscpy_s(answer.items[0].replacement,L"错别字");}
  Preferences options;options.mode=Mode::Local;options.automatic.enabled=true;options.automatic.advancedFlow=true;
  options.timing=scenario==8?0:1;
  BOOL eaten=FALSE;
  if (scenario == 10 || scenario == 11) {
    std::wstring prefix(correction::kMaxTargetUnits + 20, L'甲');
    if (scenario == 10) prefix += L"\xD83D\xDE42。";
    const auto passage = prefix + (scenario == 10 ? text : L"这里有错子");
    check(configure(subject.Get(),&options,client,L"")==S_OK &&
        Commit(apply,subject.Get(),context.Get(),client,passage) && host->text==passage,
        "oversized IME commit reaches the host unchanged");
    if (scenario == 10) {
      check(inject(subject.Get(),&answer)==S_OK && isReady(),
          "oversized commit admits only the range-verified tail after its separator");
      key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
      check(eaten && host->text==prefix+L"这里有错字，也在图书管。",
          "tail Tab preserves the excluded prefix and its surrogate pair");
    } else {
      check(inject(subject.Get(),&answer)!=S_OK && !isReady(),
          "no safe nonempty suffix means no correction for the oversized commit");
    }
    invalidate(subject.Get());document->Pop(TF_POPF_ALL);
    continue;
  }
  check(configure(subject.Get(),&options,client,L"")==S_OK && Commit(apply,subject.Get(),context.Get(),client,text.c_str()) &&
      inject(subject.Get(),&answer)==S_OK && isReady(),"advanced controller accepts multiple verified hints in next-input mode");
  if(scenario==0) {
    check(Commit(apply,subject.Get(),context.Get(),client,L"你好") && isReady(),"short word commit does not discard active hint");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"这里有错字，也在图书管。你好","advanced Tab preserves following short word and punctuation");
  } else if(scenario==1 || scenario==2) {
    if(scenario==1) host->onSetText=[&]{poll(subject.Get());};
    if(scenario==2) check(edit([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"nihaoshijie");}),
        "advanced queue coexists with long preedit");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    const auto suffix=scenario==2?L"nihaoshijie":L"";
    check(eaten && host->text==std::wstring(L"这里有错字，也在图书管。")+suffix,"first queued correction changes one spelling only");
    for(unsigned repeat=0;repeat<4;++repeat) {
      key(subject.Get(),context.Get(),VK_TAB,1LL<<30,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,1LL<<30,0,&eaten);
      check(eaten && host->text==std::wstring(L"这里有错字，也在图书管。")+suffix,"held Tab cannot accept the next queue item");
    }
    key(subject.Get(),context.Get(),VK_TAB,0,3,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,2,&eaten);
    check(flowPumpUntil(isReady),"second verified hint appears only after key release");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==std::wstring(L"这里有错字，也在图书馆。")+suffix,"fresh Tab accepts the second item with preedit/tail intact");
    host->onSetText={};
  } else if(scenario==3) {
    key(subject.Get(),context.Get(),VK_ESCAPE,0,1,&eaten);key(subject.Get(),context.Get(),VK_ESCAPE,0,0,&eaten);
    check(eaten && !isReady() && host->text==text,"explicit Esc rejects without changing text");
    key(subject.Get(),context.Get(),VK_ESCAPE,0,3,&eaten);key(subject.Get(),context.Get(),VK_ESCAPE,0,2,&eaten);
    check(inject(subject.Get(),&answer)==S_OK && !isReady(),"duplicate reply cannot immediately revive rejected hint");
    key(subject.Get(),context.Get(),L'A',0,1,&eaten);
    check(isReady(),"next normal input opportunity permits the remaining suggestion");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(host->text==L"这里有错子，也在图书馆。","rejected first spelling stays unchanged while next item applies");
  } else if(scenario==4) {
    check(host->UserReplace(0,1,L"另")==S_OK,"host changes tracked source");
    eaten=FALSE;key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);
    check(!eaten && !isReady() && host->text==L"另里有错子，也在图书管。","source change invalidates the whole old queue");
  } else if(scenario==5) {
    check(edit([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"ni");}),"enhanced no-op host has live preedit");
    const auto unchanged=host->text;host->acknowledgeWithoutWrite=true;
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    host->acknowledgeWithoutWrite=false;
    check(eaten && host->text==unchanged && !isReady(),"enhanced acknowledged no-op does not advance or replay queue");
  } else if(scenario==6) {
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);
    check(eaten && host->UserReplace(0,1,L"另")==S_OK,"host changes source between Tab query and actual");
    key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"另里有错子，也在图书管。" && !isReady(),"offered but invalidated Tab stays consumed without stale editing");
  } else if(scenario==7) {
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"这里有错别字，也在图书管。","enhanced first write may change spelling length");
    key(subject.Get(),context.Get(),VK_TAB,0,2,&eaten);
    check(flowPumpUntil(isReady),"queue revalidates after spelling length change");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"这里有错别字，也在图书馆。","next spelling uses rebuilt offset after length change");
  } else if(scenario==8) {
    DWORD value=0;HWND window=nullptr;status(subject.Get(),&value,&window);
    check(!IsWindowVisible(window),"enhanced first hint still honors next-input timing preference");
    check(edit([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"ni");}) && IsWindowVisible(window),
        "next composition exposes enhanced hint");
    check(Commit(apply,subject.Get(),context.Get(),client,L"你好") && isReady() && IsWindowVisible(window),
        "once seen enhanced hint survives fast short-word confirmation");
    AutomaticReply noSuggestion;
    check(inject(subject.Get(),&noSuggestion)==S_OK && isReady() && IsWindowVisible(window),
        "new check omitting a visible hint does not dismiss it after a verified append");
    const auto afterMinimum = GetTickCount64() + options.automatic.minimumVisibleMs + 100;
    check(flowPumpUntil([&]{return GetTickCount64() >= afterMinimum;},
        options.automatic.minimumVisibleMs + 500) && isReady() && IsWindowVisible(window),
        "visible hint outlives the minimum display interval");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"这里有错字，也在图书管。你好",
        "carried hint still edits only the original spelling");
  } else if(scenario==9) {
    key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"这里有错字，也在图书管。",
        "actual-only Tab accepts a visible hint without a TSF query");
    key(subject.Get(),context.Get(),VK_TAB,1LL<<30,0,&eaten);
    check(eaten && host->text==L"这里有错字，也在图书管。",
        "actual-only autorepeat cannot accept a second hint");
    key(subject.Get(),context.Get(),VK_TAB,0,2,&eaten);
    check(flowPumpUntil(isReady),"actual-only release lets the second hint advance");
    key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"这里有错字，也在图书馆。",
        "a fresh actual-only Tab accepts the next hint");
  } else {
    check(Commit(apply,subject.Get(),context.Get(),client,L"你好") && isReady(),
        "visible hint carries over an adjacent commit before rechecking");
    answer.count=1;
    answer.items[0].replacementUnits=3;
    wcscpy_s(answer.items[0].replacement,L"错别字");
    check(inject(subject.Get(),&answer)==S_OK && isReady(),"new overlapping correction replaces the carried proposal");
    key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text==L"这里有错别字，也在图书管。你好",
        "Tab applies the new proposal instead of a superseded overlapping hint");
  }
  invalidate(subject.Get());document->Pop(TF_POPF_ALL);
}
