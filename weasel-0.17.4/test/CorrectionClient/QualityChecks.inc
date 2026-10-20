#include "QualityCorpus.h"
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
  explicit QualityTransport(std::string prompt) : prompt_(std::move(prompt)) {}
  std::string response;
  TransportResult Post(const ModelConfig& config, const Endpoint& endpoint, const std::string& body,
      const std::atomic_bool& cancelled) override {
    auto request = json::parse(body);
    request.at("messages").as_array()[0].at("content") = prompt_;
    auto result = http_->Post(config, endpoint, json::serialize(request), cancelled);
    response = result.body;
    return result;
  }
 private:
  std::string prompt_;
  std::unique_ptr<IModelTransport> http_ = CreateHttpTransport();
};
// Only compiled synthetic text, local HTTP, production request/parser/protection
// and normal threshold. No settings, TSF registration, or external text inputs.
static int RunQualityChecks(const wchar_t* endpoint, const wchar_t* modelName,
    const wchar_t* candidate, const wchar_t* corpus, const wchar_t* output) {
  const bool validation = !wcscmp(corpus, L"validation");
  const bool diagnosticCorpus = !wcscmp(corpus, L"diagnostic");
  const bool contractAudit = !wcscmp(corpus, L"contract-audit");
  if (!validation && !diagnosticCorpus && !contractAudit && wcscmp(corpus, L"development")) return 2;
  if (wcscmp(candidate, L"baseline") && wcscmp(candidate, L"phonetic") && wcscmp(candidate, L"verify") &&
      wcscmp(candidate,L"minimal") && wcscmp(candidate,L"contract") && wcscmp(candidate,L"current")) return 2;
  const std::filesystem::path destination(output);
  WCHAR module[32768]{}; const auto length = GetModuleFileNameW(nullptr, module, _countof(module));
  if (!length || length >= _countof(module)) return 2;
  const auto workspaceOutput = std::filesystem::path(module).parent_path().parent_path().parent_path();
  if (std::filesystem::weakly_canonical(destination.parent_path()) !=
      std::filesystem::weakly_canonical(workspaceOutput / L"LocalTest") || std::filesystem::exists(destination)) return 2;
  ModelConfig config; config.mode = Mode::Local; config.endpoint = endpoint; config.model = modelName;
  config.timeoutSeconds = 10;
  if (!ValidateConfig(config)) return 2;
  auto transport = std::make_unique<QualityTransport>(QualityPrompt(candidate));
  auto* observed = transport.get();
  auto client = CreateModelClient(Provider::ChatCompletions, std::move(transport));
  const std::atomic_bool cancelled{false};
  unsigned expectedTypos = 0, corrected = 0, controls = 0, falseAlarms = 0, wrong = 0, missed = 0, invalid = 0;
  json::array samples; std::vector<ULONGLONG> latencies;
  for (const auto& sample : QualityCorpus(validation, diagnosticCorpus, contractAudit)) {
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
    samples.emplace_back(std::move(entry));
    std::cout << "Quality sample " << samples.size() << " state=" << static_cast<unsigned>(result.state) << std::endl;
  }
  std::sort(latencies.begin(), latencies.end());
  json::object report{{"corpus",QualityUtf8(corpus)}, {"candidate",QualityUtf8(candidate)},
    {"model",QualityUtf8(config.model)}, {"threshold",EffectiveThreshold(config)},
    {"systemPrompt",QualityPrompt(candidate)}, {"expectedTypos",expectedTypos}, {"corrected",corrected},
    {"missed",missed}, {"wrongCorrections",wrong}, {"controls",controls}, {"falseAlarms",falseAlarms},
    {"invalid",invalid}, {"medianMs",latencies[latencies.size()/2]}, {"maxMs",latencies.back()}, {"samples",std::move(samples)}};
  std::ofstream file(destination, std::ios::binary | std::ios::out);
  file << json::serialize(report);
  std::cout << "Quality corrected=" << corrected << '/' << expectedTypos << " falseAlarms=" << falseAlarms
      << '/' << controls << " wrong=" << wrong << " invalid=" << invalid << std::endl;
  return file.good() && corrected == expectedTypos && !falseAlarms && !wrong && !invalid ? 0 : 1;
}
