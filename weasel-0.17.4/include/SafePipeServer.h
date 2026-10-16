#pragma once
#include <PipeChannel.h>
#include <PipeSecurity.h>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <algorithm>

namespace weasel {
// Each connected client owns its entire request/response buffer. The owner
// must stop/join the listener before JoinWorkers and before destroying handlers.
template <typename Message>
class SafePipeServer : public PipeChannel<DWORD, Message> {
 public:
  using Base = PipeChannel<DWORD, Message>;
  class Session : public Base {
   public:
    explicit Session(HANDLE pipe) : Base(L"") { this->hpipe = pipe; }
    Message Read() {
      Message msg{}; body_.clear();
      this->_Receive(this->hpipe, &msg, sizeof(msg));
      this->TakeResponseData(std::function<bool(LPWSTR, UINT)>([this](LPWSTR data, UINT units) {
        const auto end = std::find(data, data + units, L'\0');
        if (end == data + units) throw DWORD(ERROR_INVALID_DATA);
        body_.assign(data, end); return true;
      }));
      return msg;
    }
    LPWSTR Body() { return body_.data(); }
    void Send(DWORD reply) { this->_Send(this->hpipe, reply); }
   private:
    std::wstring body_;
  };
  using Respond = std::function<void(DWORD)>;
  using ServerHandler = std::function<void(Message, Respond, Session&)>;
  SafePipeServer(std::wstring name, PipeAccessPolicy& access)
      : Base(std::move(name), access.get_attr()), access_(access) {}
  ~SafePipeServer() { RequestStop(); JoinWorkers(); }
  void RequestStop() { stopping_.store(true); }
  // CancelSynchronousIo can race with an operation being issued. Repeat until
  // the thread exits; do not close/reuse its pipe while an IO is still active.
  static void CancelUntilExited(HANDLE thread) {
    while (WaitForSingleObject(thread, 0) == WAIT_TIMEOUT) {
      CancelSynchronousIo(thread);
      // The server UI thread may be draining a worker that is completing a
      // synchronous window update. Dispatch sent messages without consuming
      // posted commands/WM_QUIT, so shutdown cannot deadlock on that update.
      if (MsgWaitForMultipleObjectsEx(1, &thread, 20, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0 + 1) {
        MSG message{}; PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
      }
    }
  }
  void JoinWorkers() {
    for (auto& worker : workers_) {
      if (worker->thread.joinable()) {
        CancelUntilExited(worker->thread.native_handle());
        worker->thread.join();
      }
    }
    workers_.clear();
  }
  void Listen(ServerHandler handler) {
    while (!stopping_.load()) {
      HANDLE pipe = INVALID_HANDLE_VALUE;
      try {
        pipe = this->_ConnectServerPipe(this->pname);
        for (auto it = workers_.begin(); it != workers_.end();) {
          if ((*it)->done.load() || !(*it)->thread.joinable()) {
            if ((*it)->thread.joinable()) (*it)->thread.join();
            it = workers_.erase(it);
          }
          else ++it;
        }
        if (stopping_.load() || workers_.size() >= 64) { this->_FinalizePipe(pipe); continue; }
        auto worker = std::make_unique<Worker>();
        auto* state = worker.get();
        // Allocate the container slot before starting a thread so allocation
        // failure cannot destroy a live joinable thread.
        workers_.push_back(std::move(worker));
        state->thread = std::thread([this, state, pipe, handler]() {
          bool adopted = false;
          try {
            Session session(pipe);
            adopted = true;
              while (!stopping_.load()) {
                const auto message = session.Read();
                if (stopping_.load() || !access_.AllowsClient(pipe)) break;
                DWORD result = 0; bool reply = false;
                handler(message, [&](DWORD value) { result = value; reply = true; }, session);
                // The Rime handler holds a global API mutex. A stalled client
                // must never keep it locked while a pipe write/flush blocks.
                if (reply && !stopping_.load()) session.Send(result);
              }
          } catch (...) { if (!adopted) { auto abandoned = pipe; this->_FinalizePipe(abandoned); } }
          state->done.store(true);
        });
        pipe = INVALID_HANDLE_VALUE;
      } catch (...) {
        this->_FinalizePipe(pipe);
        if (!stopping_.load()) Sleep(20);
      }
    }
  }
 private:
  struct Worker { std::thread thread; std::atomic_bool done{false}; };
  PipeAccessPolicy& access_;
  std::atomic_bool stopping_{false};
  std::vector<std::unique_ptr<Worker>> workers_;
};
}  // namespace weasel
