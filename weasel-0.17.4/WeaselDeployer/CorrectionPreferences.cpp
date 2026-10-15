#include <windows.h>
#include <stdexcept>
#define BOOST_JSON_NO_LIB
#define BOOST_CONTAINER_NO_LIB
#include <boost/json.hpp>
#include <CorrectionProbe.h>
#include "CorrectionPreferences.h"

namespace correction {
namespace {
namespace json = boost::json;
std::string Utf8(const std::wstring& input) {
  if (input.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  if (!size) throw std::runtime_error("encoding");
  std::string output(size, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), output.data(), size, nullptr, nullptr);
  return output;
}
std::wstring Wide(const json::value& value) {
  const auto& input = value.as_string();
  if (input.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), nullptr, 0);
  if (!size) throw std::runtime_error("encoding");
  std::wstring output(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
      static_cast<int>(input.size()), output.data(), size);
  return output;
}
json::object Profile(const ModelConfig& config) {
  return {{"endpoint", Utf8(config.endpoint)}, {"model", Utf8(config.model)},
      {"timeout", config.timeoutSeconds}, {"sensitivity", config.sensitivity},
      {"custom_threshold", config.useCustomThreshold}, {"threshold", config.minimumScore},
      {"json_mode", config.jsonMode}};
}
void ReadProfile(const json::value& value, ModelConfig& config) {
  const auto& object = value.as_object();
  config.endpoint = Wide(object.at("endpoint"));
  config.model = Wide(object.at("model"));
  config.timeoutSeconds = json::value_to<unsigned>(object.at("timeout"));
  config.sensitivity = json::value_to<unsigned>(object.at("sensitivity"));
  config.useCustomThreshold = object.at("custom_threshold").as_bool();
  config.minimumScore = json::value_to<double>(object.at("threshold"));
  config.jsonMode = object.at("json_mode").as_bool();
  // A saved profile may be incomplete, but it must be bounded. Validation before
  // each actual request still enforces transport/security rules.
  if (config.endpoint.size() > 2048 || config.model.size() > 128 ||
      config.timeoutSeconds < 2 || config.timeoutSeconds > 60 || config.sensitivity > 2 ||
      !(config.minimumScore >= 0 && config.minimumScore <= 1)) throw std::runtime_error("bounds");
}
}  // namespace
std::string EncodePreferences(const Preferences& preferences) {
  return json::serialize(json::object{{"version", 2},
      {"mode", static_cast<unsigned>(preferences.mode)}, {"timing", preferences.timing},
      {"local", Profile(preferences.local)}, {"online", Profile(preferences.online)}});
}
bool DecodePreferences(const std::string& encoded, Preferences& preferences) {
  if (encoded.size() > 16384) return false;
  try {
    json::parse_options options; options.max_depth = 8;
    auto object = json::parse(encoded, {}, options).as_object();
    if (object.at("version").as_int64() != 2) return false;
    Preferences decoded;
    const auto mode = json::value_to<unsigned>(object.at("mode"));
    decoded.timing = json::value_to<unsigned>(object.at("timing"));
    if (mode > 2 || decoded.timing > 1) return false;
    decoded.mode = static_cast<Mode>(mode);
    ReadProfile(object.at("local"), decoded.local);
    ReadProfile(object.at("online"), decoded.online);
    preferences = std::move(decoded);
    return true;
  } catch (...) { return false; }
}
Preferences LoadPreferences() {
  Preferences preferences;
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, L"SettingsV2", RRF_RT_REG_BINARY,
      nullptr, nullptr, &size) == ERROR_SUCCESS && size && size <= 16384) {
    std::string encoded(size, '\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, L"SettingsV2", RRF_RT_REG_BINARY,
        nullptr, encoded.data(), &size) == ERROR_SUCCESS) {
      encoded.resize(size);
      DecodePreferences(encoded, preferences);
    }
  }
  return preferences;
}
bool SavePreferences(const Preferences& preferences) {
  try {
    const std::string encoded = EncodePreferences(preferences);
    Preferences validated;
    if (!DecodePreferences(encoded, validated)) return false;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const auto status = RegSetValueExW(key, L"SettingsV2", 0, REG_BINARY,
        reinterpret_cast<const BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
  } catch (...) { return false; }
}
}  // namespace correction
