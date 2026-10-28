#pragma once
#include <windows.h>
#include <boost/json.hpp>
#include <functional>

inline bool ValidOwnedModelReady(std::string text, const std::function<bool(DWORD)>& owns) {
  try {
    if (text.empty() || text.size() > 8192) return false;
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    boost::json::parse_options options; options.max_depth = 4;
    const auto value = boost::json::parse(text, {}, options).as_object();
    const auto pid = boost::json::value_to<DWORD>(value.at("processId"));
    return pid && value.at("variant") == "4b" && value.at("alias") == "weasel-qwen3-4b" &&
        value.at("port") == 18963 && value.at("thinkingBudget") == 0 && value.at("contextTokens") == 8192 &&
        value.at("modelSha256") == "7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5" && owns(pid);
  } catch (...) { return false; }
}
