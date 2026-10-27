#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace correction {
inline UINT AutomaticWindowDpi(HWND window) {
  using GetDpi = UINT(WINAPI*)(HWND);
  const auto get = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  return get && window ? get(window) : 96;
}
struct AutomaticOptions {
  bool enabled = false;
  bool tab = true;
  bool wordLocalCompatibility = false;  // Explicit opt-in; IRM status may be unavailable.
  unsigned side = 0;  // right, left, below
  unsigned gap = 8;   // logical pixels at 96 DPI
  unsigned width = 320;
  unsigned font = 14;
  unsigned debounce = 250;
};
inline bool ValidAutomaticOptions(const AutomaticOptions& o) {
  return o.side <= 2 && o.gap <= 48 && o.width >= 180 && o.width <= 640 &&
      o.font >= 10 && o.font <= 24 && o.debounce >= 100 && o.debounce <= 2000;
}
// Private, bounded anonymous-pipe protocol. No pointers, filenames, handles,
// credentials, host context or arbitrary commands cross this boundary.
#pragma pack(push, 1)
struct AutomaticRequest {
  std::uint32_t magic = 0x31434357, version = 1, units = 0;
  WCHAR text[1025] = {};
};
struct AutomaticReply {
  std::uint32_t magic = 0x31524357, version = 1, count = 0;
  struct Item {
    std::uint32_t start = 0, originalUnits = 0, replacementUnits = 0;
    double score = 0;
    WCHAR original[33] = {}, replacement[33] = {};
  } items[8];
};
#pragma pack(pop)
inline bool ValidAutomaticText(const WCHAR* text, unsigned units, unsigned limit) {
  if (!units || units > limit || text[units]) return false;
  for (unsigned i = 0; i < units; ++i) {
    const auto c = static_cast<unsigned>(text[i]);
    if (c < 32 || c == 127) return false;
    if (c >= 0xd800 && c <= 0xdbff) {
      if (++i >= units || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
    } else if (c >= 0xdc00 && c <= 0xdfff) return false;
  }
  return true;
}
inline RECT PlaceAutomaticHint(RECT anchor, RECT work, SIZE size,
                               unsigned side, LONG gap) {
  const LONG width = (std::max)(1L, (std::min)(size.cx, work.right - work.left));
  const LONG height = (std::max)(1L, (std::min)(size.cy, work.bottom - work.top));
  LONG x = side == 1 ? anchor.left - gap - width : side == 2 ? anchor.left : anchor.right + gap;
  LONG y = side == 2 ? anchor.bottom + gap : anchor.top;
  if (side == 0 && x + width > work.right) x = anchor.left - gap - width;
  if (side == 1 && x < work.left) x = anchor.right + gap;
  if (side == 2 && y + height > work.bottom) y = anchor.top - gap - height;
  x = (std::max)(work.left, (std::min)(x, work.right - width));
  y = (std::max)(work.top, (std::min)(y, work.bottom - height));
  RECT placed{x, y, x + width, y + height}, intersection{};
  if (!IntersectRect(&intersection, &placed, &anchor)) return placed;
  // Clamping a preferred side may cover candidates. Try the other free sides.
  const RECT alternatives[] = {
      {anchor.left, anchor.bottom + gap, anchor.left + width, anchor.bottom + gap + height},
      {anchor.left, anchor.top - gap - height, anchor.left + width, anchor.top - gap},
      {anchor.right + gap, anchor.top, anchor.right + gap + width, anchor.top + height},
      {anchor.left - gap - width, anchor.top, anchor.left - gap, anchor.top + height}};
  for (auto alternative : alternatives) {
    if (alternative.bottom <= anchor.top || alternative.top >= anchor.bottom) {
      const auto offset = (std::max)(work.left, (std::min)(alternative.left, work.right - width)) - alternative.left;
      OffsetRect(&alternative, offset, 0);
    } else {
      const auto offset = (std::max)(work.top, (std::min)(alternative.top, work.bottom - height)) - alternative.top;
      OffsetRect(&alternative, 0, offset);
    }
    if (alternative.left >= work.left && alternative.top >= work.top && alternative.right <= work.right &&
        alternative.bottom <= work.bottom && !IntersectRect(&intersection, &alternative, &anchor)) return alternative;
  }
  return {};  // No unobscured location: omit the hint instead of covering input.
}
}  // namespace correction
