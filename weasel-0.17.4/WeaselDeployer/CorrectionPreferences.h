#pragma once
#include <CorrectionModel.h>
#include <CorrectionAutomatic.h>

namespace correction {
struct Preferences {
  Mode mode = Mode::Off;
  unsigned timing = 0;  // next composition / immediately after result
  AutomaticOptions automatic;
  ModelConfig local;
  ModelConfig online;
  Preferences() {
    local.mode = Mode::Local;
    online.mode = Mode::Online;
    online.endpoint.clear();
  }
};
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
