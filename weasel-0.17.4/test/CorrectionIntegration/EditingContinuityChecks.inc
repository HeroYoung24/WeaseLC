// No broker, registry writes, real model or registered TIP. Exercise production
// capture/key paths against a document owned by this diagnostic process only.
for (unsigned scenario = 0; scenario < 8; ++scenario) {
  ComPtr<ITfTextInputProcessorEx> subject;
  ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore(IS_TEXT));
  ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie ignored = 0;
  const bool made = factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(subject.GetAddressOf())) == S_OK && manager->CreateDocumentMgr(&document) == S_OK &&
      document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&ignored) == S_OK && document->Push(context.Get()) == S_OK;
  check(made,"continuity owns its synthetic context"); if (!made) continue;
  auto editOwned = [&](std::function<HRESULT(TfEditCookie)> body) {
    ComPtr<ITfEditSession> session; session.Attach(new AutomaticHostEdit(std::move(body))); HRESULT result = E_FAIL;
    return context->RequestEditSession(client,session.Get(),TF_ES_SYNC|TF_ES_READWRITE,&result) == S_OK && result == S_OK;
  };
  auto ready = [&] { DWORD value=0; HWND window=nullptr; status(subject.Get(),&value,&window); return value != 0; };
  Preferences options; options.mode=Mode::Local; options.automatic.enabled=true; options.timing=1;
  const std::wstring base = scenario < 6 ? L"这里有错子" : L"这里有错子。";
  check(configure(subject.Get(),&options,client,L"") == S_OK && Commit(apply,subject.Get(),context.Get(),client,base.c_str()) &&
      inject(subject.Get(),&reply) == S_OK && ready(),"continuity starts from a verified suggestion");
  BOOL eaten=FALSE;
  if (scenario < 5) {
    const wchar_t* tails[]{L"，",L"。。。。。",L"。，。，。",L"，，，，，",L"。，.，。,。"};
    const std::wstring tail=tails[scenario];
    for (const auto mark:tail) {
      const wchar_t text[]{mark,0};
      check(Commit(apply,subject.Get(),context.Get(),client,text) && ready(),
          "each adjacent comma/stop preserves the known suggestion without a new model reply");
    }
    check(editOwned([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"ni");}) && ready(),
        "retained punctuation suggestion coexists with the next pinyin");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten); key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text == L"这里有错字"+tail+L"ni","retained Tab changes only the spelling and preserves every punctuation mark");
    key(subject.Get(),context.Get(),VK_TAB,0,2,&eaten);
    check(Commit(apply,subject.Get(),context.Get(),client,L"你") && host->text == L"这里有错字"+tail+L"你",
        "candidate after retained correction commits exactly once");
  } else if (scenario == 5) {
    check(Commit(apply,subject.Get(),context.Get(),client,L"另") && !ready(),
        "new lexical content still requires a fresh model check");
    eaten=FALSE; key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);
    check(!eaten && host->text == base+L"另","no stale suggestion consumes Tab after lexical change");
  } else if (scenario == 6) {
    check(editOwned([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"nihaoshijie");}) && ready(),
        "long preedit starts beside a valid suggestion");
    key(subject.Get(),context.Get(),VK_BACK,0,1,&eaten);
    check(!eaten && ready(),"Backspace query preserves suggestion and leaves Rime's key untouched");
    check(editOwned([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"nihaoshiji");}),
        "Rime shortens only the current pinyin");
    key(subject.Get(),context.Get(),VK_BACK,0,0,&eaten);
    check(!eaten && ready() && host->text == base+L"nihaoshiji","actual Backspace after query still retains source and suggestion");
    key(subject.Get(),context.Get(),VK_BACK,0,2,&eaten);
    key(subject.Get(),context.Get(),VK_BACK,0,1,&eaten);
    check(Commit(apply,subject.Get(),context.Get(),client,L""),"last pinyin Backspace ends an empty composition");
    key(subject.Get(),context.Get(),VK_BACK,0,0,&eaten);
    check(!eaten && ready() && host->text == base,"paired actual after composition end does not discard the unchanged earlier suggestion");
    key(subject.Get(),context.Get(),VK_BACK,0,2,&eaten);
    check(editOwned([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"ni");}) && ready(),
        "typing again reveals the retained suggestion");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten); key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
    check(eaten && host->text == L"这里有错字。ni","Tab after pinyin correction still preserves the full stop and pinyin");
    key(subject.Get(),context.Get(),VK_TAB,0,2,&eaten);
  } else {
    key(subject.Get(),context.Get(),VK_BACK,0,1,&eaten);
    check(!eaten,"body Backspace is not swallowed by the correction feature");
    const LONG end=static_cast<LONG>(host->text.size());
    check(host->UserReplace(end-1,end,L"") == S_OK,"host really deletes committed punctuation");
    key(subject.Get(),context.Get(),VK_BACK,0,0,&eaten);
    check(!ready(),"committed-text Backspace invalidates the snapshot despite retaining preedit Backspace");
    key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);
    check(!eaten && host->text == L"这里有错子","stale source cannot be modified or consume Tab after body deletion");
  }
  invalidate(subject.Get()); document->Pop(TF_POPF_ALL);
}
