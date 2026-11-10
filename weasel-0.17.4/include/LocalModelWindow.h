#pragma once
#include <windows.h>
#include <string>
#include "LocalTestIdentity.h"
namespace correction {
inline constexpr wchar_t kModelWindowProperty[] = WEASEL_PIPE_NAME L".model.control";
inline constexpr UINT kModelShow = WM_APP + 181, kModelStop = WM_APP + 182;
inline HWND FindModelControlWindow() {
  struct Search { std::wstring executable; HWND window = nullptr; } search;
  wchar_t file[32768]{};
  const auto size = GetModuleFileNameW(nullptr,file,_countof(file));
  if (!size || size >= _countof(file)) return nullptr;
  search.executable = file;
  EnumWindows([](HWND window, LPARAM parameter)->BOOL {
    auto& state = *reinterpret_cast<Search*>(parameter);
    if (!GetPropW(window,kModelWindowProperty)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(window,&pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return TRUE;
    wchar_t path[32768]{}; DWORD size = _countof(path);
    const bool owned = QueryFullProcessImageNameW(process,0,path,&size) && !_wcsicmp(path,state.executable.c_str());
    CloseHandle(process);
    if (!owned) return TRUE;
    state.window = window; return FALSE;
  },reinterpret_cast<LPARAM>(&search));
  return search.window;
}
inline bool SignalModelControl(bool stop) {
  const auto window = FindModelControlWindow();
  if (!window) return true;
  DWORD_PTR result = 0;
  return SendMessageTimeoutW(window,stop ? kModelStop : kModelShow,0,0,
      SMTO_ABORTIFHUNG,5000,&result) != 0;
}
}
