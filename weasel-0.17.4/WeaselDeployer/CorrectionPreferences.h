#pragma once
#include <CorrectionModel.h>
#include <CorrectionAutomatic.h>
#include <LocalModelDefaults.h>

namespace correction {
struct Preferences {
  Mode mode = Mode::Off;
  unsigned timing = 0;  // next composition / immediately after result
  AutomaticOptions automatic;
  ModelConfig local;
  ModelConfig online;
  Preferences() {
    local.mode = Mode::Local;
    local.endpoint = kManagedModelEndpoint;
    local.model = kManagedModelName;
    local.timeoutSeconds = kManagedModelTimeoutSeconds;
    online.mode = Mode::Online;
    online.endpoint.clear();
  }
};
inline bool LocalAutomaticEnabled(const Preferences& preferences) {
  return preferences.mode == Mode::Local && preferences.automatic.enabled;
}
inline bool UsesManagedModel(const ModelConfig& config) {
  return config.endpoint == kManagedModelEndpoint && config.model == kManagedModelName;
}
enum class PreferencesStage : unsigned {
  NotRead, Missing, QueryFailed, InvalidSize, ReadFailed, DecodeFailed, Ready
};
struct PreferencesRead {
  PreferencesStage stage = PreferencesStage::NotRead;
  unsigned error = 0, bytes = 0;
};
Preferences ReadPreferences(const wchar_t* path, PreferencesRead* = nullptr);
Preferences LoadPreferences(PreferencesRead* = nullptr);
bool SavePreferences(const Preferences&);
// Pure codec used by the offline test; excludes credentials, consent and text.
std::string EncodePreferences(const Preferences&);
bool DecodePreferences(const std::string&, Preferences&);
}  // namespace correction
