#pragma once
#include <windows.h>
#include <WeaselIPCData.h>
#include <algorithm>
#include <cmath>

namespace correction {
struct HintPalette {
  COLORREF background, text, border, accent;
  bool operator==(const HintPalette& other) const {
    return background == other.background && text == other.text &&
        border == other.border && accent == other.accent;
  }
};
inline HintPalette SystemHintPalette() {
  return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT),
      GetSysColor(COLOR_WINDOWFRAME), GetSysColor(COLOR_HIGHLIGHT)};
}
// UIStyle stores ABGR, GDI accepts COLORREF. Flatten alpha against the actual
// background so transparent themes cannot make a GDI suggestion unreadable.
inline COLORREF FlattenHintColor(unsigned abgr, COLORREF background) {
  const unsigned alpha = abgr >> 24;
  auto blend = [alpha](unsigned front, unsigned back) {
    return static_cast<BYTE>((front * alpha + back * (255 - alpha) + 127) / 255);
  };
  return RGB(blend(GetRValue(abgr), GetRValue(background)),
      blend(GetGValue(abgr), GetGValue(background)), blend(GetBValue(abgr), GetBValue(background)));
}
inline double HintLuminance(COLORREF color) {
  auto linear = [](unsigned c) { const double v = c / 255.0; return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
  return .2126 * linear(GetRValue(color)) + .7152 * linear(GetGValue(color)) + .0722 * linear(GetBValue(color));
}
inline double HintContrast(COLORREF a, COLORREF b) {
  const double x = HintLuminance(a), y = HintLuminance(b);
  return ((std::max)(x, y) + .05) / ((std::min)(x, y) + .05);
}
inline HintPalette ResolveHintPalette(const weasel::UIStyle* style, bool highContrast,
    HintPalette system) {
  if (highContrast) { system.accent = system.text; return system; }
  if (!style) return system;
  HintPalette result = system;
  result.background = FlattenHintColor(style->back_color, system.background);
  const auto text = style->candidate_text_color ? style->candidate_text_color : style->text_color;
  result.text = FlattenHintColor(text, result.background);
  const auto readable = HintContrast(RGB(0, 0, 0), result.background) >=
      HintContrast(RGB(255, 255, 255), result.background) ? RGB(0, 0, 0) : RGB(255, 255, 255);
  if (HintContrast(result.text, result.background) < 4.5) result.text = readable;
  result.border = FlattenHintColor(style->border_color, result.background);
  result.accent = FlattenHintColor(style->hilited_candidate_back_color, result.background);
  if (HintContrast(result.accent, result.background) < 4.5) result.accent = result.text;
  return result;
}
inline HintPalette CurrentHintPalette(const weasel::UIStyle* style) {
  HIGHCONTRASTW contrast{sizeof(contrast)};
  SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
  return ResolveHintPalette(style, (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0, SystemHintPalette());
}
}  // namespace correction
