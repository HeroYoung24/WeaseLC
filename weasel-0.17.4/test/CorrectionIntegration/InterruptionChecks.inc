    // Host-generated edits and selection events: no mouse injection and no
    // foreground switching. Exercise production OnEndEdit for the active field.
    auto fresh = [&](bool composing = false, bool present = true) {
      preferences.timing = 1;
      configureNow();
      if (!Commit(apply, service.Get(), context.Get(), client, L"这里有错子。")) return false;
      if (composing && !edit([&](TfEditCookie ec) { return preedit(service.Get(), context.Get(), ec, L"nihao"); })) return false;
      return !present || inject(service.Get(), &reply) == S_OK;
    };
    auto pump = [&]() {
      MSG message;
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    };
    for (unsigned scenario = 0; scenario != 4; ++scenario) {
      const bool hasHint = (scenario & 1) != 0;
      check(store->UserSelect(static_cast<LONG>(store->text.size()), static_cast<LONG>(store->text.size())) == S_OK,
          "outside-preedit fixture begins at document end");
      check(fresh(true, hasHint), "outside-preedit fixture ready with or without model hint");
      ComPtr<ITfSource> source; context.As(&source);
      ComPtr<ITfTextEditSink> sink; service.As(&sink);
      DWORD subscription = TF_INVALID_COOKIE;
      const bool subscribed = source && source->AdviseSink(IID_ITfTextEditSink, sink.Get(), &subscription) == S_OK;
      check(subscribed, "outside-preedit fixture subscribes only its owned text store");
      const auto intact = store->text;
      const LONG end = static_cast<LONG>(intact.size());
      const LONG start = end - 6;
      const LONG selectionEnd = scenario < 2 ? start : end - 3;
      check(store->UserSelect(start, selectionEnd) == S_OK, "caret or drag selection leaves active preedit");
      pump();
      check(store->text == intact, "outside selection preserves all host text");
      check(!state().first && noKey(), "outside selection releases correction shortcut");
      check(edit([&](TfEditCookie) {
        ComPtr<ITfContextComposition> compositions;
        ComPtr<IEnumITfCompositionView> enumerator;
        ComPtr<ITfCompositionView> remaining;
        ULONG fetched = 0;
        if (context.As(&compositions) != S_OK || compositions->EnumCompositions(&enumerator) != S_OK || !enumerator)
          return E_FAIL;
        return enumerator->Next(1, &remaining, &fetched) == S_FALSE && fetched == 0 ? S_OK : E_FAIL;
      }), "mouse retirement ends the host composition, not only frontend tracking");
      if (subscribed) source->UnadviseSink(subscription);
      const auto expected = intact.substr(0, start) + L"你好" + intact.substr(selectionEnd);
      check(edit([&](TfEditCookie ec) { return preedit(service.Get(), context.Get(), ec, L"ni"); }) &&
          Commit(apply, service.Get(), context.Get(), client, L"你好") && store->text == expected,
          "next composition starts and commits at new selection without switching IME");
      check(store->UserSelect(static_cast<LONG>(store->text.size()), static_cast<LONG>(store->text.size())) == S_OK,
          "outside-preedit fixture restores append position for later tests");
    }
    {
      check(fresh(true), "interruption fixture starts with live preedit");
      ComPtr<ITfSource> source; context.As(&source);
      ComPtr<ITfTextEditSink> sink; service.As(&sink);
      DWORD subscription = TF_INVALID_COOKIE;
      const bool subscribed = source && source->AdviseSink(IID_ITfTextEditSink, sink.Get(), &subscription) == S_OK;
      check(subscribed, "production text-edit sink advised only on owned document");
      const auto unmodified = store->text;
      const LONG end = static_cast<LONG>(unmodified.size());
      check(store->UserSelect(end - 2, end - 2) == S_OK, "mouse-style caret move inside preedit");
      pump();
      check(!state().first && store->text == unmodified, "mouse interruption immediately retires hint without changing preedit");
      store->UserSelect(end, end); pump();
      check(inject(service.Get(), &reply) == S_FALSE && noKey(), "returning caret does not revive an interrupted suggestion");
      if (subscribed) source->UnadviseSink(subscription);
      auto continued = unmodified.substr(0, unmodified.size() - 5) + L"你好";
      check(Commit(apply, service.Get(), context.Get(), client, L"你好") && store->text == continued,
          "normal composition commit still works after mouse cancellation");
    }
    check(fresh(true), "drag-selection fixture ready");
    auto intact = store->text;
    LONG end = static_cast<LONG>(intact.size());
    store->UserSelect(end - 3, end - 1);
    poll(service.Get());
    check(!state().first && noKey() && store->text == intact, "nonempty mouse selection cancels hint and cannot be replaced by Tab");
    check(fresh(true), "external paste fixture ready");
    end = static_cast<LONG>(store->text.size());
    store->UserReplace(end - 3, end - 1, L"XY");
    intact = store->text; poll(service.Get());
    check(noKey() && !state().first && store->text == intact, "external preedit edit is preserved and invalidates correction");

    check(fresh(true, false), "delayed result fixture ready");
    end = static_cast<LONG>(store->text.size());
    store->UserSelect(end - 1, end - 1);
    check(inject(service.Get(), &reply) == S_FALSE && noKey(), "late result after preedit mouse movement is discarded before display");
    check(fresh(), "focus-loss fixture ready");
    DWORD child = 0;
    environment(service.Get(), FALSE, &child); poll(service.Get());
    environment(service.Get(), TRUE, &child);
    check(!state().first && inject(service.Get(), &reply) == S_FALSE && noKey(), "leaving and returning to a document cannot revive old results");

    ComPtr<ITfTextLayoutSink> layouts; service.As(&layouts);
    check(fresh(), "scroll fixture ready");
    RECT beforeMove{}; GetWindowRect(state().second, &beforeMove);
    store->textRect = {500, 350, 510, 370};
    layouts->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr);
    check(!IsWindowVisible(state().second), "layout changes immediately hide the old anchor");
    poll(service.Get());
    RECT afterMove{}, overlap{}; GetWindowRect(state().second, &afterMove);
    check(state().first && IsWindowVisible(state().second) && !EqualRect(&beforeMove, &afterMove) &&
        !IntersectRect(&overlap, &afterMove, &store->textRect), "scroll refresh follows host caret without covering it");
    store->textClipped = TRUE;
    layouts->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr); poll(service.Get());
    check(state().first && !IsWindowVisible(state().second) && noKey(), "off-screen caret hides hint and releases Tab");
    store->textClipped = FALSE; store->layoutAvailable = false;
    layouts->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr); poll(service.Get());
    check(!IsWindowVisible(state().second) && noKey(), "unavailable layout never reuses old screen coordinates");
    store->layoutAvailable = true;
    layouts->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr); poll(service.Get());
    check(state().first && IsWindowVisible(state().second), "valid layout can recover an otherwise current hint");
    layouts->OnLayoutChange(context.Get(), TF_LC_DESTROY, nullptr);
    check(!state().first && inject(service.Get(), &reply) == S_FALSE, "destroyed view retires its correction state");
    store->textRect = {100, 100, 110, 120};

    check(fresh(), "readonly transition fixture ready");
    store->ChangeStatus(TS_SD_READONLY);
    const auto readsBeforeReadonly = store->textReads;
    layouts->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr); poll(service.Get());
    check(!state().first && noKey() && store->textReads == readsBeforeReadonly,
        "readonly transition cancels before rereading text");
    store->ChangeStatus(0);
    check(fresh(), "protected scope transition fixture ready");
    store->ChangeScope(IS_PRIVATE);
    const auto readsBeforePrivate = store->textReads;
    layouts->OnLayoutChange(context.Get(), TF_LC_CHANGE, nullptr); poll(service.Get());
    check(!state().first && noKey() && store->textReads == readsBeforePrivate,
        "private scope transition cancels before rereading text");
    store->ChangeScope(IS_TEXT);

    check(fresh(), "modified Tab fixture ready");
    BYTE keyboard[256]{};
    const bool savedKeyboard = GetKeyboardState(keyboard) != FALSE;
    for (int modifier : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN}) {
      BYTE changed[256]{}; changed[modifier] = 0x80;
      const bool changedThread = savedKeyboard && SetKeyboardState(changed);
      BOOL consumed = TRUE;
      const bool bypass = changedThread && key(service.Get(), context.Get(), VK_TAB, 0, 1, &consumed) == S_FALSE && !consumed;
      if (savedKeyboard) SetKeyboardState(keyboard);
      check(bypass, "modified Tab remains available to host navigation/shortcuts");
    }
    check(fresh(), "accepted Tab release fixture ready");
    keys->OnTestKeyDown(context.Get(), VK_TAB, 0, &eaten); keys->OnKeyDown(context.Get(), VK_TAB, 0, &eaten);
    BOOL ignored = FALSE;
    key(service.Get(), context.Get(), VK_SHIFT, 0, 1, &ignored);
    check(key(service.Get(), context.Get(), VK_TAB, 0, 3, &eaten) == S_OK && eaten,
        "intervening cancellation preserves accepted Tab test key-up pairing");
    check(key(service.Get(), context.Get(), VK_TAB, 0, 2, &eaten) == S_OK && eaten && noKey(),
        "accepted Tab release is consumed once after another key");

    // An actual owned child exercises cancellation and malformed IPC without a
    // live model. Its input is restricted to compiled synthetic test strings.
    WCHAR ownExecutable[32768]{}; GetModuleFileNameW(nullptr, ownExecutable, _countof(ownExecutable));
    auto fixtureConfigure = [&]() {
      return configure(service.Get(), &preferences, client, ownExecutable) == S_OK;
    };
    auto await = [&](const std::function<bool()>& predicate, ULONGLONG timeout = 3000) {
      const auto began = GetTickCount64();
      while (!predicate() && GetTickCount64() - began < timeout) { poll(service.Get()); pump(); Sleep(10); }
      return predicate();
    };
    // A completed zero-suggestion response must retain only this IME's bounded
    // adjacent commits, otherwise pauses between words lose correction context.
    fixtureConfigure(); const auto beforeNoSuggestion = store->text;
    check(Commit(apply,service.Get(),context.Get(),client,L"这里有错"),"first fragment committed before zero-suggestion check");
    bool zeroChildSeen=false;
    const bool zeroFinished=await([&] {
      environment(service.Get(),TRUE,&child);zeroChildSeen=zeroChildSeen||child!=0;
      return zeroChildSeen && !child;
    });
    check(zeroFinished && !state().first && noKey(),"zero-suggestion reply finishes without displaying or consuming Tab");
    bool restarted=false;const auto idleUntil=GetTickCount64()+350;
    while(GetTickCount64()<idleUntil) { poll(service.Get());pump();environment(service.Get(),TRUE,&child);restarted=restarted||child!=0;Sleep(10); }
    check(!restarted,"unchanged zero-suggestion text does not relaunch model worker");
    check(Commit(apply,service.Get(),context.Get(),client,L"子。") && await([&]{return state().first;}),
        "later fragment still sees earlier IME commit after no-suggestion response");
    check(edit([&](TfEditCookie ec){return preedit(service.Get(),context.Get(),ec,L"ni");}),
        "next composition starts after merged correction result");
    eaten=FALSE;
    if(state().first) { keys->OnTestKeyDown(context.Get(),VK_TAB,0,&eaten);keys->OnKeyDown(context.Get(),VK_TAB,0,&eaten); }
    check(eaten && store->text==beforeNoSuggestion+L"这里有错字。ni",
        "Tab corrects across paused commits while preserving new pinyin");
    if(eaten) keys->OnKeyUp(context.Get(),VK_TAB,0,&eaten);
    Commit(apply,service.Get(),context.Get(),client,L"你好");
    check(store->text==beforeNoSuggestion+L"这里有错字。你好","normal commit remains usable after zero-suggestion context reuse");
    fixtureConfigure();Commit(apply,service.Get(),context.Get(),client,L"模拟空坏响应。");
    zeroChildSeen=false;
    const bool invalidEmptyFinished=await([&] { environment(service.Get(),TRUE,&child);zeroChildSeen=zeroChildSeen||child!=0;return zeroChildSeen&&!child; });
    check(invalidEmptyFinished && !state().first && noKey(),"invalid zero-count reply cannot be treated as a safe completed response");

    for(unsigned interrupted=0;interrupted!=2;++interrupted) {
      store->UserSelect(static_cast<LONG>(store->text.size()),static_cast<LONG>(store->text.size()));
      fixtureConfigure();Commit(apply,service.Get(),context.Get(),client,L"这里有错");zeroChildSeen=false;
      check(await([&]{environment(service.Get(),TRUE,&child);zeroChildSeen=zeroChildSeen||child!=0;return zeroChildSeen&&!child;}),
          "completed no-suggestion context ready for interruption boundary");
      if(interrupted) expire(service.Get());
      else {
        ComPtr<ITfSource> source;context.As(&source);
        ComPtr<ITfTextEditSink> sink;service.As(&sink);
        DWORD subscription=TF_INVALID_COOKIE;
        const bool subscribed=source && source->AdviseSink(IID_ITfTextEditSink,sink.Get(),&subscription)==S_OK;
        const auto end=static_cast<LONG>(store->text.size());
        check(subscribed && store->UserSelect(end-1,end-1)==S_OK,"no-suggestion mouse fixture delivers real owned selection notifications");
        pump();
        if(subscribed)source->UnadviseSink(subscription);
      }
      poll(service.Get());
      store->UserSelect(static_cast<LONG>(store->text.size()),static_cast<LONG>(store->text.size()));
      const auto original=store->text;Commit(apply,service.Get(),context.Get(),client,L"子。");zeroChildSeen=false;
      const bool newFinished=await([&]{environment(service.Get(),TRUE,&child);zeroChildSeen=zeroChildSeen||child!=0;return zeroChildSeen&&!child;});
      check(newFinished && !state().first && noKey() && store->text==original+L"子。",
          "mouse movement or expiry prevents reusing prior no-suggestion context");
    }


    fixtureConfigure(); Commit(apply, service.Get(), context.Get(), client, L"模拟慢响应。");
    const bool startedChild = await([&]() { environment(service.Get(), TRUE, &child); return child != 0; });
    HANDLE oldChild = startedChild ? OpenProcess(SYNCHRONIZE, FALSE, child) : nullptr;
    check(startedChild && oldChild, "slow synthetic worker starts in its own process");
    end = static_cast<LONG>(store->text.size()); store->UserSelect(end - 1, end - 1);
    poll(service.Get());
    check(oldChild && WaitForSingleObject(oldChild, 1500) == WAIT_OBJECT_0 && !state().first && noKey(),
        "mouse interruption terminates only the outstanding owned worker");
    if (oldChild) CloseHandle(oldChild);
    fixtureConfigure(); Commit(apply, service.Get(), context.Get(), client, L"模拟慢响应。");
    const bool supersededStarted = await([&]() { environment(service.Get(), TRUE, &child); return child != 0; });
    oldChild = supersededStarted ? OpenProcess(SYNCHRONIZE, FALSE, child) : nullptr;
    check(Commit(apply, service.Get(), context.Get(), client, L"这里有错子。") && oldChild &&
        WaitForSingleObject(oldChild, 1500) == WAIT_OBJECT_0, "new commit terminates the older slow request");
    if (oldChild) CloseHandle(oldChild);
    check(await([&]() { return state().first; }), "latest merged context receives a fresh worker result after supersession");
    fixtureConfigure(); Commit(apply, service.Get(), context.Get(), client, L"模拟坏响应。");
    bool observedChild = false;
    await([&]() { environment(service.Get(), TRUE, &child); observedChild = observedChild || child != 0; return observedChild && !child; });
    check(observedChild && !state().first && noKey(), "malformed worker reply fails closed without taking Tab");
    fixtureConfigure(); Commit(apply, service.Get(), context.Get(), client, L"模拟进程退出。");
    observedChild = false;
    const bool exitedChild = await([&]() {
      environment(service.Get(), TRUE, &child); observedChild = observedChild || child != 0;
      return observedChild && !child;
    });
    check(exitedChild && !state().first && noKey(), "worker exit without a reply leaves IME input unclaimed");

    std::wstring helperDirectory = ownExecutable;
    helperDirectory.resize(helperDirectory.find_last_of(L"\\/")); helperDirectory += L"\\worker space";
    CreateDirectoryW(helperDirectory.c_str(), nullptr);
    const auto spacedWorker = helperDirectory + L"\\纠错 worker.exe";
    check(CopyFileW(ownExecutable, spacedWorker.c_str(), FALSE) != FALSE, "synthetic worker copied to a Unicode path containing spaces");
    configure(service.Get(), &preferences, client, spacedWorker.c_str());
    Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
    check(await([&]() { return state().first; }), "private worker launch supports Unicode and spaces without shell parsing");
    configureNow();
    fixtureConfigure(); Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
    check(await([&]() { return state().first; }) && IsWindowVisible(state().second), "valid worker succeeds after cancellation and bad IPC");
    intact = store->text;
    keys->OnTestKeyDown(context.Get(), VK_TAB, 0, &eaten); keys->OnKeyDown(context.Get(), VK_TAB, 0, &eaten);
    auto corrected = intact; corrected.replace(store->SelectionEnd() - 3, 2, L"错字");
    check(eaten && store->text == corrected, "recovered worker suggestion applies to current synthetic document only");
    keys->OnKeyUp(context.Get(), VK_TAB, 0, &eaten);
    configureNow();

    // Large text is measured in the real popup, not reduced to an ellipsis.
    preferences.automatic.width = 180; preferences.automatic.font = 24;
    configureNow();
    const auto longOriginal = std::wstring(31, L'甲') + L"乙";
    const auto longReplacement = std::wstring(31, L'甲') + L"丙";
    Commit(apply, service.Get(), context.Get(), client, longOriginal);
    auto longReply = reply; longReply.items[0] = {};
    auto& longItem = longReply.items[0]; longItem.originalUnits = longItem.replacementUnits = 32; longItem.score = .95;
    wcscpy_s(longItem.original, longOriginal.c_str()); wcscpy_s(longItem.replacement, longReplacement.c_str());
    check(inject(service.Get(), &longReply) == S_OK && IsWindowVisible(state().second), "maximum-length suggestion fits a narrow large-font hint");
    RECT large{}; GetClientRect(state().second, &large);
    const auto dpi = AutomaticWindowDpi(state().second);
    check(large.bottom > MulDiv(24 * 4 + 20, dpi, 96), "long suggestion expands beyond the old fixed-height popup");
    check(SaveOwnedHint(state().second, L"output/CorrectionHintLong.bmp"), "long hint captured from its own paint handler");
    HDC measure = GetDC(state().second);
    for (const UINT testDpi : {96U, 144U, 192U, 288U}) {
      const auto measured = MeasureHint(measure, MulDiv(180, testDpi, 96), testDpi, 24, longOriginal, longReplacement, true, true);
      check(measured.complete && measured.title.bottom < measured.original.top &&
          measured.original.bottom < measured.replacement.top && measured.replacement.bottom < measured.footer.top &&
          measured.footer.bottom < measured.height, "hint sections remain complete and separate at multiple DPI values");
    }
    ReleaseDC(state().second, measure);
    const RECT smallWork{0, 0, 1000, 700}, wideCandidate{20, 100, 980, 200};
    const auto below = PlaceAutomaticHint(wideCandidate, smallWork, SIZE{320, 180}, 0, 8);
    RECT intersection{};
    check(!IsRectEmpty(&below) && below.top >= wideCandidate.bottom && !IntersectRect(&intersection, &below, &wideCandidate),
        "wide candidate falls back to free vertical space");
    const auto crowded = PlaceAutomaticHint(smallWork, smallWork, SIZE{320, 180}, 0, 8);
    check(IsRectEmpty(&crowded), "no available placement never covers a fullscreen candidate");
    preferences.automatic.width = 320; preferences.automatic.font = 14;
    configureNow();
