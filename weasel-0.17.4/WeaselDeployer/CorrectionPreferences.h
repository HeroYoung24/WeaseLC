#pragma once
#include <CorrectionModel.h>

namespace correction {
struct Preferences {
  Mode mode = Mode::Off;
  unsigned timing = 0;  // Reserved for future automatic checks, not manual ones.
  ModelConfig local;
  ModelConfig online;
  Preferences() {
    local.mode = Mode::Local;
    online.mode = Mode::Online;
    online.endpoint.clear();
  }
};
Preferences LoadPreferences();
bool SavePreferences(const Preferences&);
// Pure codec used by the offline test; excludes credentials, consent and text.
std::string EncodePreferences(const Preferences&);
bool DecodePreferences(const std::string&, Preferences&);
}  // namespace correction
