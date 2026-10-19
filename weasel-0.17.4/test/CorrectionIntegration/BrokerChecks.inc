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
    std::atomic_bool fail{false};
    class Fixture final : public IModelTransport {
     public:
      explicit Fixture(std::atomic_bool& fail) : fail_(fail) {}
      TransportResult Post(const ModelConfig& config, const Endpoint& endpoint, const std::string& body,
          const std::atomic_bool& cancelled) override {
        if (fail_.load()) { TransportResult result; result.state = State::Timeout; return result; }
        FixedAutomaticTransport fixed(true); return fixed.Post(config, endpoint, body, cancelled);
      }
     private: std::atomic_bool& fail_;
    };
    CorrectionBroker broker(registry, [&]() { return std::make_unique<Fixture>(fail); });
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
          const auto deadline = GetTickCount64() + 3000;
          while (GetTickCount64() < deadline) {
            poll(service.Get()); MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (done()) return true;
            Sleep(5);
          }
          return false;
        };
        check(pump([&]() { return state().first; }), "capture to broker to parser to frontend becomes ready without injection");
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
        saved.automatic.enabled = true; save(); fail = true;
        configure(service.Get(), &injected, client, backend.c_str());
        Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
        using Diagnostic = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, AutomaticDiagnostic*);
        const auto diagnostic = reinterpret_cast<Diagnostic>(GetProcAddress(module, "WeaselProbeAutomaticDiagnostic"));
        check(diagnostic && pump([&]() { AutomaticDiagnostic result; return diagnostic(service.Get(), &result) == S_OK && result.stage == AutomaticStage::WorkerFailed; }) &&
            !state().first, "backend timeout reaches WorkerFailed rather than a valid no-change result");
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
