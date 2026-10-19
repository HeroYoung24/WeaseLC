#pragma once
#include <CorrectionModel.h>
#include "FixedAutomaticReply.h"

namespace correction {
// Compiled into the independent test product only; the production parser and
// kept-term/threshold checks still run. There is no public fixture switch.
class FixedAutomaticTransport final : public IModelTransport {
 public:
  explicit FixedAutomaticTransport(bool typo) : typo_(typo) {}
  TransportResult Post(const ModelConfig&, const Endpoint&, const std::string&, const std::atomic_bool&) override {
    TransportResult result; result.state = State::NoSuggestion; result.httpStatus = 200;
    result.body = typo_
      ? R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"{\"version\":1,\"decision\":\"suggestions\",\"suggestions\":[{\"original\":\"\u9519\u5b50\",\"replacement\":\"\u9519\u5b57\",\"reason\":\"fixture\",\"score\":0.99}]}"}}]})"
      : R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"{\"version\":1,\"decision\":\"no_change\",\"suggestions\":[]}"}}]})";
    return result;
  }
 private:
  bool typo_;
};
}
