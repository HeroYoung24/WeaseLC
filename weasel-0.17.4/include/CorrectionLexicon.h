#pragma once
#include <windows.h>
#include <CorrectionModel.h>

namespace correction {
constexpr std::size_t kMaxKeptTerms = 256, kMaxKeptTermUnits = 64;
struct UserLexicon { std::vector<std::wstring> terms; };
bool KeepTerm(UserLexicon&, const std::wstring&);
bool RemoveTerm(UserLexicon&, std::size_t index);
// Suppress changes overlapping any occurrence of an explicitly kept term.
// Never put these terms into the model payload; this is local post-processing.
void FilterKeptTerms(const UserLexicon&, const CheckRequest&, CheckResult&);
std::string EncodeUserLexicon(const UserLexicon&);
bool DecodeUserLexicon(const std::string&, UserLexicon&);
bool LoadUserLexicon(UserLexicon&);
bool SaveUserLexicon(const UserLexicon&);
// Store adapter accepts an already opened key for scoped persistence testing.
bool ReadUserLexiconValue(HKEY, UserLexicon&);
bool WriteUserLexiconValue(HKEY, const UserLexicon&);
}  // namespace correction
