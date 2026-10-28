#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace correction {
// The unpackaged input backend owns one correction-only child. Closing stdin
// requests orderly shutdown; the job is the fallback for a stuck/crashed child.
class BrokerProcess {
 public:
  ~BrokerProcess() { Stop(); }
  BrokerProcess() = default;
  BrokerProcess(const BrokerProcess&) = delete;
  BrokerProcess& operator=(const BrokerProcess&) = delete;
  HANDLE Process() const { return process_; }
  bool ContainsProcess(DWORD id) const {
    if (!job_ || !id) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    BOOL contained = FALSE;
    const bool valid = process && IsProcessInJob(process, job_, &contained) && contained;
    if (process) CloseHandle(process); return valid;
  }
  bool Start(const std::wstring& executable, const std::wstring& arguments = L"/correction-broker",
      DWORD processLimit = 1) {
    Stop();
    if (!processLimit || processLimit > 4 || executable.find(L'"') != std::wstring::npos ||
        executable.find_first_of(L"\r\n") != std::wstring::npos) return false;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE input = nullptr, null = INVALID_HANDLE_VALUE;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    PROCESS_INFORMATION child{}; SIZE_T bytes = 0; std::vector<BYTE> storage;
    auto cleanup = [&]() {
      if (startup.lpAttributeList) DeleteProcThreadAttributeList(startup.lpAttributeList);
      startup.lpAttributeList = nullptr;
      if (input) CloseHandle(input);
      input = nullptr;
      if (null != INVALID_HANDLE_VALUE) CloseHandle(null);
      null = INVALID_HANDLE_VALUE;
      if (child.hThread) CloseHandle(child.hThread);
      child.hThread = nullptr;
      if (child.hProcess) CloseHandle(child.hProcess);
      child.hProcess = nullptr;
    };
    struct Cleanup { decltype(cleanup)& action; ~Cleanup() { action(); } } guard{cleanup};
    if (!CreatePipe(&input, &write_, &security, 4096) || !SetHandleInformation(write_, HANDLE_FLAG_INHERIT, 0)) {
      cleanup(); Stop(); return false;
    }
    null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes); storage.resize(bytes);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (null == INVALID_HANDLE_VALUE || !InitializeProcThreadAttributeList(list, 1, 0, &bytes)) { cleanup(); Stop(); return false; }
    startup.lpAttributeList = list; HANDLE inherited[]{input, null};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
      cleanup(); Stop(); return false;
    }
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_FORCEOFFFEEDBACK;
    startup.StartupInfo.hStdInput = input; startup.StartupInfo.hStdOutput = null; startup.StartupInfo.hStdError = null;
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = processLimit;
    const auto slash = executable.find_last_of(L'\\');
    auto command = L"\"" + executable + L"\" " + arguments;
    const auto directory = executable.substr(0, slash);
    if (slash == std::wstring::npos || !job_ ||
        !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(),
            &startup.StartupInfo, &child)) { cleanup(); Stop(); return false; }
    if (!AssignProcessToJobObject(job_, child.hProcess) || ResumeThread(child.hThread) == static_cast<DWORD>(-1)) {
      TerminateProcess(child.hProcess, 1); cleanup(); Stop(); return false;
    }
    process_ = child.hProcess; child.hProcess = nullptr; cleanup(); return true;
  }
  void Stop() {
    if (write_) { CloseHandle(write_); write_ = nullptr; }
    if (process_) WaitForSingleObject(process_, 2000);
    if (job_) { CloseHandle(job_); job_ = nullptr; }
    if (process_) { WaitForSingleObject(process_, 1000); CloseHandle(process_); process_ = nullptr; }
  }
 private:
  HANDLE job_ = nullptr, process_ = nullptr, write_ = nullptr;
};
}
