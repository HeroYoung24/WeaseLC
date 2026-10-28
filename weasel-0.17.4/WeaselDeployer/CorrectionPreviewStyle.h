#pragma once
#include <WeaselStyleColors.h>
#include <WeaselUtility.h>
#include <fstream>

// Parse only an already deployed style config. Never initialize the Rime engine,
// create user dictionaries, request a server session, or deploy settings here.
inline bool LoadCorrectionPreviewStyle(weasel::UIStyle& style) {
  try {
    std::ifstream stream(WeaselUserDataPath() / L"build" / L"weasel.yaml", std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    if (!stream || size <= 0 || size > 512 * 1024) return false;
    std::string yaml(static_cast<size_t>(size), '\0'); stream.seekg(0);
    if (!stream.read(yaml.data(), static_cast<std::streamsize>(yaml.size())) || yaml.find('\0') != std::string::npos) return false;
    auto* api = rime_get_api();
    RimeConfig config{};
    struct ConfigScope { RimeApi* api; RimeConfig& config; ~ConfigScope() { if (config.ptr) api->config_close(&config); } } scope{api, config};
    if (!RIME_API_AVAILABLE(api, config_load_string)) return false;
    const bool loaded = api->config_load_string(&config, yaml.c_str()) != False;
    bool result = false;
    if (loaded) {
      std::string scheme;
      DWORD light = 1, bytes = sizeof(light);
      if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes) == ERROR_SUCCESS && !light) {
        char dark[256]{};
        if (api->config_get_string(&config, "style/color_scheme_dark", dark, sizeof(dark))) scheme = dark;
      }
      result = weasel::style_config::LoadColors(api, &config, style, scheme);
    }
    return result;
  } catch (...) { return false; }
}
