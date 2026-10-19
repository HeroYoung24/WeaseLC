// Included in the synthetic host translation unit: it shares the same owned
// text store and never activates a TIP or sends global keyboard input.
#include <CorrectionAutomatic.h>
#include "../../WeaselDeployer/CorrectionPreferences.h"
#include <functional>
#include <fstream>
#include <CorrectionHintView.h>
#include <dbghelp.h>
#include "ContextAlias.h"
#pragma comment(lib, "dbghelp.lib")
static bool SaveOwnedHint(HWND window, const wchar_t* filename) {
  RECT rect{}; if (!GetClientRect(window, &rect) || IsRectEmpty(&rect)) return false;
  const int width = rect.right, height = rect.bottom;
  HDC reference = GetDC(window), dc = CreateCompatibleDC(reference);
  BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
  void* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(reference, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  bool saved = false;
  if (dc && bitmap) {
    const auto previous = SelectObject(dc, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader); header.bfSize = header.bfOffBits + width * height * 4;
    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char*>(pixels), width * height * 4); saved = out.good();
    SelectObject(dc, previous);
  }
  if (bitmap) DeleteObject(bitmap);
  if (dc) DeleteDC(dc);
  if (reference) ReleaseDC(window, reference);
  return saved;
}
// Invoked only as the isolated child's existing diagnostic argument. This
// worker accepts a compiled synthetic corpus and never opens a model service.
static int RunAutomaticFixtureWorker() {
  AutomaticRequest request; DWORD read = 0;
  if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &request, sizeof(request), &read, nullptr) ||
      read != sizeof(request) || !ValidAutomaticText(request.text, request.units, 1024)) return 2;
  const std::wstring text(request.text, request.units);
  if (text == L"模拟进程退出。") return 5;
  AutomaticReply reply; reply.count = 1; auto& item = reply.items[0]; item.score = .95;
  if (text == L"这里有错子。" || text == L"模拟慢响应。这里有错子。") {
    item.start = static_cast<unsigned>(text.size() - 3); item.originalUnits = item.replacementUnits = 2;
    wcscpy_s(item.original, L"错子"); wcscpy_s(item.replacement, L"错字");
  } else if (text == L"模拟慢响应。" || text == L"模拟坏响应。") {
    item.originalUnits = item.replacementUnits = 2;
    wcscpy_s(item.original, L"模拟"); wcscpy_s(item.replacement, L"示例");
    if (text == L"模拟慢响应。") Sleep(1000);
    else reply.magic = 0;
  } else if (text == L"这里有错" || text == L"子。") {
    reply.count = 0;
  } else if (text == L"模拟空坏响应。") {
    reply.count = 0; reply.magic = 0;
  } else return 3;
  DWORD written = 0;
  return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &reply, sizeof(reply), &written, nullptr) && written == sizeof(reply) ? 0 : 4;
}
static LONG CALLBACK AutomaticException(EXCEPTION_POINTERS* exception) {
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  std::cerr << "Synthetic host access violation at " << exception->ExceptionRecord->ExceptionAddress
      << " target " << reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]) << '\n';
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  void* frames[32]{};
  const auto count = CaptureStackBackTrace(0, 32, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i]), &displacement, symbol))
      std::cerr << symbol->Name << '+' << displacement << '\n';
    else {
      HMODULE module = nullptr; WCHAR path[MAX_PATH]{};
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(frames[i]), &module);
      GetModuleFileNameW(module, path, MAX_PATH);
      std::wcerr << path << L" + " << std::hex << (reinterpret_cast<ULONG_PTR>(frames[i]) - reinterpret_cast<ULONG_PTR>(module)) << std::dec << L'\n';
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

class AutomaticHostEdit final : public ITfEditSession {
 public:
  explicit AutomaticHostEdit(std::function<HRESULT(TfEditCookie)> body) : body_(std::move(body)) {}
  STDMETHODIMP QueryInterface(REFIID id, void** object) override {
    if (!object) return E_POINTER; *object = nullptr;
    if (id != IID_IUnknown && id != IID_ITfEditSession) return E_NOINTERFACE;
    *object = this; AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override { auto left = --refs_; if (!left) delete this; return left; }
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override { return body_(cookie); }
 private:
  ULONG refs_ = 1;
  std::function<HRESULT(TfEditCookie)> body_;
};
static bool RunAutomaticChecks(HMODULE module, const std::wstring& worker, bool local) {
  auto handler = AddVectoredExceptionHandler(1, AutomaticException);
  struct Cleanup { void* handler; ~Cleanup() { if (handler) RemoveVectoredExceptionHandler(handler); } } cleanup{handler};
  std::cout << std::unitbuf;
  using Configure = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, const Preferences*, TfClientId, const WCHAR*);
  using Inject = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, const AutomaticReply*);
  using Status = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, DWORD*, HWND*);
  using Key = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, ITfContext*, WPARAM, LPARAM, DWORD, BOOL*);
  using Environment = HRESULT(WINAPI*)(ITfTextInputProcessorEx*, BOOL, DWORD*);
  auto configure = reinterpret_cast<Configure>(GetProcAddress(module, "WeaselProbeAutomaticConfigure"));
  auto inject = reinterpret_cast<Inject>(GetProcAddress(module, "WeaselProbeAutomaticInject"));
  auto status = reinterpret_cast<Status>(GetProcAddress(module, "WeaselProbeAutomaticState"));
  auto key = reinterpret_cast<Key>(GetProcAddress(module, "WeaselProbeAutomaticKey"));
  auto poll = reinterpret_cast<Invalidate>(GetProcAddress(module, "WeaselProbeAutomaticPoll"));
  auto expire = reinterpret_cast<Invalidate>(GetProcAddress(module, "WeaselProbeAutomaticExpire"));
  auto environment = reinterpret_cast<Environment>(GetProcAddress(module, "WeaselProbeAutomaticEnvironment"));
  auto apply = reinterpret_cast<Apply>(GetProcAddress(module, "WeaselProbeApplySyntheticResponse"));
  auto preedit = reinterpret_cast<Apply>(GetProcAddress(module, "WeaselProbeAutomaticPreedit"));
  auto invalidate = reinterpret_cast<Invalidate>(GetProcAddress(module, "WeaselProbeInvalidateEditSessions"));
  auto get = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(module, "DllGetClassObject"));
  if (!configure || !inject || !status || !key || !poll || !expire || !environment || !apply || !preedit || !invalidate || !get) return false;
  const CLSID clsid{0xb749b45e,0x7dce,0x4496,{0x84,0x14,0x9e,0xe1,0x09,0x70,0x26,0x0a}};
  ComPtr<IClassFactory> factory;
  ComPtr<ITfTextInputProcessorEx> service;
  if (get(clsid, IID_IClassFactory, reinterpret_cast<void**>(factory.GetAddressOf())) != S_OK ||
      factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx, reinterpret_cast<void**>(service.GetAddressOf())) != S_OK) return false;
  ComPtr<ITfThreadMgrEx> manager;
  TfClientId client = TF_CLIENTID_NULL;
  if (CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgrEx,
      reinterpret_cast<void**>(manager.GetAddressOf())) != S_OK || manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP) != S_OK) return false;
  unsigned checks = 0, failed = 0;
  auto check = [&](bool good, const char* name) { ++checks; if (!good) ++failed; std::cout << (good ? "PASS " : "FAIL ") << name << '\n'; };
  Preferences preferences; preferences.mode = Mode::Local; preferences.automatic.enabled = true; preferences.timing = 1;
  AutomaticReply reply; reply.count = 1;
  auto& item = reply.items[0]; item.start = 3; item.originalUnits = 2; item.replacementUnits = 2; item.score = .95;
  wcscpy_s(item.original, L"错子"); wcscpy_s(item.replacement, L"错字");
  const HWND foreground = GetForegroundWindow();
  const InputScope fieldScopes[] = {IS_TEXT, IS_TEXT, IS_PASSWORD, IS_DEFAULT, IS_PRIVATE,
      IS_NUMERIC_PIN, IS_ALPHANUMERIC_PIN, IS_ALPHANUMERIC_PIN_SET};
  for (unsigned field = 0; field < (local ? 1U : _countof(fieldScopes)); ++field) {
    ComPtr<MemoryTextStore> store;
    store.Attach(new MemoryTextStore(local ? IS_DEFAULT : fieldScopes[field], field != 1));
    ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context;
    TfEditCookie cookie = 0;
    if (manager->CreateDocumentMgr(&document) != S_OK || document->CreateContext(client, 0,
        static_cast<ITextStoreACP*>(store.Get()), &context, &cookie) != S_OK || document->Push(context.Get()) != S_OK) {
      check(false, "own document created"); break;
    }
    auto edit = [&](std::function<HRESULT(TfEditCookie)> body) {
      ComPtr<ITfEditSession> session; session.Attach(new AutomaticHostEdit(std::move(body)));
      HRESULT result = E_FAIL;
      const auto request = context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &result);
      return request == S_OK && result == S_OK;
    };
    auto configureNow = [&]() { return configure(service.Get(), &preferences, client, local ? worker.c_str() : L"") == S_OK; };
    auto state = [&]() { DWORD ready = 0; HWND window = nullptr; status(service.Get(), &ready, &window); return std::make_pair(ready != 0, window); };
    auto noKey = [&]() { BOOL eaten = TRUE; return key(service.Get(), context.Get(), VK_TAB, 0, 1, &eaten) == S_FALSE && !eaten; };
    check(configureNow(), "automatic configuration bound to test client only");
    check(Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"), "production commit with automatic capture");
    if (field == 3) {
      check(inject(service.Get(),&reply)==S_OK && state().first,"explicit default scope supports local committed-text suggestions");
      check(edit([&](TfEditCookie ec){return preedit(service.Get(),context.Get(),ec,L"ni");}),"default-scope preedit starts beside correction target");
      BOOL eaten=FALSE;
      check(key(service.Get(),context.Get(),VK_TAB,0,1,&eaten)==S_OK && eaten && store->text==L"这里有错子。ni",
          "default-scope Tab query preserves original text");
      check(key(service.Get(),context.Get(),VK_TAB,0,0,&eaten)==S_OK && eaten && store->text==L"这里有错字。ni",
          "default-scope Tab corrects tracked text and preserves pinyin");
      key(service.Get(),context.Get(),VK_TAB,0,2,&eaten);
      check(Commit(apply,service.Get(),context.Get(),client,L"你好") && store->text==L"这里有错字。你好",
          "default-scope input continues normally after correction");
      configureNow();Commit(apply,service.Get(),context.Get(),client,L"这里有错子。");inject(service.Get(),&reply);
      ComPtr<ITfCompartmentMgr> compartments;context.As(&compartments);
      ComPtr<ITfCompartment> disabled,empty;
      const bool available=compartments && compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED,&disabled)==S_OK &&
          compartments->GetCompartment(GUID_COMPARTMENT_EMPTYCONTEXT,&empty)==S_OK && disabled && empty;
      VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=1;
      const bool marked=available && disabled->SetValue(client,&value)==S_OK;
      value.lVal=0;const bool emptyFalse=available && empty->SetValue(client,&value)==S_OK;
      check(marked && emptyFalse && noKey() && !state().first,"default scope still rejects disabled input even when empty-context flag is false");
      if(available)disabled->SetValue(client,&value);
      configureNow();Commit(apply,service.Get(),context.Get(),client,L"这里有错子。");inject(service.Get(),&reply);
      value.lVal=1;const bool noField=available && empty->SetValue(client,&value)==S_OK;
      check(noField && noKey() && !state().first,"default scope rejects an empty input context");
      value.lVal=0;if(available)empty->SetValue(client,&value);
      configureNow();Commit(apply,service.Get(),context.Get(),client,L"这里有错子。");inject(service.Get(),&reply);
      store->ChangeScope(IS_PRIVATE);
      check(noKey() && !state().first,"default-to-private transition cancels before accepting a suggestion");
      invalidate(service.Get());document->Pop(TF_POPF_ALL);continue;
    }
    if (field) {
      check(inject(service.Get(), &reply) == S_FALSE && noKey() && store->text == L"这里有错子。",
          "missing/unsupported/private/password/PIN field never shows, consumes Tab or changes text");
      invalidate(service.Get()); document->Pop(TF_POPF_ALL); continue;
    }
    if (local) {
      const auto start = GetTickCount64();
      while (!state().first && GetTickCount64() - start < 8000) {
        poll(service.Get()); MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(10);
      }
      check(state().first, "isolated worker / real local HTTP result ready");
      std::cout << "Automatic real model wall ms = " << GetTickCount64() - start << '\n';
    } else check(inject(service.Get(), &reply) == S_OK, "validated suggestion presented");
    auto current = state();
    check(current.first && current.second && IsWindowVisible(current.second), "nonactivating hint visible");
    check(current.second && (GetWindowLongPtrW(current.second, GWL_EXSTYLE) & WS_EX_NOACTIVATE) &&
        GetForegroundWindow() == foreground, "hint does not take foreground focus");
    check((GetWindowLongPtrW(current.second, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) ==
        (WS_EX_LAYERED | WS_EX_TRANSPARENT) && SendMessageW(current.second, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE,
        "nonactivating layered hint lets mouse clicks through");
    if (!local) check(SaveOwnedHint(current.second, L"output/CorrectionHintSample.bmp"), "own hint preview rendered without desktop capture");
    ComPtr<ITfKeyEventSink> keys; service.As(&keys);
    BOOL eaten = FALSE;
    keys->OnTestKeyDown(context.Get(), VK_TAB, 0, &eaten);
    check(eaten && store->text == L"这里有错子。", "test-key query never writes host text");
    keys->OnTestKeyDown(context.Get(), VK_TAB, 0, &eaten);
    check(eaten && store->text == L"这里有错子。", "repeated test-key query remains read only");
    keys->OnKeyDown(context.Get(), VK_TAB, 0, &eaten);
    check(eaten && store->text == L"这里有错字。", "production key sink accepts exactly once");
    check(!state().first && !IsWindowVisible(state().second), "accepted suggestion retired");
    check(key(service.Get(), context.Get(), VK_TAB, LPARAM(1) << 30, 0, &eaten) == S_OK && eaten &&
        store->text == L"这里有错字。", "held Tab cannot apply a second correction");
    keys->OnTestKeyUp(context.Get(), VK_TAB, 0, &eaten);
    check(eaten, "accepted Tab test key-up consumed");
    keys->OnKeyUp(context.Get(), VK_TAB, 0, &eaten);
    check(eaten && noKey(), "key-up releases Tab; empty hint preserves normal Tab");
    if (local) {
      const auto beforePaused = store->text;
      check(configureNow() && Commit(apply,service.Get(),context.Get(),client,L"这里有错"),
          "real model receives the compiled first fragment only");
      auto pumpUntil = [&](const std::function<bool()>& done) {
        const auto deadline = GetTickCount64()+8000;
        while(!done() && GetTickCount64()<deadline) {
          poll(service.Get());MSG message;
          while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
          Sleep(10);
        }
        return done();
      };
      DWORD child=0;bool seen=false;
      const bool completed=pumpUntil([&]{environment(service.Get(),TRUE,&child);seen=seen||child!=0;return seen&&!child;});
      check(completed && !state().first && noKey(),"real model first fragment finishes with no visible suggestion");
      bool restarted=false;const auto idle=GetTickCount64()+350;
      while(GetTickCount64()<idle){poll(service.Get());environment(service.Get(),TRUE,&child);restarted=restarted||child!=0;Sleep(10);}
      check(!restarted,"completed real no-change check does not repeat while idle");
      check(Commit(apply,service.Get(),context.Get(),client,L"子。") && pumpUntil([&]{return state().first;}),
          "real model checks merged context across a completed no-change response");
      check(edit([&](TfEditCookie ec){return preedit(service.Get(),context.Get(),ec,L"ni");}),
          "real model result coexists with the next active preedit");
      eaten=FALSE;
      if(state().first){keys->OnTestKeyDown(context.Get(),VK_TAB,0,&eaten);keys->OnKeyDown(context.Get(),VK_TAB,0,&eaten);}
      check(eaten && store->text==beforePaused+L"这里有错字。ni","real model Tab corrects the prior fragment and preserves pinyin");
      if(eaten)keys->OnKeyUp(context.Get(),VK_TAB,0,&eaten);
      check(Commit(apply,service.Get(),context.Get(),client,L"你好") && store->text==beforePaused+L"这里有错字。你好",
          "input still commits after the real model correction");
      invalidate(service.Get()); document->Pop(TF_POPF_ALL); continue;
    }

    preferences.timing = 0;
    for (const std::wstring sentence : {L"这里有错子.", L"这里有错子"}) {
      const auto beforeVariant = store->text;
      auto corrected = sentence; corrected.replace(3, 2, L"错字");
      check(configureNow() && Commit(apply, service.Get(), context.Get(), client, sentence) &&
          inject(service.Get(), &reply) == S_OK && !IsWindowVisible(state().second),
          "ASCII full stop or no punctuation still waits for the next composition");
      check(edit([&](TfEditCookie ec) { return preedit(service.Get(), context.Get(), ec, L"ni"); }) &&
          IsWindowVisible(state().second), "punctuation variant shows beside the next preedit");
      keys->OnTestKeyDown(context.Get(), VK_TAB, 0, &eaten);
      keys->OnKeyDown(context.Get(), VK_TAB, 0, &eaten);
      check(eaten && store->text == beforeVariant + corrected + L"ni",
          "Tab corrects punctuation variant and preserves pinyin");
      keys->OnKeyUp(context.Get(), VK_TAB, 0, &eaten);
      check(Commit(apply, service.Get(), context.Get(), client, L"你好") &&
          store->text == beforeVariant + corrected + L"你好", "input continues after punctuation variant correction");
    }
    const auto prefix = store->text;
    preferences.timing = 0;
    check(configureNow() && Commit(apply, service.Get(), context.Get(), client, L"这里有错子。") &&
        inject(service.Get(), &reply) == S_OK && !IsWindowVisible(state().second), "next-input mode waits for composition");
    check(edit([&](TfEditCookie ec) { return preedit(service.Get(), context.Get(), ec, L"ni"); }) &&
        IsWindowVisible(state().second), "next composition reveals adjacent hint");
    check(key(service.Get(), context.Get(), VK_TAB, 0, 1, &eaten) == S_OK && eaten, "active preedit Tab query valid");
    const HRESULT activeKey = key(service.Get(), context.Get(), VK_TAB, 0, 0, &eaten);
    std::cout << "Active preedit accept = " << std::hex << activeKey << std::dec << " eaten " << eaten << '\n';
    check(eaten && store->text == prefix + L"这里有错字。ni", "correction preserves active preedit");
    key(service.Get(), context.Get(), VK_TAB, 0, 2, &eaten);
    check(Commit(apply, service.Get(), context.Get(), client, L"你好"), "preedit still commits normally after correction");
    check(store->text == prefix + L"这里有错字。你好", "Rime commit replaces preedit at tracked position");

    const auto beforeShorter = store->text;
    configureNow(); Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
    auto shorter = reply; shorter.items[0] = {}; auto& shortItem = shorter.items[0];
    shortItem.start = 0; shortItem.originalUnits = 2; shortItem.replacementUnits = 1; shortItem.score = .95;
    wcscpy_s(shortItem.original, L"这里"); wcscpy_s(shortItem.replacement, L"这");
    inject(service.Get(), &shorter);
    edit([&](TfEditCookie ec) { return preedit(service.Get(), context.Get(), ec, L"ni"); });
    keys->OnTestKeyDown(context.Get(), VK_TAB, 0, &eaten); keys->OnKeyDown(context.Get(), VK_TAB, 0, &eaten);
    check(eaten && store->text == beforeShorter + L"这有错子。ni", "shorter replacement preserves preedit range");
    keys->OnKeyUp(context.Get(), VK_TAB, 0, &eaten);
    Commit(apply, service.Get(), context.Get(), client, L"你好");
    check(store->text == beforeShorter + L"这有错子。你好", "commit remains aligned after length change");

    preferences.timing = 1; configureNow();
    const auto beforeFragments = store->text;
    check(Commit(apply, service.Get(), context.Get(), client, L"这里有") &&
        Commit(apply, service.Get(), context.Get(), client, L"错子。") && inject(service.Get(), &reply) == S_OK,
        "adjacent commits form one bounded model context");
    keys->OnTestKeyDown(context.Get(), VK_TAB, 0, &eaten); keys->OnKeyDown(context.Get(), VK_TAB, 0, &eaten);
    check(eaten && store->text == beforeFragments + L"这里有错字。", "merged context uses correct local offsets");
    keys->OnKeyUp(context.Get(), VK_TAB, 0, &eaten);
    configureNow();
    check(Commit(apply, service.Get(), context.Get(), client, std::wstring(1025, L'甲')) &&
        inject(service.Get(), &reply) == S_FALSE && noKey(), "oversized commit stays in host but is never sent to model");

    preferences.timing = 1; configureNow();
    Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"); inject(service.Get(), &reply);
    const auto beforeRefusal = store->text;
    store->refuseWrites = true;
    check(key(service.Get(), context.Get(), VK_TAB, 0, 0, &eaten) == S_FALSE && !eaten &&
        store->text == beforeRefusal, "refused synchronous write leaves text and Tab alone");
    store->refuseWrites = false;
    poll(service.Get());
    check(store->text == beforeRefusal && !state().first, "refused write never queues a delayed edit");
    configureNow(); Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"); inject(service.Get(), &reply);
    check(key(service.Get(), context.Get(), VK_ESCAPE, 0, 1, &eaten) == S_FALSE && !state().first && noKey(),
        "Escape dismisses suggestion and remains available to Rime");
    configureNow(); Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"); inject(service.Get(), &reply);
    expire(service.Get());
    check(noKey() && !state().first, "expired hint cannot accept Tab before timer dispatch");

#include "InterruptionChecks.inc"

    // Move caret under a real write cookie, then verify no delayed replacement.
    preferences.timing = 1; configureNow();
    Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"); inject(service.Get(), &reply);
    check(edit([&](TfEditCookie ec) {
      ComPtr<ITfRange> start; if (context->GetStart(ec, &start) != S_OK) return E_FAIL;
      TF_SELECTION selection{start.Get(), {TF_AE_NONE, FALSE}}; return context->SetSelection(ec, 1, &selection);
    }), "host caret moved");
    const auto before = store->text;
    check(noKey() && store->text == before && !state().first, "caret move invalidates before Tab claim");
    configureNow(); Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"); inject(service.Get(), &reply);
    check(edit([&](TfEditCookie ec) {
      ComPtr<ITfRange> start; if (context->GetStart(ec, &start) != S_OK) return E_FAIL;
      LONG shifted = 0; start->ShiftEnd(ec, 1, &shifted, nullptr); return start->SetText(ec, 0, L"X", 1);
    }), "host original text changed");
    const auto modified = store->text;
    check(noKey() && store->text == modified, "stale original cannot be overwritten");
    configureNow(); Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
    auto malformed = reply; malformed.items[0].start = 999;
    check(inject(service.Get(), &malformed) == S_FALSE && noKey(), "bad worker offsets rejected");
    auto nonfinite = reply; nonfinite.items[0].score = std::numeric_limits<double>::quiet_NaN();
    check(inject(service.Get(), &nonfinite) == S_FALSE, "nonfinite worker score rejected");
    invalidate(service.Get());
    check(inject(service.Get(), &reply) == S_FALSE && noKey(), "late reply after lifecycle cancellation rejected");
    check(store->UserSelect(static_cast<LONG>(store->text.size()),static_cast<LONG>(store->text.size()))==S_OK,
        "alias acceptance fixture starts at document end");
    ComPtr<ITfContext> contextAlias; contextAlias.Attach(new OwnedContextAlias(context.Get()));
    ComPtr<IUnknown> nativeIdentity, aliasIdentity;
    context.As(&nativeIdentity); contextAlias.As(&aliasIdentity);
    check(contextAlias.Get()!=context.Get() && nativeIdentity==aliasIdentity,
        "real TSF context exposes distinct interface address with identical IUnknown");
    configureNow(); const auto beforeAlias=store->text;
    check(Commit(apply,service.Get(),context.Get(),client,L"这里有错子。") &&
        edit([&](TfEditCookie ec){return preedit(service.Get(),context.Get(),ec,L"ni");}) && inject(service.Get(),&reply)==S_OK,
        "native context captures a valid correction before alias key delivery");
    check(key(service.Get(),contextAlias.Get(),VK_TAB,0,1,&eaten)==S_OK && eaten && store->text==beforeAlias+L"这里有错子。ni",
        "alias Tab query recognizes same document without writing");
    check(key(service.Get(),contextAlias.Get(),VK_TAB,0,0,&eaten)==S_OK && eaten && store->text==beforeAlias+L"这里有错字。ni",
        "alias Tab acceptance corrects text while preserving active preedit");
    key(service.Get(),contextAlias.Get(),VK_TAB,0,2,&eaten);
    check(Commit(apply,service.Get(),context.Get(),client,L"你好") && store->text==beforeAlias+L"这里有错字。你好",
        "native context can commit after correction through its alias");
    contextAlias.Reset();nativeIdentity.Reset();aliasIdentity.Reset();

    preferences.automatic.tab = false; configureNow();
    Commit(apply, service.Get(), context.Get(), client, L"这里有错子。"); inject(service.Get(), &reply);
    check(state().first && noKey(), "Tab setting disables shortcut without hiding suggestion");
    preferences.automatic.enabled = false; configureNow();
    Commit(apply, service.Get(), context.Get(), client, L"这里有错子。");
    check(inject(service.Get(), &reply) == S_FALSE && noKey(), "default/off preference never captures for automatic requests");
    preferences.automatic.enabled = true; preferences.automatic.tab = true;
    invalidate(service.Get()); document->Pop(TF_POPF_ALL);
  }
  const RECT work{-1920, 0, 0, 1080};
  if (!local) {
    ComPtr<ITfTextInputProcessorEx> temporary;
    ComPtr<ITfKeyEventSink> temporaryKeys;
    ComPtr<MemoryTextStore> host; host.Attach(new MemoryTextStore());
    ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context; TfEditCookie cookie = 0;
    const bool made = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
        reinterpret_cast<void**>(temporary.GetAddressOf())) == S_OK &&
        manager->CreateDocumentMgr(&document) == S_OK && document->CreateContext(client, 0,
            static_cast<ITextStoreACP*>(host.Get()), &context, &cookie) == S_OK && document->Push(context.Get()) == S_OK;
    check(made, "reentrant release fixture uses a separate owned service/document");
    if (made) {
      preferences.automatic.enabled = true; preferences.automatic.tab = true; preferences.timing = 1;
      configure(temporary.Get(), &preferences, client, L""); temporary.As(&temporaryKeys);
      Commit(apply, temporary.Get(), context.Get(), client, L"这里有错子。"); inject(temporary.Get(), &reply);
      DWORD ready = 0; HWND hint = nullptr; status(temporary.Get(), &ready, &hint);
      auto* raw = temporaryKeys.Get();
      host->onGetSelection = [&]() { temporaryKeys.Reset(); temporary.Reset(); };
      BOOL consumed = FALSE;
      raw->OnKeyDown(context.Get(), VK_TAB, 0, &consumed);
      check(consumed && host->text == L"这里有错字。" && !temporary && !temporaryKeys,
          "key callback survives host releasing external service references during edit");
      check(!IsWindow(hint), "last-reference callback destroys its hint and leaves no timer owner");
      document->Pop(TF_POPF_ALL);
    }
  }
  for (unsigned side = 0; side < 3; ++side) {
    const auto placed = PlaceAutomaticHint(RECT{-10, 1060, 0, 1080}, work, SIZE{500, 120}, side, 8);
    check(placed.left >= work.left && placed.top >= work.top && placed.right <= work.right && placed.bottom <= work.bottom,
        "layout stays in negative-coordinate monitor work area");
  }
  invalidate(service.Get()); service.Reset();
  check(manager->Deactivate() == S_OK, "independent host deactivated without TIP activation");
  std::cout << "Automatic checks = " << checks << ", failures = " << failed << '\n';
  return !failed;
}
