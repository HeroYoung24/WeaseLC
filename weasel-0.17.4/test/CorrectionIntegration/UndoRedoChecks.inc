// Host edit notifications for undo/redo, not a browser's native undo stack.
// Verify that correction never replays an old replacement after host history.
for(bool composing:{false,true})for(const std::wstring replacement:{L"错字",L"错误字词"}) {
  ComPtr<ITfTextInputProcessorEx> subject;ComPtr<MemoryTextStore> host;host.Attach(new MemoryTextStore(IS_TEXT));
  ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> context;TfEditCookie ignored=0;
  const bool made=factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(subject.GetAddressOf()))==S_OK && manager->CreateDocumentMgr(&document)==S_OK &&
      document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&ignored)==S_OK &&
      document->Push(context.Get())==S_OK;
  check(made,"undo/redo owns its synthetic document and service");if(!made)continue;
  Preferences options;options.mode=Mode::Local;options.automatic.enabled=true;options.timing=composing?0:1;
  auto proposed=reply;proposed.items[0].replacementUnits=static_cast<DWORD>(replacement.size());
  wcscpy_s(proposed.items[0].replacement,replacement.c_str());
  bool prepared=configure(subject.Get(),&options,client,L"")==S_OK &&
      Commit(apply,subject.Get(),context.Get(),client,L"这里有错子。");
  if(composing) {
    ComPtr<ITfEditSession> session;session.Attach(new AutomaticHostEdit([&](TfEditCookie ec){return preedit(subject.Get(),context.Get(),ec,L"ni");}));
    HRESULT result=E_FAIL;prepared=context->RequestEditSession(client,session.Get(),TF_ES_SYNC|TF_ES_READWRITE,&result)==S_OK && result==S_OK && prepared;
  }
  prepared=inject(subject.Get(),&proposed)==S_OK && prepared;
  const std::wstring original=L"这里有错子。"+std::wstring(composing?L"ni":L"");
  const std::wstring corrected=L"这里有"+replacement+L"。"+(composing?L"ni":L"");
  BOOL eaten=FALSE;key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten);key(subject.Get(),context.Get(),VK_TAB,0,0,&eaten);
  check(prepared && eaten && host->text==corrected,"guarded correction prepares host undo case with expected text");
  key(subject.Get(),context.Get(),VK_TAB,0,2,&eaten);
  eaten=TRUE;
  check(key(subject.Get(),context.Get(),VK_CONTROL,0,0,&eaten)==S_FALSE && !eaten,
      "undo modifier belongs to host and retires correction state");
  const bool undo=host->UserReplace(3,3+static_cast<LONG>(replacement.size()),L"错子")==S_OK &&
      host->UserSelect(static_cast<LONG>(host->text.size()),static_cast<LONG>(host->text.size()))==S_OK;
  eaten=TRUE;
  check(undo && host->text==original && key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten)==S_FALSE && !eaten,
      "host undo restores original without a stale hint consuming Tab");
  const bool redo=host->UserReplace(3,5,replacement)==S_OK &&
      host->UserSelect(static_cast<LONG>(host->text.size()),static_cast<LONG>(host->text.size()))==S_OK;
  poll(subject.Get());eaten=TRUE;
  check(redo && host->text==corrected && key(subject.Get(),context.Get(),VK_TAB,0,1,&eaten)==S_FALSE && !eaten,
      "host redo restores correction without replay or unsolicited new suggestions");
  auto after=corrected;if(composing)after.resize(after.size()-2);after+=L"你好";
  check(Commit(apply,subject.Get(),context.Get(),client,L"你好") && host->text==after,
      "input after host undo/redo preserves preedit alignment without duplicate candidates");
  invalidate(subject.Get());document->Pop(TF_POPF_ALL);
}
