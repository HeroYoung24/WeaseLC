#include "CorrectionBroker.h"
#include <CorrectionBrokerWire.h>
#include <CorrectionProbe.h>
#include <CorrectionLexicon.h>
#include <CorrectionPipelineDiagnostics.h>
#include "../WeaselDeployer/CorrectionPreferences.h"
#include <atomic>
#include <thread>
#include <vector>
#include <cmath>
#include <cstring>
#ifdef WEASELC_LOCAL_TEST_BUILD
#include "../test/LocalDeployment/FixedAutomaticTransport.h"
#endif

namespace correction {
namespace {
struct Handle {
  HANDLE value = nullptr;
  explicit Handle(HANDLE handle = nullptr) : value(handle) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};
bool Transfer(HANDLE pipe, void* data, DWORD size, bool read, HANDLE stop, DWORD budget = 1000) {
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!event.value) return false;
  OVERLAPPED io{}; io.hEvent = event.value; DWORD bytes = 0;
  const BOOL result = read ? ReadFile(pipe, data, size, &bytes, &io) : WriteFile(pipe, data, size, &bytes, &io);
  if (result) return bytes == size;
  if (GetLastError() != ERROR_IO_PENDING) return false;
  HANDLE events[]{stop, event.value};
  if (WaitForMultipleObjects(2, events, FALSE, budget) != WAIT_OBJECT_0 + 1) {
    CancelIoEx(pipe, &io); GetOverlappedResult(pipe, &io, &bytes, TRUE); return false;
  }
  return GetOverlappedResult(pipe, &io, &bytes, FALSE) && bytes == size;
}
bool Unpackaged() {
  using GetPackage = LONG(WINAPI*)(UINT32*, PWSTR);
  const auto get = reinterpret_cast<GetPackage>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFullName"));
  UINT32 size = 0;
  return !get || get(&size, nullptr) == APPMODEL_ERROR_NO_PACKAGE;
}
ULONGLONG RegistryRevision(const std::wstring& path) {
  HKEY key = nullptr; FILETIME time{};
  if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return 0;
  const auto status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr, nullptr, nullptr, nullptr, &time);
  RegCloseKey(key);
  return status == ERROR_SUCCESS ? (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime : 0;
}
}
struct CorrectionBroker::Impl {
  std::wstring path, name;
  std::function<std::unique_ptr<IModelTransport>()> transport;
  weasel::PipeAccessPolicy access;
  Handle stop;
  std::thread listener;
  std::atomic<unsigned> checks{0};
  struct Worker { std::thread thread; std::atomic_bool done{false}; };
  std::vector<std::unique_ptr<Worker>> workers;
  Impl(std::wstring settings, std::function<std::unique_ptr<IModelTransport>()> factory)
      : path(settings.empty() ? kRegistryKey : std::move(settings)), name(CorrectionPipeName()),
        transport(std::move(factory)), stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
  void PrepareDiagnostics() {
    // Creating the child key changes its parent's FILETIME. Do this before
    // taking a settings revision, never midway through an inference request.
    HKEY parent = nullptr, child = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_CREATE_SUB_KEY, &parent) != ERROR_SUCCESS) return;
    if (RegCreateKeyExW(parent, L"Diagnostics", 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &child, nullptr) == ERROR_SUCCESS) RegCloseKey(child);
    RegCloseKey(parent);
  }
  void Record(const wchar_t* entry, const void* value, DWORD bytes) {
    HKEY key = nullptr; const auto diagnostic = path + L"\\Diagnostics";
    // Never recreate removed settings on a late request after rollback.
    if (RegOpenKeyExW(HKEY_CURRENT_USER, diagnostic.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegSetValueExW(key, entry, 0, REG_BINARY, static_cast<const BYTE*>(value), bytes); RegCloseKey(key);
  }
  BrokerStatus Snapshot(BrokerSettings& saved, Preferences* preferences = nullptr) {
    PrepareDiagnostics();
    const auto before = RegistryRevision(path);
    PreferencesRead detail; auto value = ReadPreferences(path.c_str(), &detail);
    const auto after = RegistryRevision(path);
    if (!before || before != after || detail.stage != PreferencesStage::Ready) return BrokerStatus::Unavailable;
    saved.revision = before;
    saved.enabled = value.mode == Mode::Local && value.automatic.enabled &&
        value.local.provider == Provider::ChatCompletions && value.local.bearerToken.empty() && ValidateConfig(value.local);
    const auto& a = value.automatic;
    saved.timing = value.timing; saved.tab = a.tab; saved.side = a.side; saved.gap = a.gap;
    saved.width = a.width; saved.font = a.font; saved.debounce = a.debounce;
    saved.timeoutSeconds = value.local.timeoutSeconds; saved.threshold = EffectiveThreshold(value.local);
    if (preferences) *preferences = std::move(value);
    return BrokerStatus::Ok;
  }
  void Metadata(const BrokerRequest& request) {
    DWORD version = 0; memcpy(&version, request.data, sizeof(version));
    if (!MetadataSize(request.metadata) || request.metadataBytes != MetadataSize(request.metadata) ||
        version != 1 || !RegistryRevision(path)) return;
    if (request.metadata == BrokerMetadata::ScopeTrace) {
      DWORD phase=0;memcpy(&phase,request.data+sizeof(DWORD),sizeof(phase));
      const wchar_t* entries[]{L"LastScopeBeforeEnd",L"LastScopeAfterEnd",L"LastScopeReadEdit"};
      if(phase>2)return;
      PrepareDiagnostics();Record(entries[phase],request.data,request.metadataBytes);return;
    }
    const wchar_t* names[]{L"LastKey",L"LastCommit",L"LastGate",L"LastCapture",L"LastAutomatic",L"LastScope"};
    PrepareDiagnostics();
    Record(names[static_cast<unsigned>(request.metadata)], request.data, request.metadataBytes);
  }
  void Check(HANDLE pipe, const BrokerRequest& request, BrokerReply& reply) {
    if (request.input.magic != 0x31434357 || request.input.version != 1 ||
        !ValidAutomaticText(request.input.text, request.input.units, 1024)) {
      reply.status = BrokerStatus::InvalidRequest; return;
    }
    Preferences preferences;
    reply.status = Snapshot(reply.settings, &preferences);
    if (reply.status != BrokerStatus::Ok) return;
    if (!reply.settings.enabled) { reply.status = BrokerStatus::Disabled; return; }
    if (request.revision != reply.settings.revision) { reply.status = BrokerStatus::SettingsChanged; return; }
    if (checks.fetch_add(1) >= 4) { --checks; reply.status = BrokerStatus::Busy; return; }
    struct Permit { std::atomic<unsigned>& count; ~Permit() { --count; } } permit{checks};
    const auto started = GetTickCount64();
    Handle finished(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!finished.value) { reply.status = BrokerStatus::Unavailable; return; }
    std::atomic_bool cancelled{false};
    std::thread monitor([&]() {
      HANDLE events[]{stop.value, finished.value};
      for (;;) {
        const auto state = WaitForMultipleObjects(2, events, FALSE, 20);
        if (state == WAIT_OBJECT_0 + 1) return;
        DWORD available = 0;
        if (state == WAIT_OBJECT_0 || !PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available ||
            RegistryRevision(path) != request.revision) { cancelled.store(true); return; }
      }
    });
    struct Finish {
      HANDLE event; std::thread& thread;
      ~Finish() { SetEvent(event); thread.join(); }
    } finish{finished.value, monitor};
    CheckRequest input; input.session = request.requestId; input.version = request.revision;
    input.safeText = true; input.source = Source::CommittedText;
    input.text.assign(request.input.text, request.input.units);
    struct Erase { std::wstring& text; ~Erase() { if (!text.empty()) SecureZeroMemory(text.data(), text.size() * sizeof(WCHAR)); } } erase{input.text};
    std::unique_ptr<IModelTransport> selected;
    if (transport) selected = transport();
#ifdef WEASELC_LOCAL_TEST_BUILD
    else if (UseFixedAutomaticReply()) {
      const auto verdict = FixedAutomaticVerdict(input.text);
      if (verdict == FixedVerdict::Reject) { reply.status = BrokerStatus::InvalidRequest; return; }
      selected = std::make_unique<FixedAutomaticTransport>(verdict == FixedVerdict::Suggest);
    }
#endif
    auto client = CreateModelClient(Provider::ChatCompletions, std::move(selected));
    auto result = client->Check(preferences.local, input, cancelled);
    reply.modelState = static_cast<DWORD>(result.state);
    reply.nativeError = result.nativeError; reply.httpStatus = result.httpStatus;
    reply.elapsedMs = static_cast<DWORD>(GetTickCount64() - started);
    if (RegistryRevision(path) != request.revision) { reply.status = BrokerStatus::SettingsChanged; return; }
    if (cancelled.load()) { reply.status = BrokerStatus::ModelFailure; reply.modelState = static_cast<DWORD>(State::Cancelled); return; }
    HKEY key = nullptr; UserLexicon lexicon;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
      reply.status = BrokerStatus::Unavailable; return;
    }
    const bool loaded = ReadUserLexiconValue(key, lexicon); RegCloseKey(key);
    if (!loaded) { reply.status = BrokerStatus::Unavailable; return; }
    FilterKeptTerms(lexicon, input, result);
    reply.modelState = static_cast<DWORD>(result.state);
    if (result.state != State::Suggestions && result.state != State::NoSuggestion && result.state != State::Filtered) {
      reply.status = BrokerStatus::ModelFailure; return;
    }
    for (const auto& suggestion : result.suggestions) {
      if (reply.result.count == 8 || suggestion.original.size() > 32 || suggestion.replacement.size() > 32) continue;
      auto& item = reply.result.items[reply.result.count++];
      item.start = static_cast<DWORD>(suggestion.start);
      item.originalUnits = static_cast<DWORD>(suggestion.original.size());
      item.replacementUnits = static_cast<DWORD>(suggestion.replacement.size()); item.score = suggestion.score;
      memcpy(item.original, suggestion.original.data(), suggestion.original.size() * sizeof(WCHAR));
      memcpy(item.replacement, suggestion.replacement.data(), suggestion.replacement.size() * sizeof(WCHAR));
    }
    if (RegistryRevision(path) != request.revision) { reply.result = {}; reply.status = BrokerStatus::SettingsChanged; }
  }
  void Serve(HANDLE pipe) {
    Handle owned(pipe); BrokerRequest request; BrokerReply reply;
    struct Erase { BrokerRequest& request; BrokerReply& reply; ~Erase() {
      SecureZeroMemory(&request, sizeof(request)); SecureZeroMemory(&reply, sizeof(reply));
    } } erase{request, reply};
    if (!Transfer(pipe, &request, sizeof(request), true, stop.value) || !access.AllowsClient(pipe) ||
        request.magic != 0x32424357 || request.version != 2) return;
    reply.requestId = request.requestId;
    try {
      switch (request.operation) {
        case BrokerOperation::Settings: reply.status = Snapshot(reply.settings); break;
        case BrokerOperation::Check: Check(pipe, request, reply); break;
        case BrokerOperation::Metadata: Metadata(request); return;
        default: reply.status = BrokerStatus::InvalidRequest;
      }
    } catch (...) { reply.status = BrokerStatus::ModelFailure; reply.modelState = static_cast<DWORD>(State::InvalidResponse); reply.result = {}; }
    if (request.operation == BrokerOperation::Check) {
      // A correlated outcome is persisted by the controller, in its own view.
      BrokerDiagnostic outcome;
      outcome.status = static_cast<DWORD>(reply.status); outcome.units = request.input.units;
      outcome.state = reply.modelState; outcome.error = reply.nativeError; outcome.http = reply.httpStatus;
      outcome.elapsed = reply.elapsedMs; outcome.requestId = request.requestId; outcome.revision = request.revision;
      GetSystemTimeAsFileTime(&outcome.time);
      Record(L"LastBrokerCheck", &outcome, sizeof(outcome));
    }
    Transfer(pipe, &reply, sizeof(reply), false, stop.value);
    // No FlushFileBuffers: a stalled client cannot block backend shutdown.
  }
  HANDLE NewPipe(bool first) {
    return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0), PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
        PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 17, 4096, 4096, 0, access.get_attr());
  }
  void Listen(HANDLE initial) {
    Handle next(initial);
    while (WaitForSingleObject(stop.value, 0) == WAIT_TIMEOUT) {
      Handle pipe(next.value); next.value = INVALID_HANDLE_VALUE;
      Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
      if (!event.value) break;
      OVERLAPPED io{}; io.hEvent = event.value;
      const BOOL connected = ConnectNamedPipe(pipe.value, &io);
      const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
      if (error == ERROR_IO_PENDING) {
        HANDLE events[]{stop.value, event.value};
        const auto state = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        DWORD bytes = 0;
        if (state != WAIT_OBJECT_0 + 1) { CancelIoEx(pipe.value, &io); GetOverlappedResult(pipe.value, &io, &bytes, TRUE); break; }
        if (!GetOverlappedResult(pipe.value, &io, &bytes, FALSE)) break;
      } else if (error != ERROR_SUCCESS && error != ERROR_PIPE_CONNECTED) break;
      for (auto it = workers.begin(); it != workers.end();) {
        if ((*it)->done.load()) { (*it)->thread.join(); it = workers.erase(it); } else ++it;
      }
      // Keep a listening instance alive before retiring a connected one.
      next.value = NewPipe(false);
      if (next.value == INVALID_HANDLE_VALUE) break;
      if (workers.size() >= 16) continue;
      auto worker = std::make_unique<Worker>(); auto* state = worker.get();
      workers.push_back(std::move(worker));
      const auto accepted = pipe.value;
      try {
        state->thread = std::thread([this, state, accepted]() {
          try { Serve(accepted); } catch (...) {}
          state->done.store(true);
        });
        pipe.value = INVALID_HANDLE_VALUE;
      } catch (...) { workers.pop_back(); }
    }
  }
};
CorrectionBroker::CorrectionBroker(std::wstring path, std::function<std::unique_ptr<IModelTransport>()> factory)
    : impl_(std::make_unique<Impl>(std::move(path), std::move(factory))) {}
CorrectionBroker::~CorrectionBroker() { Stop(); }
bool CorrectionBroker::Start() {
  if (impl_->listener.joinable() || !Unpackaged() || impl_->name.empty() || !impl_->stop.value) return false;
  ResetEvent(impl_->stop.value);
  HANDLE pipe = impl_->NewPipe(true);
  if (pipe == INVALID_HANDLE_VALUE) return false;
  try { impl_->listener = std::thread([this, pipe]() { try { impl_->Listen(pipe); } catch (...) {} }); }
  catch (...) { CloseHandle(pipe); return false; }
  return true;
}
void CorrectionBroker::Stop() {
  if (!impl_ || !impl_->listener.joinable()) return;
  SetEvent(impl_->stop.value); impl_->listener.join();
  for (auto& worker : impl_->workers) if (worker->thread.joinable()) worker->thread.join();
  impl_->workers.clear();
}
}
