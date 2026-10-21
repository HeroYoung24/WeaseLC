#include "QualityCorpus.h"
#include "TuningCorpus.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
static std::string QualityUtf8(const std::wstring& input) {
  const int units = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  std::string result(units, '\0');
  if (units) WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), units, nullptr, nullptr);
  return result;
}
class QualityTransport final : public IModelTransport {
 public:
  explicit QualityTransport(std::string prompt, std::wstring profile, unsigned seed)
      : prompt_(std::move(prompt)), profile_(std::move(profile)), seed_(seed) {}
  std::string response;
  TransportResult Post(const ModelConfig& config, const Endpoint& endpoint, const std::string& body,
      const std::atomic_bool& cancelled) override {
    auto request = json::parse(body);
    request.at("messages").as_array()[0].at("content") = prompt_;
    // Test-only bounded sampling profile. Production configuration is untouched.
    if (profile_ != L"greedy" && profile_ != L"schema-greedy") {
      auto& parameters = request.as_object();
      parameters["temperature"] = .2; parameters["top_p"] = .8; parameters["seed"] = seed_;
      if (profile_ != L"low-temperature") {
        parameters["temperature"] = profile_ == L"qwen-fast" ? .7 : profile_ == L"qwen-mid" ? .4 : .6;
        parameters["top_p"] = profile_.find(L"thinking-")==0 ? .95 : .8;
        parameters["top_k"] = 20; parameters["min_p"] = 0.0;
      }
    }
    if (profile_ == L"schema-greedy") {
      request.as_object()["response_format"] = json::object{{"type","json_object"},{"schema",json::parse(R"({
        "type":"object","properties":{"version":{"const":1},"decision":{"enum":["suggestions","no_change"]},
        "suggestions":{"type":"array","maxItems":8,"items":{"type":"object","properties":{
        "original":{"type":"string","minLength":1,"maxLength":32},"replacement":{"type":"string","minLength":1,"maxLength":32},
        "reason":{"type":"string","maxLength":64},"score":{"type":"number","minimum":0,"maximum":1}},
        "required":["original","replacement","reason","score"],"additionalProperties":false}}},
        "required":["version","decision","suggestions"],"additionalProperties":false})")}};
    }
    auto result = http_->Post(config, endpoint, json::serialize(request), cancelled);
    response = result.body;
    return result;
  }
 private:
  std::string prompt_;
  std::wstring profile_;
  unsigned seed_;
  std::unique_ptr<IModelTransport> http_ = CreateHttpTransport();
};
// Only compiled synthetic text, local HTTP, production request/parser/protection
// and normal threshold. No settings, TSF registration, or external text inputs.
static int RunQualityChecks(const wchar_t* endpoint, const wchar_t* modelName,
    const wchar_t* candidate, const wchar_t* corpus, const wchar_t* output, const wchar_t* profile = L"greedy", unsigned seed = 104729) {
  const bool sampled = !wcscmp(profile,L"low-temperature");
  const bool official = !wcscmp(profile,L"qwen-fast") || !wcscmp(profile,L"qwen-mid");
  const bool thinking = !wcscmp(profile,L"thinking-32") || !wcscmp(profile,L"thinking-64");
  if (!sampled && !official && !thinking && wcscmp(profile,L"greedy") && wcscmp(profile,L"schema-greedy")) return 2;
  const bool speedValidation = !wcscmp(corpus,L"speed-validation");
  const bool tuningValidation = !wcscmp(corpus,L"tuning-validation");
  const bool tuningFinal = !wcscmp(corpus,L"tuning-final");
  const bool tuningDevelopment = !wcscmp(corpus,L"tuning-development");
  const bool validation = !wcscmp(corpus, L"validation");
  const bool diagnosticCorpus = !wcscmp(corpus, L"diagnostic");
  const bool contractAudit = !wcscmp(corpus, L"contract-audit");
  if (!validation && !diagnosticCorpus && !contractAudit && !speedValidation && !tuningValidation && !tuningFinal && !tuningDevelopment && wcscmp(corpus, L"development")) return 2;
  if (wcscmp(candidate, L"baseline") && wcscmp(candidate, L"phonetic") && wcscmp(candidate, L"verify") &&
      wcscmp(candidate,L"minimal") && wcscmp(candidate,L"contract") && wcscmp(candidate,L"current") && wcscmp(candidate,L"spelling-v2")) return 2;
  const std::filesystem::path destination(output);
  WCHAR module[32768]{}; const auto length = GetModuleFileNameW(nullptr, module, _countof(module));
  if (!length || length >= _countof(module)) return 2;
  const auto workspaceOutput = std::filesystem::path(module).parent_path().parent_path().parent_path();
  if (std::filesystem::weakly_canonical(destination.parent_path()) !=
      std::filesystem::weakly_canonical(workspaceOutput / L"LocalTest") || std::filesystem::exists(destination)) return 2;
  ModelConfig config; config.mode = Mode::Local; config.endpoint = endpoint; config.model = modelName;
  config.timeoutSeconds = 10;
  if (!ValidateConfig(config)) return 2;
  auto transport = std::make_unique<QualityTransport>(QualityPrompt(candidate),profile,seed);
  auto* observed = transport.get();
  auto client = CreateModelClient(Provider::ChatCompletions, std::move(transport));
  const std::atomic_bool cancelled{false};
  unsigned expectedTypos = 0, corrected = 0, controls = 0, falseAlarms = 0, wrong = 0, missed = 0, invalid = 0;
  json::array samples; std::vector<ULONGLONG> latencies;
  const auto samplesToCheck = tuningFinal ? TuningFinalCorpus() : tuningValidation ? TuningValidationCorpus() : tuningDevelopment ? TuningDevelopmentCorpus() :
      speedValidation ? SpeedValidationCorpus() : QualityCorpus(validation, diagnosticCorpus, contractAudit);
  for (const auto& sample : samplesToCheck) {
    CheckRequest request; request.safeText = true; request.source = Source::CommittedText;
    request.session = 1; request.version = samples.size() + 1; request.text = sample.text;
    auto expected = request.text;
    const bool typo = *sample.original != 0;
    if (typo) {
      const auto start = expected.find(sample.original);
      if (start == std::wstring::npos) return 2;
      expected.replace(start, wcslen(sample.original), sample.replacement); ++expectedTypos;
    } else ++controls;
    const auto started = GetTickCount64();
    const auto result = client->Check(config, request, cancelled);
    const auto elapsed = GetTickCount64() - started; latencies.push_back(elapsed);
    const bool valid = result.state == State::NoSuggestion || result.state == State::Filtered || result.state == State::Suggestions;
    auto displayed = request.text;
    auto suggestions = result.suggestions;
    std::sort(suggestions.begin(), suggestions.end(), [](const auto& a, const auto& b) { return a.start > b.start; });
    for (const auto& item : suggestions) displayed.replace(item.start, item.original.size(), item.replacement);
    if (!valid) ++invalid;
    else if (typo && displayed == expected && !suggestions.empty()) ++corrected;
    else if (typo && suggestions.empty()) ++missed;
    else if (typo) ++wrong;
    else if (!suggestions.empty()) ++falseAlarms;
    // Reparse exactly the same response at zero score in a diagnostic copy;
    // deterministic protected-region checks remain active. Never another call.
    auto diagnostic = config; diagnostic.useCustomThreshold = true; diagnostic.minimumScore = 0;
    const auto unfilteredScore = ParseChatResponse(observed->response, diagnostic, request);
    json::array proposed;
    for (const auto& item : unfilteredScore.suggestions)
      proposed.emplace_back(json::object{{"original",QualityUtf8(item.original)}, {"replacement",QualityUtf8(item.replacement)}, {"score",item.score}});
    json::object entry{{"text",QualityUtf8(request.text)}, {"expected",QualityUtf8(expected)},
      {"displayed",QualityUtf8(displayed)}, {"state",static_cast<unsigned>(result.state)},
      {"http",result.httpStatus}, {"nativeError",result.nativeError}, {"elapsedMs",elapsed},
      {"scoreOnlyDiagnostic",std::move(proposed)}};
    if (!valid) entry["syntheticResponse"] = observed->response;
    try {
      const auto envelope = json::parse(observed->response);
      if (const auto* usage = envelope.as_object().if_contains("usage")) entry["usage"] = *usage;
      const auto& choice = envelope.at("choices").as_array().at(0);
      entry["finishReason"] = choice.at("finish_reason");
      const auto& message = choice.at("message").as_object();
      const auto* reasoning = message.if_contains("reasoning_content");
      entry["reasoningBytes"] = reasoning && reasoning->is_string() ? reasoning->as_string().size() : 0;
    } catch (...) {}
    samples.emplace_back(std::move(entry));
    std::cout << "Quality sample " << samples.size() << " state=" << static_cast<unsigned>(result.state) << std::endl;
  }
  std::sort(latencies.begin(), latencies.end());
  json::object report{{"corpus",QualityUtf8(corpus)}, {"candidate",QualityUtf8(candidate)},
    {"model",QualityUtf8(config.model)}, {"threshold",EffectiveThreshold(config)},
    {"systemPrompt",QualityPrompt(candidate)}, {"profile",QualityUtf8(profile)},
    {"temperature",sampled ? .2 : official ? (!wcscmp(profile,L"qwen-fast") ? .7 : .4) : thinking ? .6 : 0.0},
    {"expectedTypos",expectedTypos}, {"corrected",corrected},
    {"missed",missed}, {"wrongCorrections",wrong}, {"controls",controls}, {"falseAlarms",falseAlarms},
    {"invalid",invalid}, {"medianMs",latencies[latencies.size()/2]},
    {"p95Ms",latencies[(latencies.size()*95+99)/100-1]}, {"maxMs",latencies.back()}, {"samples",std::move(samples)}};
  if (sampled) { report["top_p"] = .8; report["seed"] = seed; }
  if (official || thinking) {
    report["top_p"] = thinking ? .95 : .8; report["top_k"] = 20; report["min_p"] = 0.0; report["seed"] = seed;
  }
  std::ofstream file(destination, std::ios::binary | std::ios::out);
  file << json::serialize(report);
  std::cout << "Quality corrected=" << corrected << '/' << expectedTypos << " falseAlarms=" << falseAlarms
      << '/' << controls << " wrong=" << wrong << " invalid=" << invalid << std::endl;
  return file.good() && corrected == expectedTypos && !falseAlarms && !wrong && !invalid ? 0 : 1;
}
