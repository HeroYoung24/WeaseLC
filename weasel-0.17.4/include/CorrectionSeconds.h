#pragma once
#include <string>
namespace correction {
// Exact decimal seconds, no floating-point rounding or locale dependence.
inline bool ParseSeconds(const std::wstring& text, unsigned low, unsigned high, unsigned& milliseconds) {
  if (text.empty() || text.size() > 7) return false;
  unsigned seconds = 0, fraction = 0, digits = 0; bool point = false, integer = false;
  for (wchar_t c : text) {
    if (c == L'.' && !point && integer) { point = true; continue; }
    if (c < L'0' || c > L'9') return false;
    if (point) { if (++digits > 3) return false; fraction = fraction * 10 + c - L'0'; }
    else { integer = true; seconds = seconds * 10 + c - L'0'; if (seconds > 60) return false; }
  }
  if (point && !digits) return false;
  while (digits++ < 3) fraction *= 10;
  const unsigned value = seconds * 1000 + fraction;
  if (value < low || value > high) return false;
  milliseconds = value; return true;
}
inline std::wstring FormatSeconds(unsigned milliseconds) {
  auto text = std::to_wstring(milliseconds / 1000);
  if (milliseconds % 1000) {
    auto tail = std::to_wstring(1000 + milliseconds % 1000).substr(1);
    while (tail.back() == L'0') tail.pop_back();
    text += L"." + tail;
  }
  return text;
}
}
