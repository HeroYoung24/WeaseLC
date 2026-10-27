#pragma once
#include <windows.h>
#include <cwchar>

namespace correction {
// Product compatibility policy, independent of text attributes and Office COM.
// Word keeps ordinary Rime input; automatic correction never starts there.
inline bool InputOnlyExecutable(const wchar_t* image) {
  if (!image || !*image) return true;  // Unknown host: preserve normal input.
  const auto slash = wcsrchr(image,L'\\');
  const auto forward = wcsrchr(image,L'/');
  const auto base = !slash ? forward : !forward ? slash : slash > forward ? slash : forward;
  return !_wcsicmp(base ? base+1 : image,L"WINWORD.EXE");
}
inline bool InputOnlyHost() {
  static const bool inputOnly=[] {
    wchar_t image[32768]{};
    const auto length=GetModuleFileNameW(nullptr,image,_countof(image));
    return !length || length>=_countof(image) || InputOnlyExecutable(image);
  }();
  return inputOnly;
}
}  // namespace correction
