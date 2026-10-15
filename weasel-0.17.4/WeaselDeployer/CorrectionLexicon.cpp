#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <stdexcept>
#define BOOST_JSON_NO_LIB
#define BOOST_CONTAINER_NO_LIB
#include <boost/json.hpp>
#include <CorrectionLexicon.h>
#include <CorrectionProbe.h>

namespace correction {
namespace {
namespace json = boost::json;
constexpr DWORD kMaxLexiconBytes = 128 * 1024;
bool ValidTerm(const std::wstring& term) {
  if (term.empty() || term.size() > kMaxKeptTermUnits || iswspace(term.front()) || iswspace(term.back())) return false;
  for (std::size_t i = 0; i < term.size(); ++i) {
    const unsigned c = term[i];
    if (c < 32 || c == 127) return false;
    if (c >= 0xd800 && c <= 0xdbff) {
      if (++i == term.size() || term[i] < 0xdc00 || term[i] > 0xdfff) return false;
    } else if (c >= 0xdc00 && c <= 0xdfff) return false;
  }
  return true;
}
std::string Utf8(const std::wstring& text) {
  if (!ValidTerm(text)) throw std::runtime_error("term");
  int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (!size) throw std::runtime_error("encoding");
  std::string out(size, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}
std::wstring Wide(const json::value& value) {
  const auto& text = value.as_string();
  if (text.empty() || text.size() > kMaxKeptTermUnits * 4) throw std::runtime_error("term");
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (!size) throw std::runtime_error("encoding");
  std::wstring out(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}
}  // namespace
bool KeepTerm(UserLexicon& lexicon, const std::wstring& term) {
  if (!ValidTerm(term)) return false;
  if (std::find(lexicon.terms.begin(), lexicon.terms.end(), term) != lexicon.terms.end()) return true;
  if (lexicon.terms.size() >= kMaxKeptTerms) return false;
  lexicon.terms.push_back(term); return true;
}
bool RemoveTerm(UserLexicon& lexicon, std::size_t index) {
  if (index >= lexicon.terms.size()) return false;
  lexicon.terms.erase(lexicon.terms.begin() + index); return true;
}
void FilterKeptTerms(const UserLexicon& lexicon, const CheckRequest& request, CheckResult& result) {
  if (result.state != State::Suggestions || result.session != request.session || result.version != request.version) return;
  auto& items = result.suggestions;
  items.erase(std::remove_if(items.begin(), items.end(), [&](const Suggestion& item) {
    for (const auto& term : lexicon.terms) {
      if (!ValidTerm(term)) continue;
      for (auto at = request.text.find(term); at != std::wstring::npos; at = request.text.find(term, at + 1))
        if (item.start < at + term.size() && at < item.start + item.original.size()) return true;
    }
    return false;
  }), items.end());
  if (items.empty()) result.state = State::Filtered;
}
std::string EncodeUserLexicon(const UserLexicon& lexicon) {
  if (lexicon.terms.size() > kMaxKeptTerms) throw std::runtime_error("bounds");
  json::array terms; UserLexicon unique;
  for (const auto& term : lexicon.terms) {
    if (!KeepTerm(unique, term) || unique.terms.size() != terms.size() + 1) throw std::runtime_error("term");
    terms.emplace_back(Utf8(term));
  }
  return json::serialize(json::object{{"version", 1}, {"terms", std::move(terms)}});
}
bool DecodeUserLexicon(const std::string& encoded, UserLexicon& lexicon) {
  if (encoded.size() > kMaxLexiconBytes) return false;
  try {
    json::parse_options options; options.max_depth = 4;
    auto object = json::parse(encoded, {}, options).as_object();
    if (object.at("version").as_int64() != 1) return false;
    const auto& terms = object.at("terms").as_array();
    if (terms.size() > kMaxKeptTerms) return false;
    UserLexicon decoded;
    for (const auto& term : terms) {
      const auto previous = decoded.terms.size();
      if (!KeepTerm(decoded, Wide(term)) || decoded.terms.size() != previous + 1) return false;
    }
    lexicon = std::move(decoded); return true;
  } catch (...) { return false; }
}
bool ReadUserLexiconValue(HKEY key, UserLexicon& lexicon) {
  DWORD size = 0;
  auto status = RegGetValueW(key, nullptr, L"KeptTermsV1", RRF_RT_REG_BINARY, nullptr, nullptr, &size);
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) { lexicon = {}; return true; }
  if (status != ERROR_SUCCESS || !size || size > kMaxLexiconBytes) return false;
  std::string encoded(size, '\0');
  status = RegGetValueW(key, nullptr, L"KeptTermsV1", RRF_RT_REG_BINARY, nullptr, encoded.data(), &size);
  if (status != ERROR_SUCCESS) return false;
  encoded.resize(size); return DecodeUserLexicon(encoded, lexicon);
}
bool WriteUserLexiconValue(HKEY key, const UserLexicon& lexicon) {
  try {
    const auto encoded = EncodeUserLexicon(lexicon);
    UserLexicon checked; if (!DecodeUserLexicon(encoded, checked)) return false;
    const auto status = RegSetValueExW(key, L"KeptTermsV1", 0, REG_BINARY,
        reinterpret_cast<const BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
    return status == ERROR_SUCCESS;
  } catch (...) { return false; }
}
bool LoadUserLexicon(UserLexicon& lexicon) {
  HKEY key = nullptr;
  const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_QUERY_VALUE, &key);
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) { lexicon = {}; return true; }
  if (status != ERROR_SUCCESS) return false;
  const bool valid = ReadUserLexiconValue(key, lexicon); RegCloseKey(key); return valid;
}
bool SaveUserLexicon(const UserLexicon& lexicon) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const bool valid = WriteUserLexiconValue(key, lexicon); RegCloseKey(key); return valid;
}
}  // namespace correction
