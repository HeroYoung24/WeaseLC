// Only the Word metadata eligibility proof is substituted. All cache, TSF lock,
// selection, readback, timeout and cancellation code is the production path.
using ScopeWait = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, DWORD);
using Deadline = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ULONGLONG*);
auto scopeWait = reinterpret_cast<ScopeWait>(GetProcAddress(module,"WeaselProbeAutomaticScopeWait"));
auto scopeDeadline = reinterpret_cast<Deadline>(GetProcAddress(module,"WeaselProbeAutomaticScopeDeadline"));
check(scopeWait && scopeDeadline,"bounded Word quarantine test controls available only in probe build");
if (!scopeWait || !scopeDeadline) return false;
for (unsigned scenario=0; scenario<11; ++scenario) {
  ComPtr<MemoryTextStore> host;host.Attach(new MemoryTextStore(IS_TEXT));
  host->attrsAtPositionResult=E_NOTIMPL;
  ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> context;TfEditCookie ignored=0;
  const bool made=manager->CreateDocumentMgr(&document)==S_OK &&
      document->CreateContext(client,0,static_cast<ITextStoreACP*>(host.Get()),&context,&ignored)==S_OK && document->Push(context.Get())==S_OK;
  check(made,"Word quarantine uses its own synthetic document");if(!made)continue;
  auto options=preferences;options.timing=scenario==1?0:1;
  WCHAR pendingWorker[32768]{};
  if(scenario==10){options.automatic.debounce=100;GetModuleFileNameW(nullptr,pendingWorker,_countof(pendingWorker));}
  check(configure(service.Get(),&options,client,pendingWorker)==S_OK && scopeWait(service.Get(),1)==S_OK &&
      Commit(apply,service.Get(),context.Get(),client,L"这里有"),"owned commit arrives while scope metadata is unavailable");
  auto units=[&]{DWORD count=MAXDWORD;cachedUnits(service.Get(),&count);return count;};
  auto forcePoll=[&]{scopeWait(service.Get(),3);poll(service.Get());};
  auto edit=[&](std::function<HRESULT(TfEditCookie)> body){
    ComPtr<ITfEditSession> session;session.Attach(new AutomaticHostEdit(std::move(body)));HRESULT result=E_FAIL;
    return context->RequestEditSession(client,session.Get(),TF_ES_SYNC|TF_ES_READWRITE,&result)==S_OK && result==S_OK;
  };
  AutomaticDiagnostic observed;memoryDiagnostic(service.Get(),&observed);
  ULONGLONG originalDeadline=0;scopeDeadline(service.Get(),&originalDeadline);
  DWORD ready=0;HWND hint=nullptr;status(service.Get(),&ready,&hint);
  DWORD child=0;environment(service.Get(),TRUE,&child);
  BOOL eaten=FALSE;
  check(units()==3 && observed.stage==AutomaticStage::WaitingScope && originalDeadline && !ready && !IsWindowVisible(hint) && !child &&
      inject(service.Get(),&reply)==S_FALSE && key(service.Get(),context.Get(),VK_TAB,0,1,&eaten)==S_FALSE && !eaten,
      "quarantine retains only owned units without model work, visible hints or Tab consumption");
  if(scenario==0 || scenario==1 || scenario==10) {
    if(scenario==1) {
      check(Commit(apply,service.Get(),context.Get(),client,L"错子。") && units()==6,
          "adjacent owned commits survive temporary scope failure as a bounded unsent whole");
      ULONGLONG carried=0;scopeDeadline(service.Get(),&carried);
      check(carried==originalDeadline,"new commits do not extend the quarantine deadline for old text");
    }
    host->attrsAtPositionResult=S_OK;host->ChangeScope(IS_TEXT);forcePoll();
    ULONGLONG cleared=1;scopeDeadline(service.Get(),&cleared);
    check(units()==(scenario==1?6U:3U) && cleared==0,"fresh explicit scope and exact readback release quarantine without losing context");
    if(scenario==10) {
      Sleep(120);
      host->onAfterLock=[&]{host->attrsAtPositionResult=E_NOTIMPL;host->ChangeScope(IS_TEXT);};
      forcePoll();environment(service.Get(),TRUE,&child);memoryDiagnostic(service.Get(),&observed);
      check(units()==3 && observed.stage==AutomaticStage::WaitingScope && !child,
          "scope failure between validation locks never starts a worker on quarantined text");
    } else if(scenario==0) {
      check(Commit(apply,service.Get(),context.Get(),client,L"错子。") && units()==6 && host->text==L"这里有错子。",
          "recovered first fragment extends to the full sentence");
    } else {
      check(inject(service.Get(),&reply)==S_OK && edit([&](TfEditCookie ec){return preedit(service.Get(),context.Get(),ec,L"ni");}),
          "recovered full context can show a normal suggestion with the next owned preedit");
      key(service.Get(),context.Get(),VK_TAB,0,1,&eaten);key(service.Get(),context.Get(),VK_TAB,0,0,&eaten);
      check(eaten && host->text==L"这里有错字。ni","recovery still uses guarded Tab correction and preserves ni");
      key(service.Get(),context.Get(),VK_TAB,0,2,&eaten);
      check(Commit(apply,service.Get(),context.Get(),client,L"你好") && host->text==L"这里有错字。你好",
          "candidate commit remains aligned after recovered-context correction");
    }
  } else {
    if(scenario==2){scopeWait(service.Get(),2);host->attrsAtPositionResult=S_OK;host->ChangeScope(IS_TEXT);}
    if(scenario==3)environment(service.Get(),FALSE,&child);
    if(scenario==4){host->attrsAtPositionResult=S_OK;host->ChangeScope(IS_PRIVATE);}
    if(scenario==5)host->ChangeStatus(TS_SD_READONLY);
    if(scenario==6)host->UserSelect(0,0);
    if(scenario==7){host->UserReplace(0,1,L"那");host->UserSelect(3,3);host->attrsAtPositionResult=S_OK;host->ChangeScope(IS_TEXT);}
    if(scenario==8)key(service.Get(),context.Get(),VK_LEFT,0,1,&eaten);
    if(scenario==9)check(edit([&](TfEditCookie ec){return preedit(service.Get(),context.Get(),ec,L"ni");}),
        "normal pinyin can start while pending scope is retired safely");
    forcePoll();memoryDiagnostic(service.Get(),&observed);
    check(units()==0 && inject(service.Get(),&reply)==S_FALSE,"expiry focus privacy readonly edits or interruption discard quarantined units");
    if(scenario==2)check(observed.stage==AutomaticStage::ScopeWaitExpired,"expiry remains expiry even if scope becomes readable too late");
    host->attrsAtPositionResult=S_OK;host->ChangeScope(IS_TEXT);host->ChangeStatus(0);environment(service.Get(),TRUE,&child);
    forcePoll();check(units()==0,"restoring permission or focus cannot resurrect discarded quarantined text");
    if(scenario==9)check(Commit(apply,service.Get(),context.Get(),client,L"你") && host->text==L"这里有你",
        "retiring pending correction never clears Rime composition or swallows the next candidate");
  }
  invalidate(service.Get());document->Pop(TF_POPF_ALL);
}
