#include <windows.h>
#include <iostream>
#include <string>
#include <WeaselIPC.h>
#include <ResponseParser.h>
#include <CorrectionBrokerClient.h>

#ifndef WEASELC_LOCAL_TEST_BUILD
#error This check must never connect to the daily backend
#endif

static int WordKeyContract() {
  struct Case { const wchar_t* name; const wchar_t* keys; const wchar_t* text; bool composing; };
  const Case cases[]{
      {L"single selection and punctuation",L"nihao1.",L"你好。",false},
      {L"deliberately duplicated selection",L"nihao11.",L"你好1",true},
      {L"ordinary decimal is preserved",L"3.14 ",L"3.14 ",false}};
  unsigned failures=0;
  for(const auto& item:cases){
    weasel::Client client;
    if(!client.Connect()){std::wcerr<<L"Private connection unavailable\n";return 2;}
    client.StartSession();
    struct End { weasel::Client& client; ~End(){client.EndSession();} } end{client};
    weasel::Status status;weasel::Context context;weasel::Config config;std::wstring host;
    auto read=[&](){
      std::wstring committed;weasel::ResponseParser parser(&committed,&context,&status,&config);
      const bool result=client.GetResponseData(std::ref(parser));host+=committed;return result;
    };
    if(!read()){std::wcerr<<L"Private initial response unavailable\n";return 3;}
    if(status.ascii_mode){std::wcerr<<L"Private session is in ASCII mode\n";return 4;}
    if(!client.Echo()){std::wcerr<<L"Private session unavailable\n";return 5;}
    bool complete=true;
    for(const wchar_t key:std::wstring(item.keys)){
      const bool handled=client.ProcessKeyEvent(weasel::KeyEvent(key,0));
      if(!read()){complete=false;break;}
      // Simulate only the application's ordinary fallback for an unhandled
      // fixed ASCII test key. This never sends input to an actual application.
      if(!handled)host+=key;
      client.ProcessKeyEvent(weasel::KeyEvent(key,ibus::RELEASE_MASK));
      if(!read()){complete=false;break;}
    }
    const bool ok=complete && host==item.text && !!status.composing==item.composing &&
        (!item.composing || context.preedit.str==(config.inline_preedit ? L"." : L".\u2038"));
    if(!ok)++failures;
    std::cout<<(ok ? "PASS " : "FAIL ")<<"private case "<<(&item-cases)
        <<"; composing="<<status.composing<<"; host UTF16=";
    for(wchar_t unit:host)std::cout<<std::hex<<static_cast<unsigned>(unit)<<' ';
    std::cout<<std::dec<<"; expected UTF16=";
    for(wchar_t unit:std::wstring(item.text))std::cout<<std::hex<<static_cast<unsigned>(unit)<<' ';
    std::cout<<std::dec<<"; preedit UTF16=";
    if(status.composing)for(wchar_t unit:context.preedit.str)std::cout<<std::hex<<static_cast<unsigned>(unit)<<' ';
    std::cout<<std::dec<<'\n';
  }
  std::cout<<"Private Rime contract cases=3 failures="<<failures<<'\n';
  return failures ? 1 : 0;
}
int wmain(int argc,wchar_t** argv) {
  if(argc==3 && !wcscmp(argv[1],L"--correction-settings")) {
    using namespace correction;
    std::cout<<"wire request="<<sizeof(BrokerRequest)<<" reply="<<sizeof(BrokerReply)<<" version="<<BrokerRequest{}.version<<'\n';
    for(DWORD budget:{20UL,500UL}) for(unsigned i=0;i<3;++i) {
      BrokerRequest request;BrokerReply reply;BrokerCall call;const auto start=GetTickCount64();
      const bool ok=call.Exchange(argv[2],request,reply,budget);
      std::cout<<"settings ok="<<ok<<" budget="<<budget<<" ms="<<GetTickCount64()-start;
      if(ok)std::cout<<" status="<<static_cast<DWORD>(reply.status)<<" enabled="<<reply.settings.enabled
          <<" advanced="<<reply.settings.advancedFlow<<" minimum="<<reply.settings.minimumVisibleMs<<" maximum="<<reply.settings.maximumAgeMs;
      std::cout<<'\n';
    }
    return 0;
  }
  if(argc==2 && !wcscmp(argv[1],L"--word-key-contract"))return WordKeyContract();
  if(argc!=1)return 2;
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
