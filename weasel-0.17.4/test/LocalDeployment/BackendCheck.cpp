#include <windows.h>
#include <iostream>
#include <string>
#include <WeaselIPC.h>
#include <ResponseParser.h>

#ifndef WEASELC_LOCAL_TEST_BUILD
#error This check must never connect to the daily backend
#endif

int wmain() {
  // Send only fixed synthetic keystrokes into a new private Rime session.
  // No keyboard injection, TSF activation, clipboard or external document.
  weasel::Client client;
  if (!client.Connect()) return 2;
  client.StartSession();
  weasel::Status status;
  std::wstring committed;
  const auto read = [&] {
    std::wstring text;
    weasel::ResponseParser parser(&text, nullptr, &status);
    const bool ok = client.GetResponseData(std::ref(parser));
    committed += text;
    return ok;
  };
  if (!read() || !client.Echo()) return 3;
  // A fresh test directory selects luna_pinyin_simp and starts in Chinese.
  if (status.ascii_mode) return 4;
  for (const wchar_t key : std::wstring(L"nihao ")) {
    if (!client.ProcessKeyEvent(weasel::KeyEvent(key, 0)) || !read()) return 5;
    client.ProcessKeyEvent(weasel::KeyEvent(key, ibus::RELEASE_MASK));
    if (!read()) return 6;
  }
  client.EndSession();
  const bool ok = committed == L"你好";
  std::wcout << L"Private backend commit: " << committed << L"\nResult: " << (ok ? L"PASS" : L"FAIL") << L'\n';
  return ok ? 0 : 7;
}
