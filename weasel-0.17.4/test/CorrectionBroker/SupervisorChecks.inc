// Invoked solely as an owned, hidden child in BrokerProcess's single-child Job.
static int RunSupervisorFixture() {
  BOOL inJob = FALSE;
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (!input || GetFileType(input) != FILE_TYPE_PIPE ||
      !IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) || !inJob) return 2;
  WCHAR mode[32]{};
  if (!GetEnvironmentVariableW(L"WEASELC_BROKER_SUPERVISOR_FIXTURE", mode, _countof(mode))) return 2;
  if (!wcscmp(mode, L"crash")) return 73;
  if (!wcscmp(mode, L"stuck")) { Sleep(10000); return 74; }
  if (wcscmp(mode, L"normal")) return 2;
  BYTE signal = 0; DWORD bytes = 0;
  ReadFile(input, &signal, 1, &bytes, nullptr);
  return 0;
}
static int RunSupervisorChecks() {
  constexpr auto env = L"WEASELC_BROKER_SUPERVISOR_FIXTURE";
  if (GetEnvironmentVariableW(env, nullptr, 0)) return 3;
  struct RemoveEnvironment { ~RemoveEnvironment() { SetEnvironmentVariableW(L"WEASELC_BROKER_SUPERVISOR_FIXTURE", nullptr); } } remove;
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks; if (!ok) ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  };
  auto wait = [](const std::function<bool()>& predicate, DWORD budget = 3000) {
    const auto deadline = GetTickCount64() + budget;
    do { if (predicate()) return true; Sleep(2); } while (GetTickCount64() < deadline);
    return predicate();
  };
  std::vector<HANDLE> observed;
  struct CloseObserved { std::vector<HANDLE>& handles; ~CloseObserved() { for (auto handle : handles) CloseHandle(handle); } } close{observed};
  auto observe = [&](DWORD pid) {
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    WCHAR path[32768]{}; DWORD units = _countof(path);
    if (!handle || !QueryFullProcessImageNameW(handle, 0, path, &units) || _wcsicmp(path, Self().c_str())) {
      if (handle) CloseHandle(handle); return static_cast<HANDLE>(nullptr);
    }
    observed.push_back(handle); return handle;
  };
  auto running = [&](BrokerSupervisor& child, DWORD attempts) {
    return wait([&]() { const auto status = child.Snapshot();
      return status.stage == BrokerProcessStage::Running && status.attempts == attempts && status.processId != 0; });
  };
  BrokerRestartPolicy fast; fast.delays = {40, 80, 120}; fast.stableMilliseconds = 60000;
  SetEnvironmentVariableW(env, L"normal");
  {
    BrokerSupervisor child; bool valid = true;
    for (unsigned i = 0; valid && i < 20; ++i) {
      valid = child.Start(Self(), fast); child.Stop();
      const auto status = child.Snapshot();
      valid = valid && status.stage == BrokerProcessStage::Stopped && !status.processId && status.attempts <= 1;
    }
    check(valid, "immediate shutdown during startup never restarts after Stop returns");
  }
  {
    BrokerSupervisor child;
    const auto began = GetTickCount64();
    check(child.Start(Self(), fast) && GetTickCount64() - began < 200 && running(child, 1),
        "supervision starts asynchronously and owns one running child");
    const DWORD first = child.Snapshot().processId;
    check(!child.Start(Self(), fast) && child.Snapshot().processId == first,
        "second Start cannot replace a running owner");
    HANDLE process = observe(first);
    check(process && TerminateProcess(process, 77) && running(child, 2) && child.Snapshot().processId != first,
        "unexpected exit restarts an owned child after backoff");
    check(child.Snapshot().lastExitCode == 77 && child.Snapshot().consecutiveFailures == 1,
        "recovery retains exit status and consecutive failure count");
    const auto stop = GetTickCount64(); child.Stop();
    check(GetTickCount64() - stop < 1000 && child.Snapshot().stage == BrokerProcessStage::Stopped && !child.Snapshot().processId,
        "Stop requests EOF and joins the supervision worker");
    check(child.Start(Self(), fast) && running(child, 1), "explicit restart begins a fresh bounded retry budget");
    child.Stop();
  }
  SetEnvironmentVariableW(env, L"crash");
  {
    BrokerSupervisor child;
    check(child.Start(Self(), fast) && wait([&]() { return child.Snapshot().stage == BrokerProcessStage::Exhausted; }),
        "continuous child failures exhaust the restart budget");
    const auto ended = child.Snapshot(); Sleep(180);
    check(ended.attempts == 4 && ended.consecutiveFailures == 4 && !ended.processId && !ended.launchFailed &&
        ended.lastExitCode == 73 && child.Snapshot().attempts == 4,
        "initial launch plus three retries cannot become an infinite crash loop");
    child.Stop();
  }
  {
    BrokerSupervisor child;
    const auto missing = Self() + L".missing-broker-test";
    if (GetFileAttributesW(missing.c_str()) != INVALID_FILE_ATTRIBUTES) return 4;
    check(child.Start(missing, fast) && wait([&]() { return child.Snapshot().stage == BrokerProcessStage::Exhausted; }) &&
        child.Snapshot().attempts == 4 && child.Snapshot().launchFailed,
        "missing executable is bounded and distinguished from an exited child");
    child.Stop();
    check(!child.Start(L"relative.exe", fast), "supervisor rejects a relative executable path");
  }
  {
    BrokerSupervisor child; BrokerRestartPolicy slow = fast; slow.delays = {5000, 5000, 5000};
    const bool backingOff = child.Start(Self(), slow) && wait([&]() { return child.Snapshot().stage == BrokerProcessStage::Backoff; });
    const auto stop = GetTickCount64(); child.Stop();
    check(backingOff && GetTickCount64() - stop < 500 && child.Snapshot().attempts == 1,
        "Stop interrupts a long retry delay without another launch");
  }
  SetEnvironmentVariableW(env, L"normal");
  {
    BrokerSupervisor child; BrokerRestartPolicy stable = fast; stable.stableMilliseconds = 150;
    bool valid = child.Start(Self(), stable) && running(child, 1);
    for (DWORD attempt = 1; valid && attempt <= 4; ++attempt) {
      Sleep(180);
      HANDLE process = observe(child.Snapshot().processId);
      valid = process && TerminateProcess(process, 78) && running(child, attempt + 1) &&
          child.Snapshot().consecutiveFailures == 1;
    }
    check(valid && child.Snapshot().attempts == 5, "stable running interval replenishes the consecutive crash budget");
    child.Stop();
  }
  {
    HANDLE process = nullptr;
    { BrokerSupervisor child; const bool started = child.Start(Self(), fast) && running(child, 1);
      process = started ? observe(child.Snapshot().processId) : nullptr; }
    check(process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0, "destructor releases its child without explicit Stop");
  }
  SetEnvironmentVariableW(env, L"stuck");
  {
    BrokerSupervisor child; const bool started = child.Start(Self(), fast) && running(child, 1);
    HANDLE process = started ? observe(child.Snapshot().processId) : nullptr;
    const auto stop = GetTickCount64(); child.Stop();
    check(process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0 && GetTickCount64() - stop < 4000,
        "Job fallback retires an unresponsive owned child on shutdown");
  }
  bool retired = true; for (auto process : observed) retired = retired && WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
  check(retired, "all observed test children exited without orphan processes");
  std::cout << "Supervisor checks=" << checks << " failures=" << failures << std::endl;
  return failures ? 1 : 0;
}
static int RunSupervisedBrokerProcess(const std::wstring& executable) {
  BrokerSupervisor child;
  BrokerCall call; BrokerRequest request; BrokerReply reply;
  unsigned checks = 0, failures = 0;
  auto check = [&](bool good, const char* label) {
    ++checks; if (!good) ++failures;
    std::cout << (good ? "PASS " : "FAIL ") << label << std::endl;
  };
  auto ready = [&](DWORD attempt) {
    const auto deadline = GetTickCount64() + 5000;
    do {
      const auto status = child.Snapshot();
      if (status.stage == BrokerProcessStage::Running && status.attempts == attempt &&
          call.Exchange(executable, request, reply, 100) &&
          (reply.status == BrokerStatus::Ok || reply.status == BrokerStatus::Unavailable)) return true;
      Sleep(5);
    } while (GetTickCount64() < deadline);
    return false;
  };
  auto observe = [&]() {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
        FALSE, child.Snapshot().processId);
    WCHAR path[32768]{}; DWORD units = _countof(path);
    if (!process || !QueryFullProcessImageNameW(process, 0, path, &units) || _wcsicmp(path, executable.c_str())) {
      if (process) CloseHandle(process); return static_cast<HANDLE>(nullptr);
    }
    return process;
  };
  const bool started = child.Start(executable) && ready(1);
  check(started, "actual supervised broker exposes authenticated settings endpoint");
  HANDLE first = started ? observe() : nullptr;
  const bool crashed = first && TerminateProcess(first, 79);
  check(crashed && ready(2), "actual broker restart restores endpoint after owned-child crash");
  check(child.Snapshot().attempts == 2 && child.Snapshot().lastExitCode == 79,
      "actual crash is correlated with one bounded restart");
  HANDLE second = child.Snapshot().stage == BrokerProcessStage::Running ? observe() : nullptr;
  child.Stop();
  DWORD code = 1;
  check(second && WaitForSingleObject(second, 0) == WAIT_OBJECT_0 && GetExitCodeProcess(second, &code) && code == 0,
      "actual supervised broker still exits normally on parent EOF");
  check(first && WaitForSingleObject(first, 0) == WAIT_OBJECT_0 && !call.Exchange(executable, request, reply),
      "crashed and restarted children leave no endpoint after shutdown");
  if (first) CloseHandle(first); if (second) CloseHandle(second);
  std::cout << "Supervised production process checks=" << checks << " failures=" << failures << std::endl;
  return failures ? 1 : 0;
}
