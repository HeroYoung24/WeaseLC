#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <initializer_list>

namespace weasel {
inline bool ParseSingleOption(const wchar_t* line, std::wstring& option) {
  int count = 0;
  auto* arguments = CommandLineToArgvW(line, &count);
  if (!arguments) return false;
  bool valid = count == 1 || count == 2;
  try { if (valid) option = count == 2 ? arguments[1] : L""; }
  catch (...) { valid = false; }
  LocalFree(arguments);
  return valid;
}
inline bool OptionIn(const std::wstring& option, std::initializer_list<const wchar_t*> known) {
  for (const auto* value : known) if (option == value) return true;
  return false;
}
inline bool DeployerOption(const std::wstring& option) {
  return OptionIn(option, {L"",L"/?",L"/help",L"/deploy",L"/dict",L"/sync",L"/install",
      L"/correction",L"/correction-demo",L"/correction-worker",L"/correction-worker-selftest",L"/correction-broker",
      L"/correction-selftest",L"/correction-local-selftest",L"/rime-learning-selftest",L"/local-model",L"/local-model-selftest"});
}
inline bool ServerOption(const std::wstring& option) {
  return OptionIn(option, {L"",L"/userdir",L"/weaseldir",L"/ascii",L"/nascii",L"/q",L"/quit",L"/update"});
}
inline bool SetupOption(const std::wstring& option) {
  return (option.size() > 9 && option.compare(0,9,L"/userdir:") == 0) ||
      OptionIn(option, {L"",L"/?",L"/help",L"/u",L"/s",L"/t",L"/i",L"/ls",L"/lt",L"/le",
      L"/eu",L"/du",L"/toggleime",L"/toggleascii",L"/testing",L"/release"});
}
}  // namespace weasel
