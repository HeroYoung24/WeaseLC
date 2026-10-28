// No model, installed backend, registration, or document access. Exercise the
// same owned job/EOF mechanism over two synthetic PowerShell processes only.
static int RunOwnedModelChecks(const wchar_t* powershell, const wchar_t* fixture) {
  unsigned checks = 0, failures = 0;
  auto check = [&](bool valid, const char* text) { ++checks; if (!valid) ++failures; std::cout << (valid ? "PASS " : "FAIL ") << text << '\n'; };
  wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, _countof(executable));
  std::wstring report(executable); report.resize(report.find_last_of(L'\\') + 1);
  report += L"owned-model-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
  correction::BrokerProcess owned;
  check(!owned.Start(powershell, L"", 5), "owned launch refuses an unbounded child count");
  const auto args = L"-NoProfile -NonInteractive -File \"" + std::wstring(fixture) + L"\" -Report \"" + report + L"\"";
  check(owned.Start(powershell, args, 2), "synthetic model-style launcher starts in its bounded job");
  DWORD childId = 0; const auto deadline = GetTickCount64() + 10000;
  while (owned.Process() && WaitForSingleObject(owned.Process(), 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
    std::ifstream input{std::filesystem::path(report)}; input >> childId;
    if (childId) break;
    Sleep(20);
  }
  check(childId && owned.ContainsProcess(childId), "only the launcher's actual job child can be recognized as owned");
  check(!owned.ContainsProcess(GetCurrentProcessId()), "an unrelated process cannot be adopted by PID");
  HANDLE child = childId ? OpenProcess(SYNCHRONIZE, FALSE, childId) : nullptr;
  HANDLE parent = nullptr;
  if (owned.Process()) DuplicateHandle(GetCurrentProcess(), owned.Process(), GetCurrentProcess(), &parent,
      SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
  // Let the fixture enter its redirected input wait. It cannot consume keyboard input.
  Sleep(600);
  owned.Stop();
  check(parent && WaitForSingleObject(parent, 3000) == WAIT_OBJECT_0, "closing the owned stdin stops the launcher");
  DWORD exitCode = MAXDWORD;
  check(parent && GetExitCodeProcess(parent, &exitCode) && exitCode == 0,
      "model-style launcher handles EOF cooperatively before the job fallback");
  check(child && WaitForSingleObject(child, 3000) == WAIT_OBJECT_0, "owned descendant is gone after stop");
  owned.Stop(); check(!owned.Process(), "owned model stop is idempotent");
  if (parent) CloseHandle(parent); if (child) CloseHandle(child);
  DeleteFileW(report.c_str());
  std::cout << "Owned model checks = " << checks << ", failures = " << failures << '\n';
  return failures ? 1 : 0;
}
