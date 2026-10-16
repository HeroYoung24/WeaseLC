#include <windows.h>
#include <authz.h>
#include <PipeSecurity.h>
#include <PipeChannel.h>
#include <CorrectionDeployment.h>
#include <CorrectionAutomatic.h>
#include <thread>
#include <iostream>
#include <atomic>
#include <SafePipeServer.h>
#include <mutex>

unsigned checks = 0, failures = 0;
void Check(bool value, const char* name) {
  ++checks;
  if (!value) { ++failures; std::cerr << "FAIL " << name << " error=" << GetLastError() << '\n'; }
}
class OwnedChannel : public weasel::PipeChannelBase {
 public:
  OwnedChannel(std::wstring name, SECURITY_ATTRIBUTES* sa = nullptr) : PipeChannelBase(std::move(name), 4096, sa) {}
  HANDLE Listen() { return _ConnectServerPipe(pname); }
  bool Connect() { return _Ensure(); }
  HANDLE Handle() const { return hpipe; }
};
bool SidAccess(PSECURITY_DESCRIPTOR descriptor, PSID sid, DWORD access) {
  AUTHZ_RESOURCE_MANAGER_HANDLE manager = nullptr;
  if (!AuthzInitializeResourceManager(AUTHZ_RM_FLAG_NO_AUDIT, nullptr, nullptr, nullptr, nullptr, &manager)) return false;
  AUTHZ_CLIENT_CONTEXT_HANDLE context = nullptr;
  LUID id{};
  DWORD granted = 0, error = 0;
  AUTHZ_ACCESS_REQUEST request{}; request.DesiredAccess = access;
  AUTHZ_ACCESS_REPLY reply{}; reply.ResultListLength = 1; reply.GrantedAccessMask = &granted; reply.Error = &error;
  // Authz needs owner/group fields, which the kernel fills during object
  // creation. Reparse the identical DACL with explicit fixture owner/group.
  LPWSTR sddl = nullptr; PSECURITY_DESCRIPTOR complete = nullptr;
  if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1,
      DACL_SECURITY_INFORMATION, &sddl, nullptr)) { AuthzFreeResourceManager(manager); return false; }
  const auto full = L"O:SYG:SY" + std::wstring(sddl); LocalFree(sddl);
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(full.c_str(), SDDL_REVISION_1, &complete, nullptr)) {
    AuthzFreeResourceManager(manager); return false;
  }
  const bool initialized = AuthzInitializeContextFromSid(AUTHZ_SKIP_TOKEN_GROUPS, sid, manager, nullptr, id, nullptr, &context) != FALSE;
  const bool evaluated = initialized && AuthzAccessCheck(0, context, &request, nullptr, complete, nullptr, 0, &reply, nullptr);
  if (!initialized || !evaluated) std::cerr << "Authz init=" << initialized << " eval=" << evaluated << " error=" << GetLastError() << '\n';
  const bool okay = evaluated && !error && (granted & access) == access;
  if (context) AuthzFreeContext(context);
  LocalFree(complete);
  AuthzFreeResourceManager(manager);
  return okay;
}
int main() try {
  using namespace correction;
  Check(CorrectionWorkerPath(L"C:\\Windows\\System32\\weasel.dll", L"C:\\Windows", L"C:\\Program Files\\WeaseLC") ==
      L"C:\\Program Files\\WeaseLC\\WeaselDeployer.exe", "installed x64 worker uses machine root");
  Check(CorrectionWorkerPath(L"C:\\WINDOWS\\SysWOW64\\weasel.dll", L"c:\\windows", L"D:\\测试 目录\\") ==
      L"D:\\测试 目录\\WeaselDeployer.exe", "installed x86 worker and unicode root");
  Check(CorrectionWorkerPath(L"E:\\build\\weaselx64.dll", L"C:\\Windows", L"C:\\daily") ==
      L"E:\\build\\WeaselDeployer.exe", "build does not fall back to daily installation");
  Check(CorrectionWorkerPath(L"C:\\Windows\\System32\\weasel.dll", L"C:\\Windows", L"").empty(), "missing installation fails closed");
  bool rejected = true;
  for (const auto* path : {L"relative", L"C:relative", L"\\\\server\\share", L"C:\\bad\"arg", L"C:\\bad\\..\\root",
      L"C:\\bad\\.\\root", L"C:\\bad:stream", L"C:\\bad\npath", L"C:\\bad.\\root", L"C:\\bad \\root"})
    rejected = rejected && CorrectionWorkerPath(L"C:\\Windows\\System32\\weasel.dll", L"C:\\Windows", path).empty();
  Check(rejected, "untrusted installation paths rejected");
  weasel::PipeAccessPolicy policy;
  auto* attributes = policy.get_attr();
  Check(attributes->nLength == sizeof(SECURITY_ATTRIBUTES) && !attributes->bInheritHandle &&
      attributes->lpSecurityDescriptor && IsValidSecurityDescriptor(attributes->lpSecurityDescriptor), "initialized non-inherited descriptor");
  HANDLE token = nullptr;
  Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) && policy.MatchesToken(token), "current logon authenticated");
  const auto logon = weasel::TokenLogonSid(token); CloseHandle(token);
  Check(!policy.MatchesToken(nullptr) && !policy.AllowsClient(INVALID_HANDLE_VALUE), "invalid identity fails closed");
  Check(SidAccess(attributes->lpSecurityDescriptor, const_cast<BYTE*>(logon.data()), weasel::kPipeClientAccess), "logon ACL allows client IO");
  for (auto type : {WinWorldSid, WinAnonymousSid, WinNetworkSid, WinAuthenticatedUserSid}) {
    BYTE sid[SECURITY_MAX_SID_SIZE]{}; DWORD size = sizeof(sid);
    Check(CreateWellKnownSid(type, nullptr, sid, &size) &&
        !SidAccess(attributes->lpSecurityDescriptor, sid, weasel::kPipeClientAccess), "unrelated SID lacks pipe IO");
  }
  BYTE packages[SECURITY_MAX_SID_SIZE]{}; DWORD size = sizeof(packages);
  Check(CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, packages, &size) &&
      SidAccess(attributes->lpSecurityDescriptor, packages, weasel::kPipeClientAccess) &&
      !SidAccess(attributes->lpSecurityDescriptor, packages, FILE_CREATE_PIPE_INSTANCE), "package grant excludes server creation");
  Check(!(weasel::kPipeClientAccess & FILE_CREATE_PIPE_INSTANCE), "client never requests server creation rights");
  for (bool anonymous : {false, true}) {
    const auto name = L"\\\\.\\pipe\\WeaseLC.Security." + std::to_wstring(GetCurrentProcessId()) + (anonymous ? L".anon" : L".id");
    OwnedChannel server(name, attributes);
    std::atomic<bool> valid{false}, reverted{false}, identification{false};
    std::thread serve([&]() {
      HANDLE pipe = INVALID_HANDLE_VALUE;
      try {
        pipe = server.Listen();
        DWORD message = 0, read = 0;
        if (ReadFile(pipe, &message, sizeof(message), &read, nullptr) && read == sizeof(message)) {
          valid = policy.AllowsClient(pipe);
          HANDLE current = nullptr;
          reverted = !OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &current) && GetLastError() == ERROR_NO_TOKEN;
          if (current) CloseHandle(current);
          if (!anonymous && ImpersonateNamedPipeClient(pipe)) {
            if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &current)) {
              SECURITY_IMPERSONATION_LEVEL level{}; DWORD bytes = 0;
              identification = GetTokenInformation(current, TokenImpersonationLevel, &level, sizeof(level), &bytes) && level == SecurityIdentification;
              CloseHandle(current);
            }
            if (!RevertToSelf()) RaiseFailFastException(nullptr, nullptr, 0);
          }
        }
      } catch (...) {}
      if (pipe != INVALID_HANDLE_VALUE) { DisconnectNamedPipe(pipe); CloseHandle(pipe); }
    });
    HANDLE client = INVALID_HANDLE_VALUE;
    const auto deadline = GetTickCount64() + 3000;
    std::unique_ptr<OwnedChannel> channel;
    do {
      if (anonymous) client = CreateFileW(name.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING,
          SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
      else {
        channel = std::make_unique<OwnedChannel>(name);
        if (channel->Connect()) client = channel->Handle();
      }
      if (client == INVALID_HANDLE_VALUE) Sleep(10);
    } while (client == INVALID_HANDLE_VALUE && GetTickCount64() < deadline);
    DWORD written = 0, message = 0x1234;
    const bool sent = client != INVALID_HANDLE_VALUE && WriteFile(client, &message, sizeof(message), &written, nullptr) && written == sizeof(message);
    if (!sent) CancelSynchronousIo(serve.native_handle());
    serve.join();
    if (anonymous && client != INVALID_HANDLE_VALUE) CloseHandle(client);
    Check(sent && (anonymous ? !valid.load() : valid.load()), anonymous ? "anonymous real client rejected" : "production client real pipe authenticated");
    Check(reverted.load(), "client authentication always restores thread identity");
    if (!anonymous) Check(identification.load(), "production client limits rogue-server impersonation");
  }
  const auto busyName = L"\\\\.\\pipe\\WeaseLC.Security.Busy." + std::to_wstring(GetCurrentProcessId());
  HANDLE busy = CreateNamedPipeW(busyName.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
      1, 4096, 4096, 0, attributes);
  HANDLE first = CreateFileW(busyName.c_str(), weasel::kPipeClientAccess, 0, nullptr, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  OwnedChannel blocked(busyName);
  const auto began = GetTickCount64();
  Check(busy != INVALID_HANDLE_VALUE && first != INVALID_HANDLE_VALUE && !blocked.Connect() &&
      GetTickCount64() - began < 2300, "busy pipe connection deadline");
  if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
  if (busy != INVALID_HANDLE_VALUE) CloseHandle(busy);
  {
    const auto name = L"\\\\.\\pipe\\WeaseLC.Security.Concurrent." + std::to_wstring(GetCurrentProcessId());
    weasel::SafePipeServer<DWORD> server(name, policy);
    std::atomic<unsigned> mixed{0}, handled{0};
    std::atomic<bool> go{false}, blockedReply{false}; std::mutex api;
    std::thread listener([&]() {
      server.Listen([&](DWORD id, const auto& reply, auto& session) {
        std::lock_guard guard(api);  // production Rime handler serialization
        if (id == 999) { blockedReply = session.Body()[0] == L'\0'; reply(id); return; }
        if (id == 1000) { reply(id); return; }
        const auto expected = L"session.client_app=synthetic-" + std::to_wstring(id);
        if (std::wstring(session.Body()) != expected) ++mixed;
        session << expected << L"\n"; ++handled; reply(id);
      });
    });
    std::vector<std::thread> clients;
    std::atomic<unsigned> complete{0};
    for (DWORD i = 1; i <= 16; ++i) clients.emplace_back([&, i]() {
      while (!go.load()) Sleep(1);
      weasel::PipeChannel<DWORD> client{std::wstring(name)};
      const auto until = GetTickCount64() + 4000;
      while (!client.Connect() && GetTickCount64() < until) Sleep(10);
      try {
        for (unsigned round = 0; round < 20; ++round) {
          const auto expected = L"session.client_app=synthetic-" + std::to_wstring(i);
          client << expected; DWORD id = i;
          const auto result = client.Transact(id);
          bool same = false;
          client.TakeResponseData(std::function<bool(LPWSTR, UINT)>([&](LPWSTR text, UINT units) {
            same = units >= expected.size() + 1 && std::wstring(text, expected.size()) == expected; return true;
          }));
          if (result != i || !same) { ++mixed; break; }
          ++complete;
        }
      } catch (...) { ++mixed; }
    });
    go = true;
    for (auto& client : clients) client.join();
    Check(!mixed.load() && complete == 320 && handled == 320, "concurrent clients preserve complete metadata and private response buffers");
    {
      OwnedChannel malformed(name); bool connected = malformed.Connect();
      const DWORD payload[] = {777, 0x00410041}; DWORD written = 0, received = 0, reply = 0;
      const bool sent = connected && WriteFile(malformed.Handle(), payload, sizeof(payload), &written, nullptr);
      const bool closed = sent && !ReadFile(malformed.Handle(), &reply, sizeof(reply), &received, nullptr);
      Check(closed && handled == 320, "unterminated metadata closes connection before handler access");
    }
    OwnedChannel stalled(name); DWORD sent = 0, command = 999;
    const bool issued = stalled.Connect() && WriteFile(stalled.Handle(), &command, sizeof(command), &sent, nullptr);
    const auto untilReply = GetTickCount64() + 1000;
    while (!blockedReply.load() && GetTickCount64() < untilReply) Sleep(5);
    Check(issued && blockedReply.load(), "scalar message clears old metadata and blocked reply is prepared");
    Sleep(20);
    std::atomic<bool> unaffected{false};
    std::thread healthy([&]() {
      try { weasel::PipeChannel<DWORD> client{std::wstring(name)};
        DWORD id=1000; unaffected=client.Connect() && client.Transact(id)==id;
      } catch (...) {}
    });
    if(WaitForSingleObject(healthy.native_handle(),1000)==WAIT_TIMEOUT)
      weasel::SafePipeServer<DWORD>::CancelUntilExited(healthy.native_handle());
    healthy.join();
    Check(unaffected.load(), "stalled response cannot hold the Rime API mutex or block other clients");
    // An idle client never sends data; shutdown must cancel its blocked read.
    OwnedChannel idle(name); Check(idle.Connect(), "idle connection prepared");
    const auto stopBegan = GetTickCount64();
    server.RequestStop();
    weasel::SafePipeServer<DWORD>::CancelUntilExited(listener.native_handle()); listener.join();
    server.JoinWorkers();
    Check(GetTickCount64() - stopBegan < 1500, "listener, idle read and blocked reply drain before handler destruction");
  }
  DWORD before = 0, after = 0; GetProcessHandleCount(GetCurrentProcess(), &before);
  {
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WeaseLC.Security.Drain";
    wc.lpfnWndProc=[](HWND window, UINT message, WPARAM w, LPARAM l)->LRESULT {
      if(message==WM_APP+1){++*reinterpret_cast<unsigned*>(w);return 1;}
      return DefWindowProcW(window,message,w,l);
    };
    RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    unsigned received=0;std::atomic<bool> sent{false};
    std::thread update([&](){DWORD_PTR result=0;sent=SendMessageTimeoutW(window,WM_APP+1,reinterpret_cast<WPARAM>(&received),0,SMTO_ABORTIFHUNG,1000,&result)!=0 && result==1;});
    weasel::SafePipeServer<DWORD>::CancelUntilExited(update.native_handle());update.join();
    Check(window && sent && received==1,"UI-thread drain completes owned synchronous window callbacks");
    if(window)DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
  }
  GetProcessHandleCount(GetCurrentProcess(), &before);
  for (unsigned i = 0; i < 200; ++i) { weasel::PipeAccessPolicy transient; }
  GetProcessHandleCount(GetCurrentProcess(), &after);
  Check(before == after, "policy lifetime releases token handles and descriptors");
  std::cout << "Security checks = " << checks << ", failures = " << failures << '\n';
  return failures ? 1 : 0;
} catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; return 2; }
