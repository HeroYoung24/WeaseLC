static int RunServiceControlChecks() {
  unsigned checks = 0, failures = 0;
  auto check = [&](bool good, const char* label) {
    ++checks; if (!good) ++failures;
    std::cout << (good ? "PASS " : "FAIL ") << label << std::endl;
  };
  const auto name = ServicePipeName() + L".test." + std::to_wstring(GetCurrentProcessId());
  auto query = [&](bool retry, ServiceReply& reply, const std::wstring& server = Self()) {
    // A one-instance endpoint may be disconnecting its previous client.
    for (unsigned i = 0; i < 40; ++i) {
      if (QueryCorrectionService(server, retry, reply, nullptr, name)) return true;
      Sleep(5);
    }
    return false;
  };
  BrokerSupervisor owner;
  CorrectionServiceControl control(owner), duplicate(owner);
  check(control.Start(Self(), name), "owned management endpoint starts without input registration");
  check(!duplicate.Start(Self(), name), "second endpoint cannot replace the first pipe instance");
  ServiceReply reply;
  check(query(false, reply) && reply.stage == static_cast<DWORD>(BrokerProcessStage::Stopped),
      "authenticated fixed-size snapshot reports stopped state");
  check(query(true, reply) && !reply.accepted && owner.Snapshot().stage == BrokerProcessStage::Stopped,
      "recovery cannot start an uninitialized owner");
  check(!query(false, reply, Self() + L".wrong"), "management client rejects mismatched server identity");
  auto raw = [&](ServiceRequest request) {
    using namespace service_detail;
    Handle pipe;
    for (unsigned i = 0; i < 40; ++i) {
      pipe.value = CreateFileW(name.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING,
          FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
      if (pipe.value != INVALID_HANDLE_VALUE) break;
      Sleep(5);
    }
    ServiceReply ignored;
    return pipe.value != INVALID_HANDLE_VALUE && Transfer(pipe.value, &request, sizeof(request), false, nullptr) &&
        Transfer(pipe.value, &ignored, sizeof(ignored), true, nullptr);
  };
  ServiceRequest invalid; invalid.version = 999;
  check(!raw(invalid), "unsupported management version is rejected");
  invalid = {}; invalid.operation = 9;
  check(!raw(invalid), "arbitrary control operations are rejected");
  bool disconnects = true;
  for (unsigned i = 0; i < 10; ++i) {
    HANDLE pipe = CreateFileW(name.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    disconnects = query(false, reply) && disconnects;
  }
  check(disconnects, "early disconnects do not retire management listener");
  constexpr auto env = L"WEASELC_BROKER_SUPERVISOR_FIXTURE";
  if (GetEnvironmentVariableW(env, nullptr, 0)) return 3;
  struct RemoveEnvironment { ~RemoveEnvironment() { SetEnvironmentVariableW(L"WEASELC_BROKER_SUPERVISOR_FIXTURE", nullptr); } } remove;
  SetEnvironmentVariableW(env, L"crash");
  BrokerRestartPolicy fast; fast.delays = {20, 30, 40};
  bool exhausted = owner.Start(Self(), fast);
  const auto deadline = GetTickCount64() + 3000;
  while (exhausted && owner.Snapshot().stage != BrokerProcessStage::Exhausted && GetTickCount64() < deadline) Sleep(5);
  check(exhausted && query(false, reply) && reply.retryAvailable && reply.attempts == 4 && reply.exitCode == 73,
      "status exposes exhausted retry budget after actual child failures");
  SetEnvironmentVariableW(env, L"normal");
  check(query(true, reply) && reply.accepted, "management recovery wakes exhausted supervision worker");
  const auto running = GetTickCount64() + 3000;
  while (owner.Snapshot().stage != BrokerProcessStage::Running && GetTickCount64() < running) Sleep(5);
  const auto pid = owner.Snapshot().processId;
  check(pid && query(true, reply) && !reply.accepted && reply.processId == pid,
      "duplicate recovery does not replace a running coordinator");
  service_detail::Handle cancel(CreateEventW(nullptr, TRUE, TRUE, nullptr));
  check(!QueryCorrectionService(Self(), true, reply, cancel.value, name) && owner.Snapshot().processId == pid,
      "cancelled UI request cannot recover or affect the child");
  control.Stop(); owner.Stop();
  check(!QueryCorrectionService(Self(), false, reply, nullptr, name), "shutdown removes management endpoint");
  check(control.Start(Self() + L".wrong", name) && !query(false, reply),
      "server rejects client with mismatched executable identity");
  control.Stop();
  check(control.Start(Self(), name), "management listener can start again after stop");
  service_detail::Handle stalled;
  for (unsigned i = 0; i < 40; ++i) {
    stalled.value = CreateFileW(name.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (stalled.value != INVALID_HANDLE_VALUE) break;
    Sleep(5);
  }
  const auto began = GetTickCount64(); control.Stop();
  check(stalled.value != INVALID_HANDLE_VALUE && GetTickCount64() - began < 500,
      "shutdown cancels idle/stalled client without waiting on the backend owner");
  std::cout << "Service control checks=" << checks << " failures=" << failures << std::endl;
  return failures ? 1 : 0;
}
