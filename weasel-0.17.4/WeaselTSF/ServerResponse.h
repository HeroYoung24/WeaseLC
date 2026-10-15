#pragma once
#include <WeaselIPCData.h>
#include <memory>
#include <string>
#include <functional>
#include <ResponseParser.h>

struct ServerResponse {
  std::wstring commit;
  weasel::Config config;
  std::shared_ptr<weasel::Context> context = std::make_shared<weasel::Context>();
  weasel::Status status;
  weasel::UIStyle style;
};

template <typename Pull>
std::shared_ptr<ServerResponse> CaptureServerResponse(const weasel::Status& status,
    const weasel::UIStyle& style, Pull&& pull) {
  auto response = std::make_shared<ServerResponse>();
  response->status = status; response->style = style;
  weasel::ResponseParser parser(&response->commit, response->context.get(),
      &response->status, &response->config, &response->style);
  if (!pull(std::ref(parser))) return nullptr;
  return response;
}
