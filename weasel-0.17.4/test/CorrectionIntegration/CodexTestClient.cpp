#include <windows.h>
#include <algorithm>
#include <CodexTestClient.h>

namespace {
using namespace correction;
struct Handle {
  HANDLE value = nullptr;
  Handle() = default;
  explicit Handle(HANDLE h) : value(h) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  void Close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
std::wstring Quote(const std::wstring& argument) {
  std::wstring quoted = L"\"";
  std::size_t slashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\\') { ++slashes; continue; }
    quoted.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    quoted += c; slashes = 0;
  }
  quoted.append(slashes * 2, L'\\');
  return quoted + L'"';
}
class CodexClient final : public IModelClient {
 public:
  CodexClient(std::wstring executable, std::wstring schema, std::wstring directory)
      : executable_(std::move(executable)), schema_(std::move(schema)), directory_(std::move(directory)) {}
  CheckResult Check(const ModelConfig& config, const CheckRequest& request,
      const std::atomic_bool& cancelled) override {
    CheckResult result;
    result.session = request.session; result.version = request.version;
    result.state = State::InvalidConfig;
    if (cancelled.load()) { result.state = State::Cancelled; return result; }
    if (config.mode == Mode::Off) { result.state = State::Off; return result; }
    if (!request.safeText) { result.state = State::ProtectedInput; return result; }
    if (!IsSyntheticModelInput(request.text) || !request.before.empty() || !request.after.empty() ||
        request.source != Source::CommittedText) { result.state = State::InvalidInput; return result; }
    const auto attributes = GetFileAttributesW(executable_.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        GetFileAttributesW(schema_.c_str()) == INVALID_FILE_ATTRIBUTES) return result;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    Handle inputRead, inputWrite, outputRead, outputWrite, errorRead, errorWrite;
    if (!CreatePipe(&inputRead.value, &inputWrite.value, &sa, 16384) ||
        !CreatePipe(&outputRead.value, &outputWrite.value, &sa, 16384) ||
        !CreatePipe(&errorRead.value, &errorWrite.value, &sa, 16384)) {
      result.nativeError = GetLastError(); return result;
    }
    if (!SetHandleInformation(inputWrite.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(outputRead.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(errorRead.value, HANDLE_FLAG_INHERIT, 0)) return result;
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit = {};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limit, sizeof(limit))) return result;
    // CLI defaults retain the user's selected provider/model, but tools, apps,
    // MCP servers, hooks and project instructions are disabled for pure inference.
    std::wstring command = Quote(executable_) + L" exec --skip-git-repo-check --ephemeral --sandbox read-only"
        L" --color never --disable shell_tool --disable multi_agent --disable apps --disable computer_use"
        L" --disable browser_use --disable hooks -c mcp_servers={} -c project_doc_max_bytes=0"
        L" --output-schema " + Quote(schema_) + L" -";
    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = inputRead.value;
    startup.StartupInfo.hStdOutput = outputWrite.value;
    startup.StartupInfo.hStdError = errorWrite.value;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> attributesBuffer(bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributesBuffer.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) return result;
    HANDLE inherited[] = {inputRead.value, outputWrite.value, errorWrite.value};
    const bool updated = UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
    PROCESS_INFORMATION info = {};
    const bool started = updated && CreateProcessW(executable_.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        directory_.c_str(), &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!started) { result.nativeError = GetLastError(); return result; }
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job.value, process.value)) {
      result.nativeError = GetLastError(); TerminateProcess(process.value, ERROR_CANCELLED); return result;
    }
    inputRead.Close(); outputWrite.Close(); errorWrite.Close();
    ResumeThread(thread.value);
    std::string prompt = CorrectionInstructions();
    prompt += "\nPure text inference only. Do not use tools, browse, read files or change settings. "
              "Return only the requested JSON object.\n" + BuildCorrectionInput(request);
    DWORD written = 0;
    if (!WriteFile(inputWrite.value, prompt.data(), static_cast<DWORD>(prompt.size()), &written, nullptr) ||
        written != prompt.size()) { result.state = State::TransportError; return result; }
    inputWrite.Close();
    std::string output;
    const auto deadline = GetTickCount64() + 180000;
    bool overflow = false;
    auto drain = [&](HANDLE pipe, bool keep) {
      for (unsigned i = 0; i < 64; ++i) {
        DWORD available = 0, received = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
        char buffer[4096];
        if (!ReadFile(pipe, buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &received, nullptr)) break;
        if (keep) {
          if (output.size() + received > kMaxResponseBytes) { overflow = true; break; }
          output.append(buffer, received);
        }
      }
    };
    for (;;) {
      drain(outputRead.value, true); drain(errorRead.value, false);
      if (cancelled.load() || GetTickCount64() >= deadline || overflow) {
        result.state = cancelled.load() ? State::Cancelled : overflow ? State::InvalidResponse : State::Timeout;
        return result;  // Job handle kills this invocation and descendants.
      }
      if (WaitForSingleObject(process.value, 25) == WAIT_OBJECT_0) break;
    }
    drain(outputRead.value, true); drain(errorRead.value, false);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.value, &exitCode);
    if (exitCode != 0) { result.state = State::TransportError; result.nativeError = exitCode; return result; }
    if (overflow) { result.state = State::InvalidResponse; return result; }
    if (cancelled.load()) { result.state = State::Cancelled; return result; }
    return ParseCorrectionAnswer(output, config, request);
  }
 private:
  std::wstring executable_, schema_, directory_;
};
}  // namespace
bool IsSyntheticModelInput(const std::wstring& text) {
  return text == L"你好。" || text == L"这里有错子。";
}
std::unique_ptr<correction::IModelClient> CreateCodexTestClient(const std::wstring& executable,
    const std::wstring& schema, const std::wstring& directory) {
  return std::make_unique<CodexClient>(executable, schema, directory);
}
