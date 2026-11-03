// Included inside RunAutomaticChecks: same synthetic document host, but real
// settings/IPC/parser/result handling. Focus and candidate anchors remain fake.
if (!local) {
  const auto registry = L"Software\\WeaseLC-Automatic-Broker-" + std::to_wstring(GetCurrentProcessId());
  HKEY existing = nullptr;
  const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, registry.c_str(), 0, KEY_READ, &existing);
  if (existing) RegCloseKey(existing);
  check(opened == ERROR_FILE_NOT_FOUND, "broker integration owns a fresh temporary settings path");
  if (opened == ERROR_FILE_NOT_FOUND) {
    struct Remove { std::wstring path; ~Remove() { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); } } remove{registry};
    Preferences saved; saved.mode = Mode::Local; saved.automatic.enabled = true; saved.automatic.debounce = 100;
    saved.local.model = L"compiled-fixture"; saved.timing = 0;
    if (brokerModel) saved.local = *brokerModel;
    auto save = [&]() {
      HKEY key = nullptr;
      if (RegCreateKeyExW(HKEY_CURRENT_USER, registry.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
          nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
      const auto encoded = EncodePreferences(saved);
      const bool ok = RegSetValueExW(key, L"SettingsV2", 0, REG_BINARY,
          reinterpret_cast<const BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size())) == ERROR_SUCCESS;
      RegCloseKey(key); return ok;
    };
    check(save(), "backend settings provisioned independently of injected preferences");
    std::atomic_bool fail{false}, hold{false}, entered{false}, cancelled{false}, multiple{false};
    std::atomic_uint posts{0};
    class Fixture final : public IModelTransport {
     public:
      Fixture(std::atomic_bool& fail, std::atomic_bool& hold, std::atomic_bool& entered, std::atomic_bool& cancelled,
              std::atomic_bool& multiple,std::atomic_uint& posts)
          : fail_(fail), hold_(hold), entered_(entered), cancelled_(cancelled),multiple_(multiple),posts_(posts) {}
      TransportResult Post(const ModelConfig& config, const Endpoint& endpoint, const std::string& body,
          const std::atomic_bool& cancelled) override {
        ++posts_;
        if (fail_.load()) { TransportResult result; result.state = State::Timeout; return result; }
        if (hold_.load()) {
          entered_.store(true);
          const auto deadline = GetTickCount64() + 2000;
          while (hold_.load() && !cancelled.load() && GetTickCount64() < deadline) Sleep(2);
          if (cancelled.load()) {
            cancelled_.store(true); TransportResult result; result.state = State::Cancelled; return result;
          }
        }
        FixedAutomaticTransport fixed(true,multiple_.load()); return fixed.Post(config, endpoint, body, cancelled);
      }
     private: std::atomic_bool &fail_, &hold_, &entered_, &cancelled_;
      std::atomic_bool& multiple_; std::atomic_uint& posts_;
    };
    std::function<std::unique_ptr<IModelTransport>()> transportFactory;
    if (!brokerModel) transportFactory = [&]() { return std::make_unique<Fixture>(fail, hold, entered, cancelled,multiple,posts); };
    CorrectionBroker broker(registry, std::move(transportFactory));
    const bool started = broker.Start(); check(started, "normal-process broker starts without TIP registration");
    if (started) {
      ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore(IS_DEFAULT));
      ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie cookie = 0;
      const bool made = manager->CreateDocumentMgr(&document) == S_OK &&
          document->CreateContext(client, 0, static_cast<ITextStoreACP*>(host.Get()), &context, &cookie) == S_OK &&
          document->Push(context.Get()) == S_OK;
      check(made, "broker fixture uses an owned default-scope document");
      if (made) {
        WCHAR executable[32768]{}; GetModuleFileNameW(nullptr, executable, _countof(executable));
        const auto backend = L"broker:" + std::wstring(executable);
        Preferences injected;  // Deliberately OFF: the real broker must supply ON.
        check(configure(service.Get(), &injected, client, backend.c_str()) == S_OK &&
            Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"), "commit obtains enabled settings through broker instead of injected off defaults");
        check(inject(service.Get(), &reply) == S_FALSE, "broker integration forbids direct reply injection");
        auto state = [&]() { DWORD ready = 0; HWND hint = nullptr; status(service.Get(), &ready, &hint); return std::make_pair(ready != 0, hint); };
        auto pump = [&](const std::function<bool()>& done) {
          const auto deadline = GetTickCount64() + (brokerModel ? brokerModel->timeoutSeconds * 1000ULL + 2000 : 3000);
          while (GetTickCount64() < deadline) {
            poll(service.Get()); MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (done()) return true;
            Sleep(5);
          }
          return false;
        };
        const auto modelStarted = GetTickCount64();
        check(pump([&]() { return state().first; }), "capture to broker to parser to frontend becomes ready without injection");
        if (brokerModel) std::cout << "Broker real-model capture-to-ready ms = " << GetTickCount64() - modelStarted << '\n';
        check(!IsWindowVisible(state().second), "broker next-input preference keeps hint hidden before composition");
        auto edit = [&](std::function<HRESULT(TfEditCookie)> body) {
          ComPtr<ITfEditSession> session; session.Attach(new AutomaticHostEdit(std::move(body)));
          HRESULT result = E_FAIL;
          return context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &result) == S_OK && result == S_OK;
        };
        check(edit([&](TfEditCookie ec) { return preedit(service.Get(), context.Get(), ec, L"ni"); }) &&
            pump([&]() { return IsWindowVisible(state().second) != FALSE; }), "broker suggestion becomes visible beside synthetic preedit");
        BOOL consumed = FALSE;
        check(key(service.Get(), context.Get(), VK_TAB, 0, 1, &consumed) == S_OK && consumed && host->text == L"这里有错子。ni",
            "broker Tab query preserves original and preedit");
        check(key(service.Get(), context.Get(), VK_TAB, 0, 0, &consumed) == S_OK && consumed && host->text == L"这里有错字。ni",
            "broker Tab corrects previous text while retaining ni");
        key(service.Get(), context.Get(), VK_TAB, 0, 2, &consumed);
        check(Commit(apply, service.Get(), context.Get(), client, L"你好") && host->text == L"这里有错字。你好",
            "Chinese commit continues after broker correction");
        saved.automatic.enabled = false; save();
        injected.mode = Mode::Local; injected.automatic.enabled = true;
        configure(service.Get(), &injected, client, backend.c_str());
        Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
        check(!state().first && key(service.Get(), context.Get(), VK_TAB, 0, 1, &consumed) == S_FALSE && !consumed,
            "backend disabled state wins over injected enabled settings and leaves Tab alone");
        saved.automatic.enabled = true; save();
        if (!brokerModel) {
          saved.automatic.advancedFlow=true;saved.timing=1;multiple=true;save();
          configure(service.Get(),&injected,client,backend.c_str());
          const auto beforeAdvanced=host->text;
          check(Commit(apply,service.Get(),context.Get(),client,L"这里有错子，也在图书管。") &&
              pump([&]{return state().first && IsWindowVisible(state().second);}),
              "enhanced settings and multiple model answers travel through real broker/parser without injection");
          const auto beforeMarks=posts.load();
          for(unsigned mark=0;mark<5;++mark) Commit(apply,service.Get(),context.Get(),client,L"。");
          poll(service.Get());
          check(state().first && posts.load()==beforeMarks,"equivalent punctuation preserves active hint without another model request");
          key(service.Get(),context.Get(),VK_TAB,0,1,&consumed);key(service.Get(),context.Get(),VK_TAB,0,0,&consumed);
          check(consumed && host->text==beforeAdvanced+L"这里有错字，也在图书管。。。。。。",
              "real broker enhanced first Tab preserves all six stops");
          key(service.Get(),context.Get(),VK_TAB,0,2,&consumed);
          check(pump([&]{return state().first;}),"real broker queue advances after confirmed write");
          key(service.Get(),context.Get(),VK_TAB,0,1,&consumed);key(service.Get(),context.Get(),VK_TAB,0,0,&consumed);
          check(consumed && host->text==beforeAdvanced+L"这里有错字，也在图书馆。。。。。。",
              "real broker second Tab replaces the next spelling in order");
          key(service.Get(),context.Get(),VK_TAB,0,2,&consumed);
          multiple=false;saved.automatic.advancedFlow=false;saved.timing=0;save();
          fail = true;
          configure(service.Get(), &injected, client, backend.c_str());
          Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
          using Diagnostic = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, AutomaticDiagnostic*);
          const auto diagnostic = reinterpret_cast<Diagnostic>(GetProcAddress(module, "WeaselProbeAutomaticDiagnostic"));
          check(diagnostic && pump([&]() { AutomaticDiagnostic result; return diagnostic(service.Get(), &result) == S_OK && result.stage == AutomaticStage::WorkerFailed; }) &&
              !state().first, "backend timeout reaches WorkerFailed rather than a valid no-change result");
          fail = false;
          for (bool lifecycle : {false, true}) {
            hold = true; entered = false; cancelled = false;
            configure(service.Get(), &injected, client, backend.c_str());
            check(Commit(apply, service.Get(), context.Get(), client, L"这里有错子。") && pump([&]() { return entered.load(); }),
                "delayed broker model starts from a validated commit");
            const auto original = host->text;
            if (lifecycle) invalidate(service.Get());
            else { consumed = FALSE; key(service.Get(), context.Get(), VK_LEFT, 0, 0, &consumed); }
            check(pump([&]() { return cancelled.load(); }) && !state().first && host->text == original,
                lifecycle ? "lifecycle invalidation cancels real broker request without editing text" :
                            "cursor navigation cancels real broker request without editing text");
            hold = false;
            configure(service.Get(), &injected, client, backend.c_str());
            check(Commit(apply, service.Get(), context.Get(), client, L"这里有错子。") && pump([&]() { return state().first; }),
                "fresh suggestion succeeds after cancelling a delayed broker request");
            invalidate(service.Get());
          }
        }
        broker.Stop();
        configure(service.Get(), &injected, client, backend.c_str());
        const auto before = host->text;
        check(Commit(apply, service.Get(), context.Get(), client, L"你好") && host->text == before + L"你好" && !state().first,
            "missing backend fails closed while Chinese input continues");
        invalidate(service.Get()); document->Pop(TF_POPF_ALL);
      }
    }
  }
}
