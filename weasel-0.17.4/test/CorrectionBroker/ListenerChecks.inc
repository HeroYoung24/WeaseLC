// Own pipes and PID-scoped settings only. The hook pauses before the first
// ConnectNamedPipe so a disconnected client is reproducible without timing luck.
int RunListenerChecks() {
  const auto root = L"Software\\WeaseLC-Listener-Check-" + std::to_wstring(GetCurrentProcessId());
  HKEY existing = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, KEY_READ, &existing) == ERROR_SUCCESS) {
    RegCloseKey(existing); return 3;
  }
  struct Cleanup { std::wstring path; ~Cleanup() { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); } } cleanup{root};
  Preferences preferences; preferences.mode = Mode::Local; preferences.automatic.enabled = true;
  preferences.local.model = L"compiled-fixture";
  if (!Save(root, preferences)) return 4;
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks; if (!ok) ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  };
  auto open = []() {
    const auto deadline = GetTickCount64() + 1000;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    do {
      pipe = CreateFileW(CorrectionPipeName().c_str(), weasel::kPipeClientAccess, 0, nullptr,
          OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
      if (pipe != INVALID_HANDLE_VALUE) return pipe;
      Sleep(1);
    } while (GetTickCount64() < deadline);
    return pipe;
  };
  auto alive = []() {
    BrokerCall call; BrokerRequest request; BrokerReply reply;
    const auto deadline = GetTickCount64() + 1500;
    do {
      if (call.Exchange(Self(), request, reply, 100) && reply.status == BrokerStatus::Ok) return true;
      Sleep(5);
    } while (GetTickCount64() < deadline);
    return false;
  };
  Fixture fixture;
  {
    std::atomic_bool first{true};
    CorrectionBroker broker(root, [&]() { return std::make_unique<Transport>(fixture); }, [&]() {
      if (first.exchange(false)) throw 1;  // Only the test build has this hook.
    });
    const bool started = broker.Start();
    const auto deadline = GetTickCount64() + 1000;
    while (broker.IsRunning() && GetTickCount64() < deadline) Sleep(1);
    check(started && !broker.IsRunning(), "terminal listener failure is observable despite live owner");
    broker.Stop();
    check(broker.Start() && broker.IsRunning() && alive(), "stopped failed listener can restart without stale endpoint");
    broker.Stop();
  }
  for (bool payload : {false, true}) {
    HANDLE reached = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!reached || !resume) { if (reached) CloseHandle(reached); if (resume) CloseHandle(resume); return 5; }
    std::atomic_bool once{false};
    CorrectionBroker broker(root, [&]() { return std::make_unique<Transport>(fixture); }, [&]() {
      if (!once.exchange(true)) { SetEvent(reached); WaitForSingleObject(resume, 3000); }
    });
    const bool started = broker.Start();
    check(started && WaitForSingleObject(reached, 1000) == WAIT_OBJECT_0, "listener paused before accepting owned early-disconnect client");
    HANDLE pipe = started ? open() : INVALID_HANDLE_VALUE;
    bool sent = pipe != INVALID_HANDLE_VALUE;
    if (sent && payload) {
      BrokerRequest notice; notice.operation = BrokerOperation::Metadata;
      notice.metadata = BrokerMetadata::Gate; notice.metadataBytes = sizeof(FrontendDiagnostic);
      FrontendDiagnostic meta; meta.stage = 701; memcpy(notice.data, &meta, sizeof(meta));
      OVERLAPPED io{}; io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); DWORD bytes = 0;
      const BOOL written = WriteFile(pipe, &notice, sizeof(notice), &bytes, &io);
      if (!written && GetLastError() == ERROR_IO_PENDING) {
        const bool completed = WaitForSingleObject(io.hEvent, 1000) == WAIT_OBJECT_0;
        if (!completed) CancelIoEx(pipe, &io);
        sent = GetOverlappedResult(pipe, &io, &bytes, TRUE) && completed;
      } else sent = written != FALSE;
      sent = sent && bytes == sizeof(notice);
      if (io.hEvent) CloseHandle(io.hEvent);
    }
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    SetEvent(resume);
    check(sent && alive(), payload ? "one-way write then disconnect before accept preserves listener" :
        "empty disconnect before accept preserves listener");
    broker.Stop(); CloseHandle(reached); CloseHandle(resume);
  }
  {
    CorrectionBroker broker(root, [&]() { return std::make_unique<Transport>(fixture); });
    const bool started = broker.Start(); check(started && broker.IsRunning() && alive(), "capacity fixture starts with responsive settings");
    std::vector<HANDLE> clients;
    if (started) for (unsigned i = 0; i < 17; ++i) {
      const auto pipe = open(); if (pipe == INVALID_HANDLE_VALUE) break;
      clients.push_back(pipe); Sleep(3);
    }
    check(clients.size() == 17, "seventeen stalled clients exercise admission boundary");
    for (auto pipe : clients) CloseHandle(pipe);
    check(alive(), "listener recovers after stalled clients hit worker capacity");
    unsigned churn = 0;
    for (; churn < 100; ++churn) {
      const auto pipe = open(); if (pipe == INVALID_HANDLE_VALUE) break;
      CloseHandle(pipe);
    }
    check(churn == 100 && alive(), "hundred rapid connect-disconnect clients leave endpoint responsive");
    BrokerCall call; BrokerRequest request; BrokerReply settings, reply;
    const bool model = call.Exchange(Self(), request, settings, 1000) &&
        call.Exchange(Self(), CheckRequestFor(settings), reply, 2000) &&
        reply.status == BrokerStatus::Ok && reply.result.count == 1;
    check(model, "fixed model/parser returns suggestion after connection churn");
    const auto start = GetTickCount64(); broker.Stop();
    check(GetTickCount64() - start < 1000, "listener lifecycle regression shuts down promptly");
  }
  std::cout << "Listener checks=" << checks << " failures=" << failures << std::endl;
  return failures ? 1 : 0;
}
