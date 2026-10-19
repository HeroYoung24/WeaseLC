#pragma once
#include <string>

namespace correction {
enum class FixedVerdict { Reject, NoChange, Suggest };
// Compiled synthetic corpus only. Prefixes permit word-by-word commits;
// punctuation variants must not make the wiring test depend on IME settings.
inline FixedVerdict FixedAutomaticVerdict(const std::wstring& text) {
  const std::wstring word = L"这里有错子";
  if (text == word || text == word + L"。" || text == word + L".") return FixedVerdict::Suggest;
  if (!text.empty() && text.size() < word.size() && word.compare(0, text.size(), text) == 0)
    return FixedVerdict::NoChange;
  return FixedVerdict::Reject;
}
}  // namespace correction
