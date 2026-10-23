// Exercise the actual native candidate panel and the strict production anchor
// gate. Only the suggestion and document are synthetic; no model/IPC is needed.
if (!local) {
  using Candidate = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, DWORD, RECT*);
  using Diagnostic = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, AutomaticDiagnostic*);
  const auto candidate = reinterpret_cast<Candidate>(GetProcAddress(module, "WeaselProbeAutomaticCandidate"));
  const auto diagnostic = reinterpret_cast<Diagnostic>(GetProcAddress(module, "WeaselProbeAutomaticDiagnostic"));
  check(candidate && diagnostic, "strict visual test entry points available");
  if (candidate && diagnostic) {
    using SetDpi = HANDLE(WINAPI*)(HANDLE);
    const auto setDpi = reinterpret_cast<SetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
    HANDLE previousDpi = setDpi ? setDpi(reinterpret_cast<HANDLE>(-4)) : nullptr;
    struct Restore { SetDpi set; HANDLE previous; ~Restore() { if (set && previous) set(previous); } } restore{setDpi, previousDpi};
    ComPtr<ITfTextInputProcessorEx> visualService;
    const bool created = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx, reinterpret_cast<void**>(visualService.GetAddressOf())) == S_OK;
    ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore(IS_TEXT));
    ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie cookie = 0;
    const bool made = created && manager->CreateDocumentMgr(&document) == S_OK &&
        document->CreateContext(client, 0, static_cast<ITextStoreACP*>(host.Get()), &context, &cookie) == S_OK && document->Push(context.Get()) == S_OK;
    check(made, "visual fixture uses owned text and native windows without TIP activation");
    if (made) {
      const auto getCandidate = reinterpret_cast<HRESULT(WINAPI*)(ITfTextInputProcessorEx*, IUnknown**)>(
          GetProcAddress(module, "WeaselProbeGetCandidate"));
      ComPtr<IUnknown> candidateObject;
      ComPtr<ITfIntegratableCandidateListUIElement> integrated;
      check(getCandidate && getCandidate(visualService.Get(), &candidateObject) == S_OK &&
          candidateObject.As(&integrated) == S_OK, "integrated candidate keyboard interface is available");
      if (integrated) {
        check(integrated->OnKeyDown(VK_TAB, 0, nullptr) == E_POINTER,
            "integrated candidate rejects a missing handled output");
        for (WPARAM offered : {WPARAM(VK_TAB), WPARAM(VK_ESCAPE), WPARAM(VK_UP), WPARAM(VK_DOWN), WPARAM('1'), WPARAM(VK_OEM_PERIOD)}) {
          BOOL handled = TRUE;
          check(integrated->OnKeyDown(offered, 0, &handled) == S_OK && !handled,
              "integrated candidate never claims an unprocessed key without a hint");
        }
      }
      auto state = [&]() { DWORD ready = 0; HWND hint = nullptr; status(visualService.Get(), &ready, &hint); return std::make_pair(ready != 0, hint); };
      auto edit = [&](std::function<HRESULT(TfEditCookie)> body) {
        ComPtr<ITfEditSession> session; session.Attach(new AutomaticHostEdit(std::move(body))); HRESULT result = E_FAIL;
        return context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &result) == S_OK && result == S_OK;
      };
      Preferences visual; visual.mode = Mode::Local; visual.automatic.enabled = true; visual.timing = 0;
      auto prepare = [&]() {
        return configure(visualService.Get(), &visual, client, L"") == S_OK &&
            Commit(apply, visualService.Get(), context.Get(), client, L"这里有错子。") &&
            edit([&](TfEditCookie ec) { return preedit(visualService.Get(), context.Get(), ec, L"ni"); }) &&
            inject(visualService.Get(), &reply) == S_OK;
      };
      MONITORINFO monitor{sizeof(monitor)};
      GetMonitorInfoW(MonitorFromPoint(POINT{100,100}, MONITOR_DEFAULTTOPRIMARY), &monitor);
      const auto work = monitor.rcWork;
      const RECT positions[] = {
        {work.left + (work.right-work.left)/2,work.top + (work.bottom-work.top)/2,work.left + (work.right-work.left)/2+2,work.top + (work.bottom-work.top)/2+24},
        {work.right-8,work.bottom-32,work.right-6,work.bottom-8},
        {work.left+2,work.top+2,work.left+4,work.top+26}};
      bool saved = false;
      for (unsigned side = 0; side < 3; ++side) for (unsigned location = 0; location < 3; ++location) {
        visual.automatic.side = side;
        check(prepare(), "fixed suggestion prepared for strict native visual layout");
        RECT anchor = positions[location];
        const HWND beforeFocus = GetForegroundWindow();
        const bool panel = candidate(visualService.Get(), 1, &anchor) == S_OK;
        const auto current = state(); RECT hint{}, intersection{};
        const bool visible = current.first && current.second && IsWindowVisible(current.second) && GetWindowRect(current.second, &hint);
        check(panel && visible, "strict production candidate anchor permits an available native panel");
        check(visible && !IntersectRect(&intersection, &anchor, &hint), "hint never overlaps native candidate panel");
        check(visible && hint.left >= work.left && hint.top >= work.top && hint.right <= work.right && hint.bottom <= work.bottom,
            "native hint stays inside monitor work area including edge fallback");
        if (location == 0) check(visible && (side == 0 ? hint.left >= anchor.right : side == 1 ? hint.right <= anchor.left : hint.top >= anchor.bottom),
            "preferred right left or below layout uses candidate rather than host caret");
        check(GetForegroundWindow() == beforeFocus && (GetWindowLongPtrW(current.second,GWL_EXSTYLE) & WS_EX_NOACTIVATE),
            "native candidate and hint never steal foreground focus");
        BYTE alpha = 0; DWORD flags = 0; COLORREF color = 0;
        check(GetLayeredWindowAttributes(current.second,&color,&alpha,&flags) && (flags & LWA_ALPHA) && alpha == 255 &&
            (GetWindowLongPtrW(current.second,GWL_EXSTYLE) & WS_EX_TOPMOST), "hint is opaque and topmost rather than accidentally transparent");
        WCHAR title[256]{}; GetWindowTextW(current.second,title,_countof(title));
        check(wcsstr(title,L"错子") && wcsstr(title,L"错字") && wcsstr(title,L"Tab"), "native hint exposes correction and Tab cue");
        AutomaticDiagnostic details; diagnostic(visualService.Get(),&details);
        check(details.stage == AutomaticStage::Shown, "Shown is reported after native positioning succeeds");
        std::cout << "Visual geometry side=" << side << " location=" << location << " dpi=" << AutomaticWindowDpi(current.second)
          << " candidate=" << anchor.left << ',' << anchor.top << ',' << anchor.right << ',' << anchor.bottom
          << " hint=" << hint.left << ',' << hint.top << ',' << hint.right << ',' << hint.bottom << '\n';
        if (!saved && visible) { check(SaveOwnedHint(current.second,L"output/CorrectionHintStrictVisual.bmp"), "strict native hint rendered to owned preview image"); saved = true; }
        candidate(visualService.Get(),0,&anchor); invalidate(visualService.Get());
      }
      visual.automatic.side = 0;
      check(prepare(), "missing and host-owned candidate fixture prepared");
      RECT anchor = positions[0]; candidate(visualService.Get(),1,&anchor);
      candidate(visualService.Get(),3,&anchor); AutomaticDiagnostic details; diagnostic(visualService.Get(),&details);
      BOOL consumed = TRUE;
      check(!IsWindowVisible(state().second) && details.stage == AutomaticStage::CandidateUnavailable &&
          key(visualService.Get(),context.Get(),VK_TAB,0,1,&consumed) == S_FALSE && !consumed,
          "hidden candidate hides hint and leaves Tab to host");
      anchor = positions[0]; candidate(visualService.Get(),1,&anchor);
      check(IsWindowVisible(state().second), "hint recovers after candidate becomes visible again");
      candidate(visualService.Get(),2,&anchor); diagnostic(visualService.Get(),&details);
      check(!IsWindowVisible(state().second) && details.stage == AutomaticStage::CandidateUnavailable,
          "host-owned candidate fails closed instead of using an unverified rectangle");
      anchor = positions[0]; candidate(visualService.Get(),1,&anchor);
      const auto beforeHiddenQuery = host->text;
      candidate(visualService.Get(),4,&anchor);
      check(IsWindowVisible(state().second), "candidate race fixture retains stale hint until next refresh");
      consumed = TRUE;
      check(key(visualService.Get(),context.Get(),VK_TAB,0,1,&consumed)==S_FALSE && !consumed &&
          host->text==beforeHiddenQuery && !IsWindowVisible(state().second),
          "Tab query rechecks candidate visibility before claiming the key");
      anchor = positions[0]; candidate(visualService.Get(),1,&anchor);
      check(key(visualService.Get(),context.Get(),VK_TAB,0,1,&consumed)==S_OK && consumed,
          "candidate is visible during initial Tab query");
      candidate(visualService.Get(),4,&anchor);
      consumed = TRUE;
      check(key(visualService.Get(),context.Get(),VK_TAB,0,0,&consumed)==S_FALSE && !consumed &&
          host->text==beforeHiddenQuery && !IsWindowVisible(state().second),
          "candidate disappearing after query cannot apply a stale correction");
      // A refused actual Tab cancels the suggestion; prepare a new one.
      check(prepare(), "strict visual fixture recovers after stale candidate rejection");
      anchor = positions[0]; candidate(visualService.Get(),1,&anchor);
      const auto beforeAccept = host->text;
      check(key(visualService.Get(),context.Get(),VK_TAB,0,1,&consumed)==S_OK && consumed && host->text==beforeAccept,
          "strict displayed hint permits read-only Tab query");
      check(key(visualService.Get(),context.Get(),VK_TAB,0,0,&consumed)==S_OK && consumed && !state().first,
          "strict native candidate hint accepts Tab and retires");
      key(visualService.Get(),context.Get(),VK_TAB,0,2,&consumed);
      visual.timing = 1;
      check(configure(visualService.Get(),&visual,client,L"")==S_OK &&
          Commit(apply,visualService.Get(),context.Get(),client,L"这里有错子。") &&
          inject(visualService.Get(),&reply)==S_OK,
          "immediate-mode strict visual fixture prepared without composition");
      anchor = positions[0]; candidate(visualService.Get(),1,&anchor);
      candidate(visualService.Get(),3,&anchor);
      RECT immediate{}, overlap{};
      check(IsWindowVisible(state().second) && GetWindowRect(state().second,&immediate) &&
          immediate.left >= host->textRect.right && !IntersectRect(&overlap,&immediate,&host->textRect),
          "immediate hint stays at host caret when an old candidate is hidden");
      check(key(visualService.Get(),context.Get(),VK_TAB,0,1,&consumed)==S_OK && consumed,
          "immediate hint permits Tab without requiring a composing candidate");
      // Applications can offer a key through the integration interface as
      // well as ITfKeyEventSink. The same accepted Tab must not write twice.
      consumed = TRUE;
      check(integrated && integrated->OnKeyDown(VK_ESCAPE,0,&consumed)==S_OK && !consumed && !state().first,
          "integrated Escape dismisses correction without claiming the application's key");
      check(prepare(), "integrated correction prepared with active pinyin");
      anchor = positions[0]; candidate(visualService.Get(),1,&anchor);
      const auto beforeIntegrated = host->text;
      auto expectedIntegrated = beforeIntegrated;
      const auto correctionAt = expectedIntegrated.rfind(L"错子");
      if (correctionAt != std::wstring::npos) expectedIntegrated.replace(correctionAt,2,L"错字");
      consumed = FALSE;
      check(integrated && integrated->OnKeyDown(VK_TAB,0,&consumed)==S_OK && consumed &&
          host->text==expectedIntegrated && host->text!=beforeIntegrated,
          "integrated candidate Tab applies the displayed correction and preserves pinyin");
      check(integrated && integrated->OnKeyDown(VK_TAB,LPARAM(1)<<30,&consumed)==S_OK && consumed &&
          host->text==expectedIntegrated, "integrated Tab autorepeat cannot reapply a retired suggestion");
      check(key(visualService.Get(),context.Get(),VK_TAB,0,0,&consumed)==S_OK && consumed &&
          host->text==expectedIntegrated, "normal callback after integrated Tab cannot apply twice");
      key(visualService.Get(),context.Get(),VK_TAB,0,2,&consumed);
      consumed = TRUE;
      check(integrated && integrated->OnKeyDown(VK_TAB,0,&consumed)==S_OK && !consumed,
          "integrated Tab returns to application navigation after key-up");
      candidate(visualService.Get(),0,&anchor); invalidate(visualService.Get()); document->Pop(TF_POPF_ALL);
    }
  }
}
