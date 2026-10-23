// Compatibility investigation only: proving this modeled limitation is not
// real Edge/Word acceptance. The staged alternative below is TEST CODE ONLY.
for (const InputScope scope : {IS_TEXT, IS_DEFAULT}) for (const bool composing : {false, true}) {
  ComPtr<ITfTextInputProcessorEx> subject;
  ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore(scope));
  ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie ignored = 0;
  const bool made = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
      reinterpret_cast<void**>(subject.GetAddressOf())) == S_OK && manager->CreateDocumentMgr(&document) == S_OK &&
      document->CreateContext(client, 0, static_cast<ITextStoreACP*>(host.Get()), &context, &ignored) == S_OK &&
      document->Push(context.Get()) == S_OK;
  check(made, "composition projection owns its synthetic document and service");
  if (!made) continue;
  auto editOwned = [&](std::function<HRESULT(TfEditCookie)> body) {
    ComPtr<ITfEditSession> session; session.Attach(new AutomaticHostEdit(std::move(body)));
    HRESULT result = E_FAIL;
    return context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &result) == S_OK && result == S_OK;
  };
  Preferences options; options.mode = Mode::Local; options.timing = composing ? 0 : 1; options.automatic.enabled = true;
  check(configure(subject.Get(), &options, client, L"") == S_OK &&
      Commit(apply, subject.Get(), context.Get(), client, L"这里有错子。") &&
      (!composing || editOwned([&](TfEditCookie ec) { return preedit(subject.Get(), context.Get(), ec, L"ni"); })) &&
      inject(subject.Get(), &reply) == S_OK, "projection prepares the same correction with or without preedit");
  {
    CompositionProjection projection(*host.Get(), context.Get());
    const auto original = host->text;
    const auto expected = composing ? L"这里有错字。ni" : L"这里有错字。";
    BOOL handled = FALSE;
    key(subject.Get(), context.Get(), VK_TAB, 0, 1, &handled);
    key(subject.Get(), context.Get(), VK_TAB, 0, 0, &handled);
    if (!composing) {
      check(handled && projection.Valid() && projection.Visible() == expected && host->text == expected,
          "projection control: correction without active preedit reaches visible text");
    } else {
      check(handled && projection.Valid() && projection.Visible() == original && host->text == original,
          "KNOWN LIMITATION REPRODUCED: restored same-start preedit loses the committed-prefix correction");
      using Diagnostic = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, AutomaticDiagnostic*);
      const auto diagnostic = reinterpret_cast<Diagnostic>(GetProcAddress(module, "WeaselProbeAutomaticDiagnostic"));
      AutomaticDiagnostic observed;
      check(diagnostic && diagnostic(subject.Get(), &observed) == S_OK && observed.stage == AutomaticStage::TabPersistenceUnconfirmed,
          "projection failure is unconfirmed rather than incorrectly accepted");
    }
    key(subject.Get(), context.Get(), VK_TAB, 0, 2, &handled);
    if (composing) {
      // Feasibility experiment: retire only the TSF composition (keep ni),
      // correct with no active preedit, then start a composition over existing ni.
      // This does not modify the production algorithm or add a user option.
      using Exercise = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*, ITfContextView*, ITfComposition*, TfEditCookie, DWORD, BOOL);
      const auto exercise = reinterpret_cast<Exercise>(GetProcAddress(module, "WeaselProbeExerciseComposition"));
      ComPtr<ITfRange> pinyin, selection;
      check(exercise && editOwned([&](TfEditCookie ec) {
        ComPtr<ITfContextComposition> compositions; ComPtr<IEnumITfCompositionView> items;
        ComPtr<ITfCompositionView> view; ComPtr<ITfComposition> live;
        TF_SELECTION selected{}; ULONG fetched = 0;
        if (context.As(&compositions) != S_OK || compositions->EnumCompositions(&items) != S_OK ||
            items->Next(1,&view,nullptr) != S_OK || view.As(&live) != S_OK || view->GetRange(&pinyin) != S_OK ||
            context->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selected,&fetched) != S_OK) return E_FAIL;
        selection.Attach(selected.range);
        return fetched == 1 && selection ? exercise(subject.Get(),context.Get(),nullptr,live.Get(),ec,0,FALSE) : E_FAIL;
      }) && projection.Visible() == original, "staged prototype ends TSF preedit without deleting its text");
      check(editOwned([&](TfEditCookie ec) {
        ComPtr<ITfRange> correction;
        if (context->GetStart(ec,&correction) != S_OK) return E_FAIL;
        LONG shifted = 0;
        if (correction->ShiftStart(ec,3,&shifted,nullptr) != S_OK || shifted != 3 ||
            correction->Collapse(ec,TF_ANCHOR_START) != S_OK ||
            correction->ShiftEnd(ec,2,&shifted,nullptr) != S_OK || shifted != 2) return E_FAIL;
        const auto written = correction->SetText(ec,TF_ST_CORRECTION,L"错字",2);
        TF_SELECTION selected{selection.Get(),{TF_AE_NONE,FALSE}};
        return written == S_OK ? context->SetSelection(ec,1,&selected) : written;
      }) && projection.Visible() == expected, "staged prototype commits correction in a separate write session");
      check(exercise && pinyin && editOwned([&](TfEditCookie ec) {
        ComPtr<ITfContextComposition> compositions; ComPtr<ITfCompositionSink> sink; ComPtr<ITfComposition> live;
        if (context.As(&compositions) != S_OK || subject.As(&sink) != S_OK) return E_FAIL;
        const auto started = compositions->StartComposition(ec,pinyin.Get(),sink.Get(),&live);
        if (started != S_OK || !live) return E_FAIL;
        const auto bound = exercise(subject.Get(),context.Get(),nullptr,live.Get(),ec,7,FALSE);
        TF_SELECTION selected{selection.Get(),{TF_AE_NONE,FALSE}};
        return bound == S_OK ? context->SetSelection(ec,1,&selected) : bound;
      }) && projection.Visible() == expected, "staged prototype resumes composition on existing ni without reinserting text");
    }
    check(Commit(apply,subject.Get(),context.Get(),client,L"你好") && projection.Valid() &&
        projection.Visible() == L"这里有错字。你好", "projection prototype or control still commits the next candidate at the correct position");
  }
  invalidate(subject.Get()); document->Pop(TF_POPF_ALL);
}
