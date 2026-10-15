#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Run this client in the controller process, never on the IME input thread.
// No provider is invoked unless the caller explicitly submits a safe request.
namespace correction {
enum class Mode : unsigned { Off, Local, Online };
enum class Provider : unsigned { ChatCompletions, CodexCliReserved };
enum class Source : unsigned { Manual, CommittedText, HostNearby };
enum class State {
  Off, NoSuggestion, Filtered, Suggestions, InvalidInput, InvalidConfig,
  ProtectedInput, Cancelled, Timeout, TransportError, HttpError, InvalidResponse
};

struct ModelConfig {
  Mode mode = Mode::Off;
  Provider provider = Provider::ChatCompletions;
  std::wstring endpoint = L"http://127.0.0.1:1234/v1/chat/completions";
  std::wstring model;
  // Credentials are supplied for this process only; never persisted or logged.
  std::wstring bearerToken;
  unsigned timeoutSeconds = 30;
  unsigned sensitivity = 0;
  bool useCustomThreshold = false;
  double minimumScore = 0.90;
  bool jsonMode = true;
  bool onlineConsent = false;
};

struct CheckRequest {
  std::uint64_t session = 0;
  std::uint64_t version = 0;
  Source source = Source::Manual;
  bool safeText = false;  // Unknown/password contexts must leave this false.
  std::wstring text;
  std::wstring before;
  std::wstring after;
};
struct Suggestion {
  std::size_t start = 0;  // Derived from original text, in UTF-16 units.
  std::wstring original;
  std::wstring replacement;
  std::wstring reason;
  double score = 0;       // Uncalibrated model score, not a probability.
};
struct CheckResult {
  State state = State::InvalidResponse;
  std::uint64_t session = 0;
  std::uint64_t version = 0;
  unsigned nativeError = 0;
  unsigned httpStatus = 0;
  std::vector<Suggestion> suggestions;
};

constexpr std::size_t kMaxTargetUnits = 1024;
constexpr std::size_t kMaxNearbyUnits = 256;
constexpr std::size_t kMaxResponseBytes = 64 * 1024;
constexpr std::size_t kMaxSuggestions = 8;

struct Endpoint {
  std::wstring host, path;
  unsigned short port = 0;
  bool secure = false;
};
struct TransportResult {
  State state = State::TransportError;
  unsigned nativeError = 0;
  unsigned httpStatus = 0;
  std::string body;
};

// Injectable transport: offline fixtures exercise the same production parser.
class IModelTransport {
 public:
  virtual ~IModelTransport() = default;
  virtual TransportResult Post(const ModelConfig&, const Endpoint&,
      const std::string& body, const std::atomic_bool& cancelled) = 0;
};
class IModelClient {
 public:
  virtual ~IModelClient() = default;
  virtual CheckResult Check(const ModelConfig&, const CheckRequest&,
      const std::atomic_bool& cancelled) = 0;
};

bool ValidateConfig(const ModelConfig&, Endpoint* = nullptr);
double EffectiveThreshold(const ModelConfig&);
const char* CorrectionInstructions();
std::string BuildCorrectionInput(const CheckRequest&);
// Provider-independent JSON contract; a future CLI adapter can reuse this.
CheckResult ParseCorrectionAnswer(const std::string&, const ModelConfig&,
                                  const CheckRequest&);
std::string BuildChatRequest(const ModelConfig&, const CheckRequest&);
CheckResult ParseChatResponse(const std::string&, const ModelConfig&,
                              const CheckRequest&);
std::unique_ptr<IModelTransport> CreateHttpTransport();
// Reserved Codex provider fails closed. No CLI process or token access exists.
std::unique_ptr<IModelClient> CreateModelClient(Provider,
    std::unique_ptr<IModelTransport> transport = nullptr);
const wchar_t* StateText(State, bool chinese);
}  // namespace correction
