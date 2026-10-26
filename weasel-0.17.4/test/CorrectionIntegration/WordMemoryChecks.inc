// Reproduce cache retirement with production code over our own synthetic text.
// No Word process, installed TIP, registry settings, model or broker is used.
using CachedUnits = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, DWORD*);
using MemoryDiagnostic = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, AutomaticDiagnostic*);
auto cachedUnits = reinterpret_cast<CachedUnits>(GetProcAddress(module, "WeaselProbeAutomaticCachedUnits"));
auto memoryDiagnostic = reinterpret_cast<MemoryDiagnostic>(GetProcAddress(module, "WeaselProbeAutomaticDiagnostic"));
check(cachedUnits && memoryDiagnostic, "cache analysis exports only unit counts and diagnostic stages");
if (!cachedUnits || !memoryDiagnostic) return false;
for (unsigned scenario = 0; scenario < 3; ++scenario) {
  ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore(IS_TEXT));
  ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie ignored = 0;
  const bool made = manager->CreateDocumentMgr(&document) == S_OK &&
      document->CreateContext(client, 0, static_cast<ITextStoreACP*>(host.Get()), &context, &ignored) == S_OK &&
      document->Push(context.Get()) == S_OK;
  check(made, "memory scenario owns its synthetic document"); if (!made) continue;
  WCHAR executable[32768]{}; GetModuleFileNameW(nullptr, executable, _countof(executable));
  check(configure(service.Get(), &preferences, client, scenario == 2 ? executable : L"") == S_OK,
      "memory scenario uses isolated configuration, no real service");
  auto units = [&]() { DWORD count = MAXDWORD; return cachedUnits(service.Get(), &count) == S_OK ? count : MAXDWORD; };
  const std::wstring prefix = scenario == 2 ? L"模拟进程退出。" : L"这里有";
  check(Commit(apply, service.Get(), context.Get(), client, prefix) && units() == prefix.size(),
      "successful readback initially retains the owned committed text");
  if (scenario == 0) {
    AutomaticReply noChange; noChange.count = 0;
    check(inject(service.Get(), &noChange) == S_OK && units() == prefix.size(),
        "valid no-change result preserves context rather than forgetting it");
    check(Commit(apply, service.Get(), context.Get(), client, L"错子。") && host->text == L"这里有错子。" && units() == 6,
        "uninterrupted owned commits accumulate to the complete sentence");
  } else if (scenario == 1) {
    host->attrsAtPositionResult = E_NOTIMPL; host->ChangeScope(IS_TEXT);
    ComPtr<ITfEditSession> inspect;
    inspect.Attach(new AutomaticHostEdit([&](TfEditCookie ec) {
      ComPtr<ITfRange> range, end;
      if (context->GetStart(ec, &range) != S_OK || context->GetEnd(ec, &end) != S_OK ||
          range->ShiftEndToRange(ec, end.Get(), TF_ANCHOR_END) != S_OK) return E_FAIL;
      const auto policy = InspectInputScope(context.Get(), ec, range.Get(), ScopeUse::LocalCommit);
      WCHAR text[16]{}; ULONG count = 0;
      const auto read = range->GetText(ec, 0, text, _countof(text), &count);
      const bool same = read == S_OK && std::wstring(text, count) == prefix;
      SecureZeroMemory(text, sizeof(text));
      check(!policy && policy.error == E_FAIL && same,
          "owned host still allows GetText while scope metadata fails: these are independent capabilities");
      return S_OK;
    }));
    HRESULT inspected = E_FAIL;
    check(context->RequestEditSession(client, inspect.Get(), TF_ES_SYNC | TF_ES_READ, &inspected) == S_OK && inspected == S_OK,
        "comparison uses a real granted read cookie and reads no external document");
    poll(service.Get()); AutomaticDiagnostic observed;
    check(memoryDiagnostic(service.Get(), &observed) == S_OK && observed.stage == AutomaticStage::ScopeUnavailable && units() == 0 && host->text == prefix,
        "scope failure clears the correction cache while document text survives");
    host->attrsAtPositionResult = S_OK; host->ChangeScope(IS_TEXT); poll(service.Get());
    check(units() == 0 && host->text == prefix, "metadata recovery alone does not reconstruct discarded context");
    check(Commit(apply, service.Get(), context.Get(), client, L"错子。") && host->text == L"这里有错子。" && units() == 3,
        "next successful commit remembers only the new tail despite a complete visible sentence");
  } else {
    // The diagnostic worker exits for this compiled string without emitting a
    // reply. It is not a model and receives only our generated test text.
    const auto deadline = GetTickCount64() + 3000;
    AutomaticDiagnostic observed;
    do { poll(service.Get()); memoryDiagnostic(service.Get(), &observed); if (observed.stage == AutomaticStage::WorkerFailed) break; Sleep(10); }
    while (GetTickCount64() < deadline);
    check(observed.stage == AutomaticStage::WorkerFailed && units() == 0 && host->text == prefix,
        "failed inference/transport path also clears context without removing host text");
    check(Commit(apply, service.Get(), context.Get(), client, L"这里有") && units() == 3 && host->text == prefix + L"这里有",
        "after worker failure the next capture starts over rather than rereading old document text");
  }
  invalidate(service.Get()); document->Pop(TF_POPF_ALL);
}
