#pragma once
#include <string>

namespace correction {
enum class FixedVerdict { Reject, NoChange, Suggest };
// Compiled synthetic corpus only. Prefixes permit word-by-word commits;
// Punctuation variants must not make the wiring test depend on IME settings.
// This bound belongs only to the compiled fixture, not the real model product.
inline FixedVerdict FixedAutomaticVerdict(const std::wstring& text) {
  const std::wstring word = L"这里有错子";
  if (text.size() >= word.size() && text.size() - word.size() <= 8 &&
      text.compare(0, word.size(), word) == 0 &&
      text.find_first_not_of(L"。，、；：！？.,;:!?…", word.size()) == std::wstring::npos)
    return FixedVerdict::Suggest;
  if (!text.empty() && text.size() < word.size() && word.compare(0, text.size(), text) == 0)
    return FixedVerdict::NoChange;
  return FixedVerdict::Reject;
}
}  // namespace correction
