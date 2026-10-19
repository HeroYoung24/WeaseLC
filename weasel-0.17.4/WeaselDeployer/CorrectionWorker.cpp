#include <windows.h>
#include <CorrectionAutomatic.h>
#include <CorrectionLexicon.h>
#include <CorrectionPipelineDiagnostics.h>
#include "CorrectionPreferences.h"
#include "CorrectionWorker.h"

#ifdef WEASELC_LOCAL_TEST_BUILD
#include "../test/LocalDeployment/FixedAutomaticTransport.h"
#endif

int RunCorrectionWorker(bool syntheticTest) {
  using namespace correction;
  // This entry runs before ATL, Rime, TSF, user-directory creation and deployment.
  AutomaticRequest input;
  DWORD read = 0;
  if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &input, sizeof(input), &read, nullptr) ||
      read != sizeof(input) || input.magic != 0x31434357 || input.version != 1 ||
      !ValidAutomaticText(input.text, input.units, 1024)) return 2;
  AutomaticReply reply;
  const auto started = GetTickCount64();
  WorkerDiagnostic diagnostic; diagnostic.units = input.units;
  auto record = [&](WorkerStage stage) {
#ifdef WEASELC_LOCAL_TEST_BUILD
    if (!syntheticTest) {
      diagnostic.stage = stage;
      diagnostic.elapsedMs = static_cast<DWORD>(GetTickCount64() - started);
      SavePipelineDiagnostic(L"LastWorker", diagnostic);
    }
#endif
  };
#ifdef WEASELC_LOCAL_TEST_BUILD
  const bool fixed = !syntheticTest && UseFixedAutomaticReply();
  diagnostic.provider = fixed ? 1 : 0;
#endif
  record(WorkerStage::Received);
#ifdef WEASELC_LOCAL_TEST_BUILD
  // Accept compiled prefixes too: a person can commit the sentence in several
  // pieces and pause between them. Valid no-change keeps the owned context.
  const auto verdict = fixed ? FixedAutomaticVerdict(std::wstring(input.text, input.units)) : FixedVerdict::Reject;
  if (fixed && verdict == FixedVerdict::Reject) {
    record(WorkerStage::Rejected); SecureZeroMemory(&input, sizeof(input)); return 4;
  }
#endif
  try {
    auto preferences = syntheticTest ? Preferences{} : LoadPreferences();
    if (syntheticTest) {
      // Diagnostic mode accepts only compiled synthetic samples and never
      // reads or writes the user's preferences/lexicon or launches a CLI.
      const std::wstring sample(input.text, input.units);
      if (sample != L"这里有错子。" && sample != L"这里有错") return 4;
      preferences = Preferences{}; preferences.mode = Mode::Local;
      preferences.automatic.enabled = true;
      preferences.local.endpoint = L"http://127.0.0.1:18963/v1/chat/completions";
      preferences.local.model = L"weasel-qwen3-4b"; preferences.local.timeoutSeconds = 5;
    }
    const auto& config = preferences.local;
    UserLexicon lexicon;
    // Automatic online capture needs a separate consent/credential design.
    if (preferences.mode == Mode::Local && preferences.automatic.enabled &&
        config.provider == Provider::ChatCompletions && ValidateConfig(config) &&
        config.bearerToken.empty() && (syntheticTest || LoadUserLexicon(lexicon))) {
      CheckRequest request;
      request.session = 1; request.version = 1; request.safeText = true;
      request.source = Source::CommittedText; request.text.assign(input.text, input.units);
      const std::atomic_bool cancelled{false};
      std::unique_ptr<IModelTransport> transport;
#ifdef WEASELC_LOCAL_TEST_BUILD
      if (fixed) transport = std::make_unique<FixedAutomaticTransport>(verdict == FixedVerdict::Suggest);
#endif
      auto client = CreateModelClient(config.provider, std::move(transport));
      record(WorkerStage::RequestStarted);
      auto result = client->Check(config, request, cancelled);
      diagnostic.state = static_cast<DWORD>(result.state);
      diagnostic.nativeError = result.nativeError; diagnostic.httpStatus = result.httpStatus;
      record(WorkerStage::Returned);
      // This test must receive an actual valid no-change response, rather than
      // confusing a transport/parse failure with the zero-suggestion contract.
      if (syntheticTest && request.text == L"这里有错" && result.state != State::NoSuggestion) return 6;
      if (!syntheticTest && !LoadUserLexicon(lexicon)) return 5;
      FilterKeptTerms(lexicon, request, result);
      diagnostic.state = static_cast<DWORD>(result.state);
      if (result.state == State::Suggestions) for (const auto& suggestion : result.suggestions) {
        if (reply.count == 8 || suggestion.original.size() > 32 || suggestion.replacement.size() > 32) continue;
        auto& item = reply.items[reply.count++];
        item.start = static_cast<unsigned>(suggestion.start);
        item.originalUnits = static_cast<unsigned>(suggestion.original.size());
        item.replacementUnits = static_cast<unsigned>(suggestion.replacement.size());
        item.score = suggestion.score;
        memcpy(item.original, suggestion.original.data(), item.originalUnits * sizeof(WCHAR));
        memcpy(item.replacement, suggestion.replacement.data(), item.replacementUnits * sizeof(WCHAR));
      }
    }
  } catch (...) { reply = {}; diagnostic.state = static_cast<DWORD>(State::InvalidResponse); }
  SecureZeroMemory(&input, sizeof(input));
  DWORD written = 0;
  const bool ok = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &reply, sizeof(reply), &written, nullptr) && written == sizeof(reply);
  record(ok ? WorkerStage::Written : WorkerStage::Rejected);
  SecureZeroMemory(&reply, sizeof(reply));
  return ok ? 0 : 3;
}
