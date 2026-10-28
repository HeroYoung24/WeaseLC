#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <CorrectionHintTheme.h>

namespace correction {
class HintFont {
 public:
  HintFont(unsigned pixels, UINT dpi, bool bold = false) {
    font_ = CreateFontW(-MulDiv(pixels, dpi, 96), 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
  }
  ~HintFont() { if (font_) DeleteObject(font_); }
  HFONT Get() const { return font_ ? font_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)); }
  HintFont(const HintFont&) = delete;
  HintFont& operator=(const HintFont&) = delete;
 private:
  HFONT font_ = nullptr;
};
struct HintLayout {
  RECT title{}, original{}, replacement{}, footer{};
  LONG height = 0;
  bool complete = true;
};
inline const wchar_t* HintTitle(bool chinese, bool preview = false) {
  return preview ? (chinese ? L"纠错建议（预览示例）" : L"Correction preview") :
      (chinese ? L"纠错建议" : L"Correction suggestion");
}
inline std::wstring HintOriginal(const std::wstring& text, bool chinese) { return (chinese ? L"原文：" : L"Original: ") + text; }
inline std::wstring HintReplacement(const std::wstring& text, bool chinese) { return (chinese ? L"建议：" : L"Suggested: ") + text; }
inline const wchar_t* HintFooter(bool tab, bool chinese, bool preview = false) {
  if (preview) return chinese ? L"仅显示示例，不检查或修改文字" : L"Preview only. No checks or edits.";
  return tab ? (chinese ? L"Tab 接受建议  ·  Esc 忽略" : L"Tab accepts  ·  Esc dismisses") :
      (chinese ? L"仅提示（Tab 已关闭）  ·  Esc 忽略" : L"Hints only (Tab off)  ·  Esc dismisses");
}
inline HintLayout MeasureHint(HDC dc, LONG width, UINT dpi, unsigned font,
    const std::wstring& original, const std::wstring& replacement, bool tab, bool chinese, bool preview = false) {
  HintLayout result;
  const LONG padding = MulDiv(12, dpi, 96), gap = MulDiv(8, dpi, 96);
  LONG y = padding;
  HintFont title(12, dpi, true), body(font, dpi), suggestion(font, dpi, true), footer(11, dpi);
  auto measure = [&](const std::wstring& text, HFONT face, RECT& bounds) {
    const auto previous = SelectObject(dc, face);
    RECT measured{0, 0, (std::max)(1L, width - 2 * padding), 0};
    const auto height = DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured,
        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    result.complete = result.complete && height > 0 && measured.right <= width - 2 * padding;
    bounds = {padding, y, width - padding, y + height}; y += height + gap;
    SelectObject(dc, previous);
  };
  measure(HintTitle(chinese, preview), title.Get(), result.title);
  measure(HintOriginal(original, chinese), body.Get(), result.original);
  measure(HintReplacement(replacement, chinese), suggestion.Get(), result.replacement);
  measure(HintFooter(tab, chinese, preview), footer.Get(), result.footer);
  result.height = y - gap + padding;
  return result;
}
inline void PaintHint(HDC dc, RECT bounds, UINT dpi, unsigned font,
    const std::wstring& original, const std::wstring& replacement, bool tab, bool chinese,
    bool preview = false, const weasel::UIStyle* style = nullptr) {
  const auto layout = MeasureHint(dc, bounds.right - bounds.left, dpi, font, original, replacement, tab, chinese, preview);
  const auto palette = CurrentHintPalette(style);
  HBRUSH background = CreateSolidBrush(palette.background), border = CreateSolidBrush(palette.border);
  FillRect(dc, &bounds, background);
  FrameRect(dc, &bounds, border);
  DeleteObject(background); DeleteObject(border);
  RECT accent = bounds; accent.right = accent.left + MulDiv(3, dpi, 96);
  HBRUSH blue = CreateSolidBrush(palette.accent);
  FillRect(dc, &accent, blue); DeleteObject(blue);
  HintFont title(12, dpi, true), body(font, dpi), suggestion(font, dpi, true), footer(11, dpi);
  const int saved = SaveDC(dc);
  IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
  SetBkMode(dc, TRANSPARENT);
  auto paint = [&](const std::wstring& text, HFONT face, RECT rect, bool emphasize) {
    OffsetRect(&rect, bounds.left, bounds.top);
    SelectObject(dc, face);
    SetTextColor(dc, emphasize ? palette.accent : palette.text);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_WORDBREAK | DT_NOPREFIX);
  };
  paint(HintTitle(chinese, preview), title.Get(), layout.title, false);
  paint(HintOriginal(original, chinese), body.Get(), layout.original, false);
  paint(HintReplacement(replacement, chinese), suggestion.Get(), layout.replacement, true);
  paint(HintFooter(tab, chinese, preview), footer.Get(), layout.footer, false);
  RestoreDC(dc, saved);
}
}  // namespace correction
