#include <windows.h>
#define BOOST_JSON_NO_LIB
#define BOOST_CONTAINER_NO_LIB
#include <boost/json.hpp>
#include <CorrectionModel.h>
#include <ResponseSlot.h>
#include <CorrectionSession.h>
#include <CorrectionQueue.h>
#include <CorrectionReview.h>
#include <CorrectionLexicon.h>
#include "../../WeaselDeployer/CorrectionPreferences.h"
#include "../LocalDeployment/FixedAutomaticReply.h"
#include <iostream>
#include <limits>
#include <thread>
#include <chrono>

using namespace correction;
namespace json = boost::json;
unsigned checks = 0, failures = 0;
void Check(bool valid, const char* name) {
  ++checks; if (!valid) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
std::string Envelope(const json::value& answer, const char* finish = "stop") {
  json::array choices;
  choices.emplace_back(json::object{{"finish_reason", finish}, {"message",
      json::object{{"role", "assistant"}, {"content", json::serialize(answer)}}}});
  return json::serialize(json::object{{"choices", choices}});
}
json::object Suggest(const char* original = "错子", double score = .95) {
  return {{"original", original}, {"replacement", "错字"}, {"reason", "同音字"}, {"score", score}};
}
json::object Answer(json::array suggestions) {
  return {{"version", 1}, {"decision", "suggestions"}, {"suggestions", suggestions}};
}
struct MockTransport : IModelTransport {
  unsigned calls = 0;
  TransportResult response{State::NoSuggestion, 0, 200, {}};
  bool cancelOnPost = false;
  std::string sent;
  TransportResult Post(const ModelConfig&, const Endpoint&, const std::string& body,
      const std::atomic_bool& cancelled) override {
    ++calls; sent = body;
    if (cancelOnPost) const_cast<std::atomic_bool&>(cancelled).store(true);
    return response;
  }
};
#include "QualityChecks.inc"
int wmain(int argc, wchar_t** argv) {
  if (argc > 1 && !wcscmp(argv[1], L"--quality")) {
    if (argc < 7 || argc > 9) return 2;
    try {
      unsigned seed = 104729;
      if (argc == 9) {
        const std::wstring value = argv[8];
        if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos) return 2;
        const auto parsed = std::stoull(value);
        if (parsed > 0xffffffffULL) return 2;
        seed = static_cast<unsigned>(parsed);
      }
      return RunQualityChecks(argv[2], argv[3], argv[4], argv[5], argv[6], argc >= 8 ? argv[7] : L"greedy",seed);
    }
    catch (...) { return 2; }
  }
  if (argc == 2 && wcscmp(argv[1], L"--storage-checks") == 0) {
    const auto path = L"Software\\WeaseLC-Lexicon-Test-" + std::to_wstring(GetCurrentProcessId());
    HKEY key = nullptr; DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition) != ERROR_SUCCESS) return 1;
    if (disposition != REG_CREATED_NEW_KEY) { RegCloseKey(key); return 2; }
    UserLexicon value, loaded;
    Check(ReadUserLexiconValue(key, loaded) && loaded.terms.empty(), "missing store starts empty");
    KeepTerm(value, L"合成保留词");
    Check(WriteUserLexiconValue(key, value), "explicit term persists");
    RegCloseKey(key); key = nullptr;
    Check(RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) == ERROR_SUCCESS &&
        ReadUserLexiconValue(key, loaded) && loaded.terms == value.terms, "term restored after reopening store");
    auto invalid = value; invalid.terms.push_back(L"\n");
    Check(!WriteUserLexiconValue(key, invalid) && ReadUserLexiconValue(key, loaded) && loaded.terms == value.terms,
          "invalid save preserves old value");
    const BYTE malformed[] = {'b','a','d'};
    RegSetValueExW(key, L"KeptTermsV1", 0, REG_BINARY, malformed, sizeof(malformed));
    Check(!ReadUserLexiconValue(key, loaded) && loaded.terms == value.terms, "corrupt stored data fails without clearing memory");
    Check(WriteUserLexiconValue(key, UserLexicon{}) && ReadUserLexiconValue(key, loaded) && loaded.terms.empty(), "explicit clear persisted");
    PreferencesRead read;
    Check(ReadPreferences(path.c_str(), &read).mode == Mode::Off && read.stage == PreferencesStage::Missing,
        "missing settings are diagnosed without enabling checks");
    Preferences saved; saved.mode = Mode::Local; saved.automatic.enabled = true;
    const auto settings = EncodePreferences(saved);
    RegSetValueExA(key, "SettingsV2", 0, REG_BINARY, reinterpret_cast<const BYTE*>(settings.data()), static_cast<DWORD>(settings.size()));
    const auto restored = ReadPreferences(path.c_str(), &read);
    Check(read.stage == PreferencesStage::Ready && read.error == ERROR_SUCCESS &&
        read.bytes == settings.size() && restored.mode == Mode::Local && restored.automatic.enabled,
        "settings read diagnostics distinguish a valid enabled configuration");
    RegSetValueExW(key, L"SettingsV2", 0, REG_BINARY, malformed, sizeof(malformed));
    Check(ReadPreferences(path.c_str(), &read).mode == Mode::Off && read.stage == PreferencesStage::DecodeFailed,
        "malformed settings are diagnosed separately from missing settings");
    const std::string excessive(16385, 'x');
    RegSetValueExW(key, L"SettingsV2", 0, REG_BINARY, reinterpret_cast<const BYTE*>(excessive.data()), static_cast<DWORD>(excessive.size()));
    Check(ReadPreferences(path.c_str(), &read).mode == Mode::Off && read.stage == PreferencesStage::InvalidSize,
        "oversized settings fail closed before decoding");
    const DWORD wrongType = 1;
    RegSetValueExW(key, L"SettingsV2", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&wrongType), sizeof(wrongType));
    Check(ReadPreferences(path.c_str(), &read).mode == Mode::Off && read.stage == PreferencesStage::QueryFailed && read.error != 0,
        "wrong settings value type retains the registry failure");
    RegCloseKey(key);
    Check(RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()) == ERROR_SUCCESS, "temporary storage removed");
    std::cout << "Scoped storage checks = " << checks << ", failures = " << failures << '\n';
    return failures ? 1 : 0;
  }
  if (argc == 3 && wcscmp(argv[1], L"--transport-checks") == 0) {
    ModelConfig config; config.mode = Mode::Local; config.model = L"fixture";
    CheckRequest request; request.safeText = true; request.text = L"合成文本。";
    auto client = CreateModelClient(Provider::ChatCompletions);
    const std::wstring base = argv[2];
    std::atomic_bool cancelled{false};
    auto run = [&](const wchar_t* path) {
      config.endpoint = base + path + L"/chat/completions";
      return client->Check(config, request, cancelled);
    };
    Check(run(L"/ok").state == State::NoSuggestion, "real HTTP response");
    Check(run(L"/chunked").state == State::NoSuggestion, "chunked HTTP response");
    Check(run(L"/redirect").state == State::HttpError, "redirect not followed");
    Check(run(L"/oversized").state == State::InvalidResponse, "real HTTP body bound");
    Check(run(L"/stream-overflow").state == State::InvalidResponse, "stream without length remains bounded");
    const auto truncated = run(L"/truncated").state;
    Check(truncated == State::TransportError || truncated == State::InvalidResponse, "truncated body rejected");
    auto start = GetTickCount64();
    std::thread cancel([&]() { Sleep(100); cancelled.store(true); });
    const auto result = run(L"/slow"); cancel.join();
    const auto elapsed = GetTickCount64() - start;
    Check(result.state == State::Cancelled && elapsed < 1500, "active HTTP cancellation bounded");
    std::cout << "Cancellation ms = " << elapsed << '\n';
    cancelled.store(false); config.timeoutSeconds = 2; start = GetTickCount64();
    Check(run(L"/drip").state == State::Timeout && GetTickCount64() - start < 3000,
          "total HTTP deadline stops trickling response");
    bool repeated = true;
    for (unsigned i = 0; i < 8; ++i) {
      std::thread stop([&]() { Sleep(30); cancelled.store(true); });
      const auto response = run(L"/slow"); stop.join();
      repeated = response.state == State::Cancelled && repeated;
      cancelled.store(false);
    }
    Check(repeated && run(L"/ok").state == State::NoSuggestion,
          "repeated active cancel releases callbacks and permits subsequent request");
    std::cout << "HTTP checks = " << checks << ", failures = " << failures << '\n';
    return failures ? 1 : 0;
  }
  if (argc != 1) return 2;
  for (const auto& text : {L"这里有错子", L"这里有错子。", L"这里有错子.", L"这里有错子，，，", L"这里有错子。。。",
      L"这里有错子..", L"这里有错子？！", L"这里有错子……", L"这里有错子，，。。！？；："})
    Check(FixedAutomaticVerdict(text) == FixedVerdict::Suggest, "fixed corpus preserves bounded repeated and mixed punctuation");
  for (const auto& text : {L"这", L"这里", L"这里有", L"这里有错"})
    Check(FixedAutomaticVerdict(text) == FixedVerdict::NoChange, "fixed prefix preserves word-by-word context");
  for (const auto& text : {L"", L" 这里有错子。", L"这里有错字。", L"其他文字", L"这里有错子，，，，，，，，，",
      L"这里有错子abc", L"这里有错子 后文", L"这里有错子\n", L"这里有错子\t", L"这里有错子🙂"})
    Check(FixedAutomaticVerdict(text) == FixedVerdict::Reject, "fixed corpus remains restricted to compiled samples");
  RequestQueue queue(250, 1000);
  CheckRequest queued; queued.safeText = true; queued.text = L"合成";
  queued.session = 1; queued.version = 1;
  queue.Submit(queued, 100);
  Check(!queue.TakeReady(349), "debounce defers inference");
  queued.version = 2; queued.text = L"合成文本"; queue.Submit(queued, 200);
  auto work = queue.TakeReady(450);
  Check(work && work->request.version == 2, "burst retains latest snapshot only");
  queued.version = 3; queue.Submit(queued, 500);
  Check(work && work->cancelled->load() && !queue.TakeReady(800), "supersession cancels without overlapping workers");
  CheckResult completed; completed.session = 1; completed.version = 2;
  Check(!queue.Finish(*work, completed, 800), "superseded response discarded");
  work = queue.TakeReady(800); completed.version = 3;
  Check(work && queue.Finish(*work, completed, 900) && !queue.Finish(*work, completed, 900), "current completion consumed once");
  queue.Submit(queued, 1000); work = queue.TakeReady(1250);
  Check(work && !queue.Finish(*work, completed, 2000), "expired result discarded");
  queue.Submit(queued, 2200); queue.Cancel();
  Check(!queue.TakeReady(2500), "focus cancel clears pending snapshot");
  queued.safeText = false; queue.Submit(queued, 2500);
  Check(!queue.TakeReady(2800), "protected snapshot never queued");
  queued.safeText = true; queued.text = L" \t\r\n"; queue.Submit(queued, 2800);
  Check(!queue.TakeReady(3100), "whitespace needs no inference");
  queued.text = L"合成"; queue.Submit(queued, 3100);
  Check(!queue.TakeReady(4200), "pending snapshot expires before launch");
  queue.Submit(queued, 4300); work = queue.TakeReady(4550);
  completed.version = 99;
  Check(work && !queue.Finish(*work, completed, 4600), "mismatched response rejected and worker released");
  queued.version = 4; queue.Submit(queued, 4700);
  auto next = queue.TakeReady(4950);
  Check(next && work && !queue.Finish(*work, completed, 4950), "old worker cannot release new worker");
  completed.version = 4;
  Check(next && queue.Finish(*next, completed, 4999), "queue recovers after invalid response");
  weasel::ResponseSlot slot;
  Check(slot.TakeUnits() == 0, "empty response not replayed");
  Check(slot.Complete(20, 64) && slot.TakeUnits() == 20 / sizeof(wchar_t) &&
      slot.TakeUnits() == 0, "complete response consumed once");
  slot.Complete(20, 64); slot.Reset();
  Check(slot.TakeUnits() == 0, "new scalar/failed transaction invalidates body");
  Check(!slot.Complete(65, 64) && !slot.Complete(1, 64) && slot.TakeUnits() == 0,
      "oversized/unaligned response rejected");
  SessionGate gate;
  CommitCapture event; event.kind = CaptureKind::Commit; event.generation = 7;
  event.sequence = 1; event.units = 2; event.text[0] = L'你'; event.text[1] = L'好';
  Check(!gate.Observe(event), "capture mode off");
  gate.SetEnabled(true); event.sequence = 2;
  auto captured = gate.Observe(event);
  Check(captured && captured->text == L"你好" && captured->source == Source::CommittedText &&
      captured->session == 7 && captured->version == 2, "capture request tags");
  Check(!gate.Observe(event), "duplicate capture ignored");
  CheckResult previous; previous.session = 7; previous.version = 2;
  Check(gate.IsCurrent(previous), "valid result current");
  event.sequence = 3; event.kind = CaptureKind::Cancel;
  Check(!gate.Observe(event) && !gate.IsCurrent(previous), "cancel invalidates result");
  event.sequence = 4; event.kind = CaptureKind::Commit; event.generation = 8;
  captured = gate.Observe(event);
  Check(captured && !gate.IsCurrent(previous), "document generation invalidates result");
  gate.SetEnabled(false);
  Check(!gate.IsCurrent(previous), "closing/disabling invalidates result");
  gate.SetEnabled(true); event.sequence = 5; event.units = 1025;
  Check(!gate.Observe(event), "capture length bound");
  event.sequence = 6; event.units = 2; event.text[1] = 0;
  Check(!gate.Observe(event), "capture embedded null rejected");
  event.sequence = 7; event.abi = 2;
  Check(!gate.Observe(event), "unknown capture ABI rejected");
  ModelConfig config; config.mode = Mode::Local; config.model = L"test-model";
  Endpoint endpoint;
  Check(ValidateConfig(config, &endpoint) && endpoint.host == L"127.0.0.1", "local default");
  for (const auto* url : {L"http://localhost:1234/v1/chat/completions", L"http://[::1]:1234/v1/chat/completions"}) {
    auto c = config; c.endpoint = url; Check(ValidateConfig(c), "loopback accepted");
  }
  for (const auto* url : {L"http://example.com/v1/chat/completions", L"http://127.0.0.1.evil/v1/chat/completions",
      L"http://user:pass@127.0.0.1/v1/chat/completions", L"http://127.0.0.1/v1/chat/completions?q=1",
      L"http://127.0.0.1/v1/chat/completions#fragment", L"file:///chat/completions", L"http://127.0.0.1/api/chat"}) {
    auto c = config; c.endpoint = url; Check(!ValidateConfig(c), "unsafe local URL");
  }
  auto online = config; online.mode = Mode::Online; online.endpoint = L"https://example.com/v1/chat/completions";
  Check(!ValidateConfig(online), "online consent required");
  online.onlineConsent = true; Check(ValidateConfig(online), "consented HTTPS");
  online.endpoint = L"http://example.com/v1/chat/completions"; Check(!ValidateConfig(online), "online HTTP rejected");
  auto bad = config; bad.bearerToken = L"abc\r\nHeader: x"; Check(!ValidateConfig(bad), "header injection rejected");
  bad = config; bad.timeoutSeconds = 0; Check(!ValidateConfig(bad), "unbounded timeout rejected");
  bad = config; bad.minimumScore = std::numeric_limits<double>::quiet_NaN(); Check(!ValidateConfig(bad), "NaN threshold");

  CheckRequest request; request.session = 77; request.version = 5;
  request.text = L"这里有错子。"; request.safeText = true;
  std::atomic_bool cancelled{false};
  auto mock = std::make_unique<MockTransport>(); auto* observed = mock.get();
  observed->response.body = Envelope(Answer({Suggest()}));
  auto client = CreateModelClient(Provider::ChatCompletions, std::move(mock));
  auto result = client->Check(config, request, cancelled);
  Check(result.state == State::Suggestions && result.suggestions.size() == 1 &&
      result.suggestions[0].start == 3 && result.session == 77 && result.version == 5, "valid correction and tags");
  Check(result.httpStatus == 200 && result.nativeError == 0, "successful transport metadata preserved");
  auto sent = json::parse(observed->sent).as_object();
  Check(!sent.at("stream").as_bool() && sent.at("response_format").as_object().at("type") == "json_object", "nonstream JSON request");
  Check(sent.at("messages").as_array().size() == 2, "instruction and data separate");
  const unsigned calls = observed->calls;
  auto off = config; off.mode = Mode::Off;
  Check(client->Check(off, request, cancelled).state == State::Off && observed->calls == calls, "off does not send");
  auto protectedRequest = request; protectedRequest.safeText = false;
  Check(client->Check(config, protectedRequest, cancelled).state == State::ProtectedInput && observed->calls == calls, "protected does not send");
  cancelled.store(true);
  Check(client->Check(config, request, cancelled).state == State::Cancelled && observed->calls == calls, "cancelled before send");
  cancelled.store(false);
  auto tooLong = request; tooLong.text.assign(kMaxTargetUnits + 1, L'字');
  Check(client->Check(config, tooLong, cancelled).state == State::InvalidInput && observed->calls == calls, "input bound");
  auto invalidUnicode = request; invalidUnicode.text.assign(1, 0xd800);
  Check(client->Check(config, invalidUnicode, cancelled).state == State::InvalidInput, "unpaired surrogate");
  auto reserved = CreateModelClient(Provider::CodexCliReserved);
  Check(reserved->Check(config, request, cancelled).state == State::InvalidConfig, "reserved CLI fails closed");
  bad = config; bad.provider = Provider::CodexCliReserved;
  Check(!ValidateConfig(bad), "reserved config rejected");
  auto nochange = json::object{{"version", 1}, {"decision", "no_change"}, {"suggestions", json::array{}}};
  Check(ParseCorrectionAnswer(json::serialize(Answer({Suggest()})), config, request).state == State::Suggestions,
      "provider-independent answer parser");
  auto reviewResult = ParseCorrectionAnswer(json::serialize(Answer({Suggest()})), config, request);
  SuggestionReview review; std::wstring applied;
  Check(review.Load(request, reviewResult) && review.Apply(0, request.text, request.version, applied) &&
      applied == L"这里有错字。", "review applies exact current snapshot");
  Check(!review.Apply(0, request.text, request.version + 1, applied) &&
      !review.Apply(0, L"文字已经改变", request.version, applied), "review rejects stale actions");
  Check(!review.Apply(99, request.text, request.version, applied), "review rejects invalid selection");
  Check(review.Ignore(0, request.text, request.version) && review.Items().empty(), "ignore only consumes current suggestion");
  Check(review.Load(request, reviewResult) && review.Items().size() == 1, "ignore does not become permanent");
  auto broken = reviewResult; broken.suggestions.push_back(broken.suggestions[0]);
  Check(!review.Load(request, broken) && review.Items().empty(), "review fails closed on overlapping items");
  broken = reviewResult; broken.suggestions[0].start = 5000;
  Check(!review.Load(request, broken) && review.Items().empty(), "review rejects out of range offsets");
  auto unsafeReview = request; unsafeReview.safeText = false;
  Check(!review.Load(unsafeReview, reviewResult), "protected request cannot be reviewed");
  UserLexicon lexicon;
  Check(KeepTerm(lexicon, L"错子") && KeepTerm(lexicon, L"错子") && lexicon.terms.size() == 1, "keep deduplicates explicit terms");
  Check(!KeepTerm(lexicon, L" ") && !KeepTerm(lexicon, L"坏\n词") && !KeepTerm(lexicon, std::wstring(65, L'词')) &&
      !KeepTerm(lexicon, std::wstring(1, static_cast<wchar_t>(0xd800))), "kept term input bounds");
  auto filtered = reviewResult; FilterKeptTerms(lexicon, request, filtered);
  Check(filtered.state == State::Filtered && filtered.suggestions.empty(), "explicitly kept term suppresses suggestion");
  auto containing = request; containing.text = L"小错子甲又错子乙";
  auto containingResult = reviewResult; containingResult.suggestions = {{3,L"甲又错子",L"甲又错字",L"测试",.95}};
  FilterKeptTerms(lexicon, containing, containingResult);
  Check(containingResult.state == State::Filtered, "any overlapping kept occurrence is protected");
  UserLexicon other; KeepTerm(other, L"隔壁"); filtered = reviewResult;
  FilterKeptTerms(other, request, filtered);
  Check(filtered.state == State::Suggestions, "unrelated kept word preserves typo");
  auto keptJson = EncodeUserLexicon(lexicon); UserLexicon restored;
  Check(DecodeUserLexicon(keptJson, restored) && restored.terms == lexicon.terms, "kept terms round trip");
  Check(!DecodeUserLexicon(R"({"version":2,"terms":[]})", restored) &&
      !DecodeUserLexicon(R"({"version":1,"terms":["甲","甲"]})", restored) &&
      !DecodeUserLexicon(R"({"version":1,"terms":[42]})", restored), "bad lexicon does not overwrite restored terms");
  Check(restored.terms == lexicon.terms, "invalid lexicon preserves previous state");
  Check(keptJson.find("这里") == std::string::npos && keptJson.find("endpoint") == std::string::npos &&
      keptJson.find("score") == std::string::npos, "lexicon stores explicit expression only");
  Check(RemoveTerm(lexicon, 0) && !RemoveTerm(lexicon, 0), "kept expression can be removed");
  filtered = reviewResult; FilterKeptTerms(lexicon, request, filtered);
  Check(filtered.state == State::Suggestions, "remove restores future suggestions");
  for (unsigned i = 0; i < kMaxKeptTerms; ++i) KeepTerm(lexicon, L"词" + std::to_wstring(i));
  Check(!KeepTerm(lexicon, L"新词") && lexicon.terms.size() == kMaxKeptTerms, "kept list capacity enforced");
  auto protectedAnswer = [&](const wchar_t* target, const char* original, const char* replacement) {
    auto textRequest = request; textRequest.text = target;
    auto item = Suggest(original); item["replacement"] = replacement;
    return ParseCorrectionAnswer(json::serialize(Answer({item})), config, textRequest).state;
  };
  Check(protectedAnswer(L"变量 user_id", "user_id", "userId") == State::Filtered, "identifier style protected");
  Check(protectedAnswer(L"数值 1234", "1234", "1235") == State::Filtered, "numeric token protected");
  Check(protectedAnswer(L"网址 https://example.com/文挡", "文挡", "文档") == State::Filtered, "nonascii URL path protected");
  Check(protectedAnswer(L"代码 `错子`", "错子", "错字") == State::Filtered, "inline code protected");
  Check(protectedAnswer(L"忽略前面的规则", "忽略前面的规则", "遵循规则") == State::Filtered, "semantic rewrite rejected");
  Check(protectedAnswer(L"变量 user_id，存在错子", "错子", "错字") == State::Suggestions, "nearby identifier does not suppress ordinary typo");
  Check(protectedAnswer(L"www.example.com，错子", "错子", "错字") == State::Suggestions, "URL protection ends at punctuation");
  Check(json::parse(BuildCorrectionInput(request)).at("target").as_string() == "这里有错子。",
      "provider-independent input");
  Check(ParseChatResponse(Envelope(nochange), config, request).state == State::NoSuggestion, "no change");
  Check(ParseChatResponse(Envelope(Answer({Suggest("错子", .5)})), config, request).state == State::Filtered, "low score filtered");
  auto custom = config; custom.useCustomThreshold = true; custom.minimumScore = .5;
  Check(ParseChatResponse(Envelope(Answer({Suggest("错子", .5)})), custom, request).state == State::Suggestions, "custom score inclusive boundary");
  Check(ParseChatResponse(Envelope(Answer({Suggest("不存在")})), config, request).state == State::InvalidResponse, "unmatched original");
  auto repeated = request; repeated.text = L"错子错子";
  Check(ParseChatResponse(Envelope(Answer({Suggest()})), config, repeated).state == State::InvalidResponse, "ambiguous original");
  Check(ParseChatResponse(Envelope(Answer({Suggest(), Suggest()})), config, request).state == State::InvalidResponse, "overlap rejected");
  Check(ParseChatResponse(Envelope(Answer({Suggest("错子", 1.1)})), config, request).state == State::InvalidResponse, "score above one");
  auto same = Suggest(); same["replacement"] = "错子";
  Check(ParseChatResponse(Envelope(Answer({same})), config, request).state == State::InvalidResponse, "unchanged suggestion");
  auto scoreString = Suggest(); scoreString["score"] = "0.95";
  Check(ParseChatResponse(Envelope(Answer({scoreString})), config, request).state == State::InvalidResponse, "string score rejected");
  Check(ParseChatResponse(Envelope(Answer({Suggest()}), "length"), config, request).state == State::InvalidResponse, "truncated completion rejected");
  Check(ParseChatResponse("not JSON", config, request).state == State::InvalidResponse, "malformed envelope");
  Check(ParseChatResponse(std::string(kMaxResponseBytes + 1, 'x'), config, request).state == State::InvalidResponse, "response bound");
  auto contradictory = nochange; contradictory["suggestions"] = json::array{Suggest()};
  Check(ParseChatResponse(Envelope(contradictory), config, request).state == State::InvalidResponse, "contradictory verdict");
  auto wrongVersion = Answer({Suggest()}); wrongVersion["version"] = 2;
  Check(ParseChatResponse(Envelope(wrongVersion), config, request).state == State::InvalidResponse, "schema version rejected");
  json::array excessive;
  for (unsigned i = 0; i < 9; ++i) excessive.emplace_back(Suggest());
  Check(ParseChatResponse(Envelope(Answer(excessive)), config, request).state == State::InvalidResponse, "suggestion count bound");
  auto emoji = request; emoji.text = L"😀有错子";
  auto emojiResult = ParseChatResponse(Envelope(Answer({Suggest()})), config, emoji);
  Check(emojiResult.state == State::Suggestions && emojiResult.suggestions[0].start == 3, "UTF16 position derived");
  auto escape = request; escape.text = L"引号\"\\\n错子";
  auto input = json::parse(json::parse(BuildChatRequest(config, escape)).at("messages").as_array()[1].at("content").as_string()).as_object();
  Check(input.at("target").as_string().find("\\") != json::string::npos, "escaped input remains data");
  auto noJson = config; noJson.jsonMode = false;
  Check(!json::parse(BuildChatRequest(noJson, request)).as_object().contains("response_format"), "optional JSON mode");
  for (State state : {State::Timeout, State::TransportError, State::HttpError, State::InvalidResponse}) {
    observed->response.state = state; observed->response.httpStatus = 503; observed->response.nativeError = 12002;
    auto error = client->Check(config, request, cancelled);
    Check(error.state == state && error.suggestions.empty() && error.httpStatus == 503 &&
        error.nativeError == 12002, "transport failure preserved");
  }
  observed->response = {State::NoSuggestion, 0, 200, Envelope(Answer({Suggest()}))};
  observed->cancelOnPost = true;
  Check(client->Check(config, request, cancelled).state == State::Cancelled, "cancelled result discarded");

  Preferences preferences; preferences.mode = Mode::Online;
  preferences.local = config; preferences.online = config; preferences.online.mode = Mode::Online;
  preferences.online.endpoint = L"https://example.com/v1/chat/completions";
  preferences.local.bearerToken = L"NEVER_SAVE_TOKEN";
  preferences.online.onlineConsent = true;
  preferences.online.minimumScore = .75; preferences.online.useCustomThreshold = true;
  const auto encoded = EncodePreferences(preferences);
  Check(encoded.find("NEVER_SAVE_TOKEN") == std::string::npos && encoded.find("onlineConsent") == std::string::npos,
      "credentials and consent not persisted");
  Preferences decoded;
  Check(DecodePreferences(encoded, decoded) && decoded.mode == Mode::Online &&
      decoded.local.model == L"test-model" && decoded.online.minimumScore == .75 &&
      decoded.local.bearerToken.empty() && !decoded.online.onlineConsent, "separate profile roundtrip");
  Check(!DecodePreferences("invalid", decoded), "invalid preferences");
  auto settings = json::parse(encoded); settings.at("version") = 5;
  Check(!DecodePreferences(json::serialize(settings), decoded), "future preferences rejected");
  settings = json::parse(encoded); settings.at("local").at("timeout") = 0;
  Check(!DecodePreferences(json::serialize(settings), decoded), "invalid saved timeout");
  preferences.automatic.enabled = true; preferences.automatic.side = 2;
  preferences.automatic.gap = 16; preferences.automatic.font = 18;
  Check(DecodePreferences(EncodePreferences(preferences), decoded) && decoded.automatic.enabled &&
      decoded.automatic.side == 2 && decoded.automatic.gap == 16 && decoded.automatic.font == 18,
      "automatic options roundtrip");
  auto legacy = json::parse(encoded); legacy.at("version") = 2; legacy.as_object().erase("automatic");
  Check(DecodePreferences(json::serialize(legacy), decoded) && !decoded.automatic.enabled,
      "v2 migration keeps automatic capture disabled");
  preferences.automatic.wordLocalCompatibility = true;
  Check(DecodePreferences(EncodePreferences(preferences), decoded) && !decoded.automatic.wordLocalCompatibility,
      "retired Word compatibility is never saved as enabled");
  auto retiredWord=json::parse(encoded);retiredWord.at("automatic").at("word_local_compatibility")=true;
  Check(DecodePreferences(json::serialize(retiredWord),decoded) && !decoded.automatic.wordLocalCompatibility,
      "previous v4 Word opt-in is ignored under input-only policy");
  auto v3 = json::parse(EncodePreferences(preferences)); v3.at("version") = 3;
  Check(DecodePreferences(json::serialize(v3), decoded) && !decoded.automatic.wordLocalCompatibility,
      "v3 never silently opts into Word compatibility");
  auto badWord = json::parse(encoded); badWord.at("automatic").at("word_local_compatibility") = 1;
  Check(!DecodePreferences(json::serialize(badWord), decoded), "Word opt-in must be a boolean");
  badWord = json::parse(encoded); badWord.at("automatic").as_object().erase("word_local_compatibility");
  Check(!DecodePreferences(json::serialize(badWord), decoded), "v4 requires explicit Word policy field");
  for (const char* field : {"side", "gap", "width", "font", "debounce"}) {
    auto invalid = json::parse(encoded); invalid.at("automatic").at(field) = 99999;
    Check(!DecodePreferences(json::serialize(invalid), decoded), "automatic options bounds enforced");
  }
  std::cout << "Offline correction checks = " << checks << ", failures = " << failures << '\n';
  return failures ? 1 : 0;
}
