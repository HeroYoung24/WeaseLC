#pragma once
#include <CorrectionHintTheme.h>
#include <WeaselStyleColors.h>
#include <functional>
#include <CorrectionAppPolicy.h>
#include "LocalModelIdentity.h"

inline void CheckCorrectionThemes(const std::function<void(bool, const char*)>& check) {
  using namespace correction;
  const HintPalette system{RGB(255, 255, 255), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 128)};
  check(ResolveHintPalette(nullptr, false, system) == system, "missing theme uses system palette");
  weasel::UIStyle style;
  for (const auto bg : {0xffeeeeeeu, 0xff181818u, 0x80eeeeeeu, 0u}) {
    style.back_color = bg; style.candidate_text_color = 0xff777777;
    style.hilited_candidate_back_color = bg;
    const auto palette = ResolveHintPalette(&style, false, system);
    check(HintContrast(palette.text, palette.background) >= 4.5, "theme body stays readable");
    check(HintContrast(palette.accent, palette.background) >= 4.5, "theme suggestion stays readable");
    auto highContrast = system; highContrast.accent = highContrast.text;
    check(ResolveHintPalette(&style, true, system) == highContrast, "high contrast ignores custom colors");
  }
  check(FlattenHintColor(0x80000000, RGB(255, 255, 255)) == RGB(127, 127, 127), "alpha flattened before GDI painting");
  check(weasel::style_config::blend_colors(0, 0) == 0, "fully transparent upstream blend is defined");
  auto* api = rime_get_api();
  for (const auto* format : {"abgr", "argb", "rgba"}) {
    const std::string yaml = std::string("style: {color_scheme: test}\npreset_color_schemes:\n  test:\n    color_format: ") + format +
        "\n    back_color: 0xff181818\n    candidate_text_color: 0xffeeeeee\n    hilited_candidate_back_color: 0xffee8833\n";
    RimeConfig config{};
    const bool loaded = api->config_load_string(&config, yaml.c_str()) != False;
    check(loaded && weasel::style_config::LoadColors(api, &config, style, {}), "existing theme loader reused for preview");
    if (loaded) {
      const auto palette = ResolveHintPalette(&style, false, system);
      check(HintContrast(palette.text, palette.background) >= 4.5 &&
          HintContrast(palette.accent, palette.background) >= 4.5, "all existing color formats readable");
      int parsed = 0;
      weasel::style_config::_RimeGetColor(api, &config, "preset_color_schemes/test/hilited_candidate_back_color",
          parsed, weasel::style_config::COLOR_ABGR, 0);
      check(static_cast<unsigned>(parsed) == 0xffee8833, "ABGR parser preserves existing color bits");
    }
    if (config.ptr) api->config_close(&config);
  }
  std::wstring app;
  check(AppName(L"MsEdge.EXE", app) && app == L"msedge.exe", "application policy uses normalized executable identity");
  for (const auto* invalid : {L"", L"edge", L"C:\\msedge.exe", L"../msedge.exe", L"*.exe", L"a.exe\nb.exe", L" app.exe"})
    check(!AppName(invalid, app), "application policy rejects paths wildcards and control characters");
  std::vector<std::wstring> apps{L"sentinel.exe"};
  check(DecodePausedApps({0, 0}, apps) && apps.empty(), "empty pause list is valid");
  std::vector<wchar_t> encoded{L'a', L'.', L'e', L'x', L'e', 0, 0};
  check(DecodePausedApps(encoded, apps) && apps == std::vector<std::wstring>{L"a.exe"}, "pause policy bounded decoding");
  for (const auto& invalid : {std::vector<wchar_t>{0}, std::vector<wchar_t>{0, 0, L'x'},
      std::vector<wchar_t>{L'a', 0, 0}, std::vector<wchar_t>{0, 0, 0},
      std::vector<wchar_t>{L'a', L'.', L'e', L'x', L'e', 0, 0, L'x', 0, 0}})
    check(!DecodePausedApps(invalid, apps), "malformed app policies never accept a prefix");
  check(FlattenHintColor(0, system.background) == system.background, "transparent theme fallback remains unchanged");
  const std::string ready = R"({"processId":123,"variant":"4b","alias":"weasel-qwen3-4b","port":18963,"thinkingBudget":0,"contextTokens":8192,"modelSha256":"7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5"})";
  check(ValidOwnedModelReady(ready, [](DWORD pid) { return pid == 123; }), "model readiness checks frozen identity and actual process ownership");
  check(!ValidOwnedModelReady(ready, [](DWORD) { return false; }), "existing external model cannot be adopted through a ready file");
  for (const char* field : {"variant", "alias", "port", "thinkingBudget", "contextTokens", "modelSha256", "processId"}) {
    auto invalid = boost::json::parse(ready).as_object(); invalid.erase(field);
    check(!ValidOwnedModelReady(boost::json::serialize(invalid), [](DWORD) { return true; }), "missing model identity fields fail closed");
  }
  auto changed = boost::json::parse(ready); changed.at("thinkingBudget") = 16;
  check(!ValidOwnedModelReady(boost::json::serialize(changed), [](DWORD) { return true; }), "GUI controller never silently changes frozen model parameters");
  check(!ValidOwnedModelReady(std::string(9000, 'x'), [](DWORD) { return true; }), "model readiness is size bounded");
}
