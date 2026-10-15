#pragma once

static LONG WINAPI DiagnosticHostExceptionFilter(EXCEPTION_POINTERS*) {
  return EXCEPTION_CONTINUE_SEARCH;
}

// Test only the loader in this single-threaded diagnostic process. No crash,
// COM activation, registration, user document, or external process is involved.
static bool TestHostExceptionFilter(const WCHAR* dll) {
  struct RestoreHostFilter {
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(nullptr);
    ~RestoreHostFilter() { SetUnhandledExceptionFilter(previous); }
  } restore;
  bool valid = true;
  for (int scenario = 0; scenario < 2; ++scenario) {
    const LPTOP_LEVEL_EXCEPTION_FILTER expected = scenario == 0 ? nullptr :
        DiagnosticHostExceptionFilter;
    SetUnhandledExceptionFilter(expected);
    HMODULE module = LoadLibraryW(dll);
    if (!module) {
      std::wcout << L"Host boundary LoadLibrary error = " << GetLastError() << L'\n';
      return false;
    }
    const auto loadedFilter = SetUnhandledExceptionFilter(expected);
    const bool loadedValid = loadedFilter == expected;
    // Keep the DLL's observed state until detach, to detect a dangling filter.
    // The pointer is compared only; this test never invokes either callback.
    SetUnhandledExceptionFilter(loadedFilter);
    const BOOL freed = FreeLibrary(module);
    const auto unloadedFilter = SetUnhandledExceptionFilter(expected);
    const bool unloadedValid = freed && unloadedFilter == expected;
    std::wcout << L"Host exception filter scenario " << scenario
               << L", preserved after load = " << loadedValid
               << L", preserved after unload = " << unloadedValid << L'\n';
    valid = loadedValid && unloadedValid && valid;
  }
  return valid;
}
