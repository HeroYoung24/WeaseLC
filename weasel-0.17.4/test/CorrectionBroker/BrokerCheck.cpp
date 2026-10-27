#include <CorrectionBrokerClient.h>
#include <CorrectionLexicon.h>
#include <CorrectionPipelineDiagnostics.h>
#include <CorrectionBrokerProcess.h>
#include <CorrectionBrokerSupervisor.h>
#include <CorrectionServiceControl.h>
#include <KeyDispatchDiagnostic.h>
#include "../../WeaselServer/CorrectionBroker.h"
#include "../../WeaselDeployer/CorrectionPreferences.h"
#include "../LocalDeployment/FixedAutomaticTransport.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>

using namespace correction;
struct Fixture {
  std::atomic<unsigned> mode{0}, calls{0}, cancelled{0};
};
class Transport : public IModelTransport {
 public:
  explicit Transport(Fixture& fixture) : fixture_(fixture) {}
  TransportResult Post(const ModelConfig& config, const Endpoint& endpoint,
      const std::string& body, const std::atomic_bool& cancelled) override {
    ++fixture_.calls;
    const auto mode = fixture_.mode.load();
    if (mode == 4 || mode == 5 || mode == 7) {
      const auto end = GetTickCount64() + (mode == 5 ? 11000 : mode == 7 ? 300 : 2000);
      while (GetTickCount64() < end && !cancelled.load()) Sleep(5);
      if (cancelled.load()) { ++fixture_.cancelled; TransportResult result; result.state = State::Cancelled; return result; }
    }
    TransportResult result;
    if (mode == 1 || mode == 7) { result.state = State::Timeout; result.nativeError = 12002; return result; }
    if (mode == 2) { result.state = State::NoSuggestion; result.httpStatus = 500; return result; }
    if (mode == 3) { result.state = State::NoSuggestion; result.httpStatus = 200; result.body = "invalid"; return result; }
    FixedAutomaticTransport fixed(mode != 6);
    return fixed.Post(config, endpoint, body, cancelled);
  }
 private:
  Fixture& fixture_;
};
std::wstring Self() { WCHAR path[32768]{}; GetModuleFileNameW(nullptr, path, _countof(path)); return path; }
bool Save(const std::wstring& path, const Preferences& preferences, const UserLexicon& lexicon = {}) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const auto data = EncodePreferences(preferences);
  const bool ok = RegSetValueExW(key, L"SettingsV2", 0, REG_BINARY,
      reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size())) == ERROR_SUCCESS && WriteUserLexiconValue(key, lexicon);
  RegCloseKey(key); return ok;
}
BrokerRequest CheckRequestFor(const BrokerReply& settings) {
  BrokerRequest request; request.operation = BrokerOperation::Check;
  request.requestId = 123; request.revision = settings.settings.revision;
  const std::wstring text = L"这里有错子。";
  request.input.units = static_cast<DWORD>(text.size()); memcpy(request.input.text, text.data(), text.size() * sizeof(WCHAR));
  return request;
}
bool RemoteClient(const std::wstring& server, const std::wstring& output, const std::wstring& root, bool enabled) {
  BrokerCall call; BrokerReply settings, response; BrokerRequest request;
  PreferencesRead direct; ReadPreferences(root.c_str(), &direct);
  using GetPackage = LONG(WINAPI*)(UINT32*, PWSTR);
  const auto getPackage = reinterpret_cast<GetPackage>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFullName"));
  UINT32 units = 0; const LONG package = getPackage ? getPackage(&units, nullptr) : APPMODEL_ERROR_NO_PACKAGE;
  bool configured = call.Exchange(server, request, settings, 1000) && settings.status == BrokerStatus::Ok && settings.settings.enabled == static_cast<DWORD>(enabled);
  bool checked = false, metadata = false;
  if (configured) {
    const auto check = CheckRequestFor(settings);
    checked = call.Exchange(server, check, response, 2000) &&
        (enabled ? response.status == BrokerStatus::Ok && response.modelState == static_cast<DWORD>(State::Suggestions) && response.result.count == 1 :
                   response.status == BrokerStatus::Disabled && response.result.count == 0) &&
        response.requestId == check.requestId && response.settings.revision == settings.settings.revision;
    BrokerRequest notice; notice.operation = BrokerOperation::Metadata;
    FrontendDiagnostic diagnostic; diagnostic.stage = 777; diagnostic.units = 7;
    notice.metadata = BrokerMetadata::Gate; notice.metadataBytes = sizeof(diagnostic);
    memcpy(notice.data, &diagnostic, sizeof(diagnostic)); metadata = call.SendMetadata(server, notice);
  }
  std::ofstream file(std::filesystem::path(output), std::ios::binary);
  file << "{\"packageStatus\":" << package << ",\"directSettingsStage\":" << static_cast<unsigned>(direct.stage)
       << ",\"directSettingsError\":" << direct.error << ",\"configured\":" << configured
       << ",\"checked\":" << checked << ",\"metadataSent\":" << metadata
       << ",\"revision\":" << settings.settings.revision << ",\"count\":" << response.result.count << "}";
  return configured && checked && metadata && file.good();
}
#include "ListenerChecks.inc"
#include "SupervisorChecks.inc"
#include "ServiceControlChecks.inc"
int wmain(int argc, wchar_t** argv) {
  if (argc == 2 && !wcscmp(argv[1], L"/correction-broker")) return RunSupervisorFixture();
  if (argc == 2 && !wcscmp(argv[1], L"--supervisor")) return RunSupervisorChecks();
  if (argc == 2 && !wcscmp(argv[1], L"--service-control")) return RunServiceControlChecks();
  if (argc == 3 && !wcscmp(argv[1], L"--supervisor-process")) return RunSupervisedBrokerProcess(argv[2]);
  if (argc == 2 && !wcscmp(argv[1], L"--listener-lifecycle")) return RunListenerChecks();
  if (argc == 3 && (!wcscmp(argv[1], L"--process") || !wcscmp(argv[1], L"--process-on"))) {
    const bool enabled = !wcscmp(argv[1], L"--process-on");
    BrokerProcess child;
    bool valid = child.Start(argv[2]);
    HANDLE observed = nullptr;
    if (valid) DuplicateHandle(GetCurrentProcess(), child.Process(), GetCurrentProcess(), &observed, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
    BrokerCall call; BrokerReply reply; BrokerRequest request;
    const auto deadline = GetTickCount64() + 5000;
    bool ready = false;
    while (valid && !ready && GetTickCount64() < deadline) {
      ready = call.Exchange(argv[2], request, reply, 100) &&
          (enabled ? reply.status == BrokerStatus::Ok && reply.settings.enabled : reply.status == BrokerStatus::Unavailable);
      if (!ready) Sleep(10);
    }
    valid = valid && ready && observed;
    if (valid && enabled) {
      const auto input = CheckRequestFor(reply);
      valid = call.Exchange(argv[2], input, reply, 2000) && reply.status == BrokerStatus::Ok && reply.result.count == 1;
    }
    child.Stop();
    DWORD code = 1;
    valid = valid && WaitForSingleObject(observed, 0) == WAIT_OBJECT_0 && GetExitCodeProcess(observed, &code) && code == 0;
    if (observed) CloseHandle(observed);
    valid = !call.Exchange(argv[2], request, reply) && valid;
    std::cout << "Production broker child startup, " << (enabled ? "enabled fixed inference" : "missing-settings fail-closed")
        << ", graceful EOF exit, endpoint removed = " << valid << '\n';
    return valid ? 0 : 1;
  }
  if (argc == 6 && !wcscmp(argv[1], L"--client")) return RemoteClient(argv[2], argv[3], argv[4], !wcscmp(argv[5], L"on")) ? 0 : 1;
  const bool serve = argc == 3 && !wcscmp(argv[1], L"--serve");
  if (argc != 1 && !serve) return 2;
  const auto root = L"Software\\WeaseLC-Broker-Check-" + std::to_wstring(GetCurrentProcessId());
  HKEY existing = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, KEY_READ, &existing) == ERROR_SUCCESS) { RegCloseKey(existing); return 3; }
  struct Cleanup { std::wstring path; ~Cleanup() { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); } } cleanup{root};
  Preferences preferences; preferences.mode = Mode::Local; preferences.automatic.enabled = true;
  preferences.local.model = L"compiled-fixture"; preferences.local.timeoutSeconds = 30;
  if (!Save(root, preferences)) return 4;
  Fixture fixture;
  CorrectionBroker broker(root, [&]() { return std::make_unique<Transport>(fixture); });
  if (!broker.Start()) return 5;
  if (serve) {
    const std::filesystem::path directory(argv[2]);
    { std::ofstream ready(directory / "ready.txt"); ready << GetCurrentProcessId(); }
    { std::wofstream key(directory / "root.txt"); key << root; }
    const auto deadline = GetTickCount64() + 120000;
    while (!std::filesystem::exists(directory / "stop") && GetTickCount64() < deadline) {
      if (preferences.automatic.enabled && std::filesystem::exists(directory / "disable")) {
        preferences.automatic.enabled = false; Save(root, preferences);
        std::ofstream disabled(directory / "disabled.txt"); disabled << "off";
      }
      Sleep(20);
    }
    FrontendDiagnostic diagnostic; DWORD bytes = sizeof(diagnostic);
    const auto path = root + L"\\Diagnostics";
    const bool received = RegGetValueW(HKEY_CURRENT_USER, path.c_str(), L"LastGate", RRF_RT_REG_BINARY,
        nullptr, &diagnostic, &bytes) == ERROR_SUCCESS && diagnostic.stage == 777;
    broker.Stop(); std::ofstream result(directory / "host-result.json");
    result << "{\"metadataReceived\":" << received << ",\"modelCalls\":" << fixture.calls.load() << "}";
    return received ? 0 : 1;
  }
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) { ++checks; if (!ok) ++failures; std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; };
  BrokerCall call; BrokerReply settings, reply; BrokerRequest request;
  auto snapshot = [&]() { return call.Exchange(Self(), request, settings, 1000) && settings.status == BrokerStatus::Ok; };
  auto run = [&]() { return call.Exchange(Self(), CheckRequestFor(settings), reply, 3000); };
  check(snapshot() && settings.settings.enabled && settings.settings.timeoutSeconds == 30, "shared public settings snapshot");
  check(!settings.settings.wordLocalCompatibility,"Word compatibility defaults off in public snapshot");
  preferences.automatic.wordLocalCompatibility=true;Save(root,preferences);
  check(snapshot() && settings.settings.wordLocalCompatibility==0,"retired Word opt-in cannot cross settings broker");
  preferences.automatic.wordLocalCompatibility=false;Save(root,preferences);
  check(snapshot() && !settings.settings.wordLocalCompatibility,"Word opt-out crosses settings broker");
  BrokerReply health;
  auto healthSnapshot = [&]() {
    BrokerRequest status; status.operation = BrokerOperation::Health;
    const auto calls = fixture.calls.load();
    return call.Exchange(Self(), status, health, 1000) && health.status == BrokerStatus::Ok &&
        fixture.calls.load() == calls && !health.result.count;
  };
  check(healthSnapshot() && health.modelState == 0xffffffff, "health query neither invokes model nor invents a successful check");
  {
    BrokerRequest notice;notice.operation=BrokerOperation::Metadata;
    notice.metadata=BrokerMetadata::WordMetadata;notice.metadataBytes=40;
    const DWORD sample[]{1,4,0,static_cast<DWORD>(DISP_E_EXCEPTION),static_cast<DWORD>(E_FAIL),0,0,0,123,456};
    memcpy(notice.data,sample,sizeof(sample));
    const auto calls=fixture.calls.load();
    const bool sent=call.SendMetadata(Self(),notice);
    DWORD stored[10]{};DWORD bytes=sizeof(stored);
    const auto key=root+L"\\Diagnostics";
    const auto deadline=GetTickCount64()+1000;
    while(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"LastWordMetadata",RRF_RT_REG_BINARY,nullptr,stored,&bytes)!=ERROR_SUCCESS &&
        GetTickCount64()<deadline){bytes=sizeof(stored);Sleep(5);}
    check(sent && bytes==sizeof(sample) && !memcmp(sample,stored,sizeof(sample)) && fixture.calls==calls,
        "numeric Word getter diagnostics cross the dedicated metadata channel without a model call");
  }
  {
    BrokerRequest notice;notice.operation=BrokerOperation::Metadata;
    notice.metadata=BrokerMetadata::ScopeTrace;notice.metadataBytes=40;
    const DWORD trace[10]{1,1,3,0,0,static_cast<DWORD>(E_FAIL),0,0,0,0};
    memcpy(notice.data,trace,sizeof(trace));
    DWORD saved[10]{};DWORD bytes=sizeof(saved);
    const auto key=root+L"\\Diagnostics";
    const bool sent=call.SendMetadata(Self(),notice);
    const auto deadline=GetTickCount64()+1000;
    while(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"LastScopeAfterEnd",RRF_RT_REG_BINARY,nullptr,saved,&bytes)!=ERROR_SUCCESS &&
        GetTickCount64()<deadline){bytes=sizeof(saved);Sleep(5);}
    check(sent &&
        RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"LastScopeAfterEnd",RRF_RT_REG_BINARY,nullptr,saved,&bytes)==ERROR_SUCCESS &&
        bytes==sizeof(trace) && !memcmp(trace,saved,sizeof(trace)),"scope trace metadata relays without content");
    const DWORD invalid=3;memcpy(notice.data+sizeof(DWORD),&invalid,sizeof(invalid));
    check(call.SendMetadata(Self(),notice) &&
        RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"LastScopeAfterEnd",RRF_RT_REG_BINARY,nullptr,saved,&bytes)==ERROR_SUCCESS &&
        !memcmp(trace,saved,sizeof(trace)),"scope trace rejects unknown phase without replacing evidence");
  }
  for (const auto kind : {BrokerMetadata::KeyDownDispatch,BrokerMetadata::KeyUpDispatch,BrokerMetadata::IntegratedDispatch}) {
    BrokerRequest notice;notice.operation=BrokerOperation::Metadata;notice.metadata=kind;
    KeyDispatchDiagnostic sample;sample.callbacks=17;sample.dispatches=9;sample.reuses=8;
    sample.unhandled=1;sample.actualMisses=1;sample.reasons=DifferentFlags;
    notice.metadataBytes=sizeof(sample);memcpy(notice.data,&sample,sizeof(sample));
    const wchar_t* names[]{L"LastKeyDownDispatch",L"LastKeyUpDispatch",L"LastIntegratedDispatch"};
    const auto name=names[static_cast<unsigned>(kind)-static_cast<unsigned>(BrokerMetadata::KeyDownDispatch)];
    const auto path=root+L"\\Diagnostics";KeyDispatchDiagnostic saved;DWORD bytes=sizeof(saved);
    const bool sent=call.SendMetadata(Self(),notice);const auto deadline=GetTickCount64()+1000;
    while(RegGetValueW(HKEY_CURRENT_USER,path.c_str(),name,RRF_RT_REG_BINARY,nullptr,&saved,&bytes)!=ERROR_SUCCESS &&
        GetTickCount64()<deadline){bytes=sizeof(saved);Sleep(5);}
    check(sent && bytes==sizeof(sample) && !memcmp(&sample,&saved,sizeof(sample)),
        "bounded dispatch counters relay on the correct closed metadata channel");
  }
  check(run() && reply.status == BrokerStatus::Ok && reply.result.count == 1, "fixed response through production parser and broker");
  check(healthSnapshot() && health.modelState == static_cast<DWORD>(State::Suggestions),
      "health retains result state without suggestion text");
  check(!call.Exchange(L"C:\\invalid-server.exe", request, reply), "reject mismatched backend executable");
  auto stale = CheckRequestFor(settings); stale.revision--;
  const auto before = fixture.calls.load();
  check(call.Exchange(Self(), stale, reply, 1000) && reply.status == BrokerStatus::SettingsChanged && fixture.calls == before, "reject stale revision before model invocation");
  auto invalid = CheckRequestFor(settings); invalid.input.units = 1025;
  check(call.Exchange(Self(), invalid, reply, 1000) && reply.status == BrokerStatus::InvalidRequest, "reject oversized input");
  invalid = CheckRequestFor(settings); invalid.version = 999;
  check(!call.Exchange(Self(), invalid, reply, 1000), "reject unsupported protocol version");
  invalid = CheckRequestFor(settings); invalid.version = 2;
  check(!call.Exchange(Self(), invalid, reply, 1000), "older broker protocol cannot silently drop Word opt-in policy");
  for (unsigned mode : {1u, 2u, 3u}) {
    fixture.mode = mode;
    const State expected = mode == 1 ? State::Timeout : mode == 2 ? State::HttpError : State::InvalidResponse;
    check(run() && reply.status == BrokerStatus::ModelFailure && reply.modelState == static_cast<DWORD>(expected) && !reply.result.count,
        "model failure is distinct from no suggestion");
    check(healthSnapshot() && health.modelState == static_cast<DWORD>(expected),
        "management health distinguishes timeout, HTTP and invalid-response failures");
  }
  fixture.mode = 6;
  check(run() && reply.status == BrokerStatus::Ok && reply.modelState == static_cast<DWORD>(State::NoSuggestion), "valid no-change remains successful");
  check(healthSnapshot() && health.modelState == static_cast<DWORD>(State::NoSuggestion),
      "new successful check replaces previous model failure");
  {
    fixture.mode = 7; BrokerCall old;
    const auto calls = fixture.calls.load();
    const bool submitted = old.Begin(Self(), CheckRequestFor(settings));
    const auto deadline = GetTickCount64() + 1000;
    while (fixture.calls == calls && GetTickCount64() < deadline) { old.Poll(reply); Sleep(1); }
    fixture.mode = 6;
    const bool newer = run() && reply.modelState == static_cast<DWORD>(State::NoSuggestion);
    int completed = 0;
    while (!(completed = old.Poll(reply)) && GetTickCount64() < deadline) Sleep(1);
    check(submitted && newer && completed == 1 && reply.modelState == static_cast<DWORD>(State::Timeout) &&
        healthSnapshot() && health.modelState == static_cast<DWORD>(State::NoSuggestion),
        "late failure from an older request cannot overwrite newer successful model health");
  }
  fixture.mode = 0;
  UserLexicon lexicon; KeepTerm(lexicon, L"错子"); Save(root, preferences, lexicon);
  check(snapshot() && run() && reply.status == BrokerStatus::Ok && !reply.result.count &&
      reply.modelState == static_cast<DWORD>(State::Filtered), "kept-term filter runs in authoritative backend");
  preferences.automatic.enabled = false; Save(root, preferences);
  const bool invalidatedHealth = healthSnapshot() && !health.settings.enabled && health.modelState == 0xffffffff;
  if (!invalidatedHealth) std::cout << "Health revisions before=" << settings.settings.revision << " after=" << health.settings.revision
      << " enabled=" << health.settings.enabled << " modelState=" << health.modelState << '\n';
  check(invalidatedHealth,
      "saved configuration changes invalidate previous model status");
  bool rapidVersions = true;
  for (unsigned i = 0; i < 24; ++i) {
    const auto previous = health.settings.revision;
    preferences.automatic.enabled = !preferences.automatic.enabled;
    rapidVersions = Save(root, preferences) && healthSnapshot() && health.settings.revision != previous &&
        health.settings.enabled == static_cast<DWORD>(preferences.automatic.enabled) && rapidVersions;
  }
  check(rapidVersions, "rapid configuration toggles always change opaque revision without timestamp sleeps");
  check(snapshot() && !settings.settings.enabled && run() && reply.status == BrokerStatus::Disabled, "disable switch is authoritative");
  preferences.automatic.enabled = true; Save(root, preferences); snapshot();
  fixture.mode = 4;
  const auto previousCalls = fixture.calls.load();
  check(call.Begin(Self(), CheckRequestFor(settings)), "asynchronous model submission");
  // Drive the overlapped write/read once, without waiting for inference.
  auto started = GetTickCount64();
  while (fixture.calls == previousCalls && GetTickCount64() - started < 1000) { call.Poll(reply); Sleep(1); }
  call.Reset(); started = GetTickCount64();
  while (!fixture.cancelled.load() && GetTickCount64() - started < 1000) Sleep(5);
  check(fixture.cancelled.load() > 0, "closing a request cancels backend model transport");
  fixture.mode = 4; const auto cancelled = fixture.cancelled.load();
  call.Begin(Self(), CheckRequestFor(settings)); call.Poll(reply); Sleep(80);
  preferences.automatic.enabled = false; Save(root, preferences);
  started = GetTickCount64(); int state = 0;
  while (!(state = call.Poll(reply)) && GetTickCount64() - started < 1000) Sleep(5);
  check(state == 1 && reply.status == BrokerStatus::SettingsChanged && fixture.cancelled > cancelled, "settings changes cancel in-flight inference");
  preferences.automatic.enabled = true; Save(root, preferences); snapshot(); fixture.mode = 4;
  std::vector<std::unique_ptr<BrokerCall>> occupied;
  bool submitted = true;
  for (unsigned i = 0; i < 4; ++i) {
    const auto count = fixture.calls.load();
    auto pending = std::make_unique<BrokerCall>(); submitted = pending->Begin(Self(), CheckRequestFor(settings)) && submitted;
    const auto deadline = GetTickCount64() + 1000;
    while (fixture.calls == count && GetTickCount64() < deadline) { pending->Poll(reply); Sleep(1); }
    submitted = submitted && fixture.calls > count; occupied.push_back(std::move(pending));
  }
  check(submitted && run() && reply.status == BrokerStatus::Busy, "bounded model concurrency returns explicit busy status");
  const auto configurationStart = GetTickCount64();
  check(snapshot() && GetTickCount64() - configurationStart < 100, "settings remain responsive while all model slots are occupied");
  const auto cancelledBeforeClose = fixture.cancelled.load(); occupied.clear();
  started = GetTickCount64();
  while (fixture.cancelled < cancelledBeforeClose + 4 && GetTickCount64() - started < 1000) Sleep(5);
  check(fixture.cancelled >= cancelledBeforeClose + 4, "all disconnected model clients are cancelled");
  Sleep(20); fixture.mode = 5;
  const auto slowStart = GetTickCount64();
  check(call.Begin(Self(), CheckRequestFor(settings)), "slow model starts without blocking host");
  state = 0;
  while (!(state = call.Poll(reply)) && GetTickCount64() - slowStart < 12500) Sleep(10);
  check(state == 1 && reply.status == BrokerStatus::Ok && reply.result.count == 1 && GetTickCount64() - slowStart >= 10000,
      "response after ten seconds survives a thirty-second request budget");
  fixture.mode = 4; call.Begin(Self(), CheckRequestFor(settings)); call.Poll(reply); Sleep(50);
  const auto stopStart = GetTickCount64(); broker.Stop();
  check(GetTickCount64() - stopStart < 1000, "backend shutdown cancels active model and listener");
  call.Reset();
  check(!call.Exchange(Self(), request, reply), "absent backend fails closed");
  weasel::PipeAccessPolicy access;
  HANDLE stalled = CreateNamedPipeW(CorrectionPipeName().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, access.get_attr());
  check(stalled != INVALID_HANDLE_VALUE, "stalled authenticated endpoint fixture created");
  if (stalled != INVALID_HANDLE_VALUE) {
    std::thread endpoint([&]() { ConnectNamedPipe(stalled, nullptr); Sleep(120); CloseHandle(stalled); });
    const auto timeoutStart = GetTickCount64();
    check(!call.Exchange(Self(), request, reply, 20) && GetTickCount64() - timeoutStart < 100,
        "settings timeout cancels overlapped IO without waiting for stalled backend");
    endpoint.join();
  }
  std::cout << "Broker checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
