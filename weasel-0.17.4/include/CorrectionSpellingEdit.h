#pragma once
#include <windows.h>
#include <algorithm>
#include <string>

namespace correction {
struct SpellingEditBounds {
  size_t prefix = 0, original = 0, replacement = 0;
};
// Spelling suggestions may quote unchanged punctuation, but may never change
// it. Strip common context for validation; this is not a punctuation editor.
inline bool SpellingEdit(const std::wstring& before, const std::wstring& after,
                         SpellingEditBounds* output = nullptr) {
  if (before.empty() || after.empty() || before == after) return false;
  size_t prefix = 0, suffix = 0;
  while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
  // A matching high surrogate does not make half a scalar an edit boundary.
  if (prefix && prefix < before.size() && before[prefix] >= 0xdc00 && before[prefix] <= 0xdfff) --prefix;
  while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
         before[before.size()-suffix-1] == after[after.size()-suffix-1]) ++suffix;
  if (suffix && before[before.size()-suffix] >= 0xdc00 && before[before.size()-suffix] <= 0xdfff) --suffix;
  const SpellingEditBounds bounds{prefix, before.size()-prefix-suffix, after.size()-prefix-suffix};
  const auto wordsOnly = [&](const std::wstring& text, size_t units) {
    for (size_t i = prefix; i < prefix + units; ++i) {
      WORD type = 0;
      if (!GetStringTypeW(CT_CTYPE1, text.data()+i, 1, &type) ||
          (type & (C1_PUNCT | C1_SPACE | C1_CNTRL))) return false;
    }
    return true;
  };
  if (!wordsOnly(before, bounds.original) || !wordsOnly(after, bounds.replacement)) return false;
  if (output) *output = bounds;
  return true;
}
}  // namespace correction
