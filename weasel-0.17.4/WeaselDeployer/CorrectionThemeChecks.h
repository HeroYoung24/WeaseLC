#pragma once
#include <CorrectionHintTheme.h>
#include <WeaselStyleColors.h>
#include <functional>
#include <CorrectionAppPolicy.h>
#include <CorrectionMenuState.h>
#include "LocalModelIdentity.h"
#include "CorrectionBehaviorChecks.h"
#include "CorrectionAppPolicyChecks.h"

inline void CheckCorrectionThemes(const std::function<void(bool, const char*)>& check) {
  using namespace correction;
  CheckCorrectionBehavior(check);
  CheckAppPolicyRegistry(check);
  HMENU menu = CreatePopupMenu();
  check(menu != nullptr, "native correction menu fixture created");
  if (menu) {
    AppendMenuW(menu, MF_STRING, ID_WEASELTRAY_CORRECTION_PAUSE, L"test");
    // Reuse one menu through every transition, including checked -> unchecked
    // and disabled -> enabled; no host preferences are read or written.
    for (int flags = 3; flags >= 0; --flags) {
      const bool inputOnly = (flags & 1) != 0, paused = (flags & 2) != 0;
      UpdateCorrectionMenuState(menu, inputOnly, paused);
      const auto pauseState = GetMenuState(menu, ID_WEASELTRAY_CORRECTION_PAUSE, MF_BYCOMMAND);
      check(!!(pauseState & MF_CHECKED) == (inputOnly || paused), "native menu pause check reflects current policy");
      check(!!(pauseState & (MF_GRAYED | MF_DISABLED)) == inputOnly, "native menu cannot enable Word correction");
    }
    UpdateCorrectionMenuState(menu, false, false, false);
    check((GetMenuState(menu, ID_WEASELTRAY_CORRECTION_PAUSE, MF_BYCOMMAND) & MF_GRAYED) != 0,
        "stale background language bar cannot pause another foreground application");
    UpdateCorrectionMenuState(menu, false, false, true);
    check(!(GetMenuState(menu, ID_WEASELTRAY_CORRECTION_PAUSE, MF_BYCOMMAND) & (MF_GRAYED | MF_DISABLED)),
        "foreground application can recover the pause menu");
    DestroyMenu(menu);
  }
  HWND host = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
  HWND other = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
  HWND child = host ? CreateWindowW(L"STATIC", L"", WS_CHILD, 0, 0, 1, 1, host, nullptr, nullptr, nullptr) : nullptr;
  check(host && other && child, "foreground menu test owns hidden windows only");
  check(CorrectionMenuTargetsForeground(child, host), "child text view matches its foreground application root");
  check(!CorrectionMenuTargetsForeground(child, other), "different window cannot receive the pause action");
  check(!CorrectionMenuTargetsForeground(nullptr, host) && !CorrectionMenuTargetsForeground(host, nullptr),
      "unknown menu target cannot change application preferences");
  if (host) DestroyWindow(host); if (other) DestroyWindow(other);
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
  style.back_color = 0xff222222; style.candidate_text_color = 0xffd8e3e6;
  style.hilited_candidate_back_color = 0xff333333; style.hilited_candidate_text_color = 0xff92f6da;
  const auto darkAccent = ResolveHintPalette(&style, false, system);
  check(darkAccent.accent == RGB(0xda, 0xf6, 0x92) && HintContrast(darkAccent.accent, darkAccent.background) >= 4.5,
      "dark theme uses readable native highlight text when selected background cannot emphasize");
  style.back_color = 0xffffffff; style.hilited_candidate_back_color = 0xfffa3a0a;
  check(ResolveHintPalette(&style, false, system).accent == RGB(0x0a, 0x3a, 0xfa),
      "readable light theme accent stays unchanged");
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
