#include "stdafx.h"

#include <PipeChannel.h>
#include <PipeSecurity.h>

using namespace weasel;
using namespace std;
using namespace boost;

#define _ThrowLastError throw ::GetLastError()
#define _ThrowCode(__c) throw __c
#define _ThrowIfNot(__c)                 \
  {                                      \
    DWORD err;                           \
    if ((err = ::GetLastError()) != __c) \
      throw err;                         \
  }

PipeChannelBase::PipeChannelBase(std::wstring&& pn_cmd,
                                 size_t bs = 4 * 1024,
                                 SECURITY_ATTRIBUTES* s = NULL)
    : pname(pn_cmd),
      write_stream(nullptr),
      buff_size(bs),
      buffer(std::make_unique<char[]>(bs)),
      hpipe(INVALID_HANDLE_VALUE),
      has_body(false),
      sa(s) {};

PipeChannelBase::PipeChannelBase(PipeChannelBase&& r)
    : write_stream(std::move(r.write_stream)),
      pname(std::move(r.pname)),
      buff_size(r.buff_size),
      buffer(std::move(r.buffer)),
      hpipe(r.hpipe),
      has_body(r.has_body),
      sa(r.sa) {};

PipeChannelBase::~PipeChannelBase() {
  _FinalizePipe(hpipe);
}

bool PipeChannelBase::_Ensure() {
  try {
    if (_Invalid(hpipe)) {
      hpipe = _Connect(pname.c_str());
      return !_Invalid(hpipe);
    }
  } catch (...) {
    return false;
  }

  return true;
}

HANDLE PipeChannelBase::_Connect(const wchar_t* name) {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  const auto deadline = GetTickCount64() + 1500;
  while (_Invalid(pipe = _TryConnect())) {
    if (GetTickCount64() >= deadline) throw DWORD(ERROR_SEM_TIMEOUT);
    ::WaitNamedPipe(name, 100);
  }
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
    const DWORD error = GetLastError(); CloseHandle(pipe); throw error;
  }
  return pipe;
}

void PipeChannelBase::_Reconnect() {
  _FinalizePipe(hpipe);
  _Ensure();
}

HANDLE PipeChannelBase::_TryConnect() {
  auto pipe = ::CreateFile(pname.c_str(), kPipeClientAccess, 0, NULL,
                           OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
  if (!_Invalid(pipe)) {
    // connected to the pipe
    return pipe;
  }
  // being busy is not really an error since we just need to wait.
  _ThrowIfNot(ERROR_PIPE_BUSY);
  // All pipe instances are busy
  return INVALID_HANDLE_VALUE;
}

size_t PipeChannelBase::_WritePipe(HANDLE pipe, size_t s, char* b) {
  DWORD lwritten;
  if (!::WriteFile(pipe, b, s, &lwritten, NULL) || lwritten <= 0) {
    _ThrowLastError;
  }
  ::FlushFileBuffers(pipe);
  return lwritten;
}

void PipeChannelBase::_FinalizePipe(HANDLE& p) {
  if (!_Invalid(p)) {
    DisconnectNamedPipe(p);
    CloseHandle(p);
  }
  p = INVALID_HANDLE_VALUE;
}

void PipeChannelBase::_Receive(HANDLE pipe, LPVOID msg, size_t rec_len) {
  response_slot.Reset();
  DWORD lread = 0;
  BOOL success = ::ReadFile(pipe, msg, rec_len, &lread, NULL);
  if (!success) {
    _ThrowIfNot(ERROR_MORE_DATA);

    memset(buffer.get(), 0, buff_size);
    success = ::ReadFile(pipe, buffer.get(), buff_size, &lread, NULL);
    if (!success) {
      _ThrowLastError;
    }
    if (!response_slot.Complete(lread, buff_size)) throw DWORD(ERROR_INVALID_DATA);
  } else if (lread != rec_len) {
    throw DWORD(ERROR_INVALID_DATA);
  }
  has_body = false;
}

HANDLE PipeChannelBase::_ConnectServerPipe(std::wstring& pn) {
  HANDLE pipe =
      CreateNamedPipe(pn.c_str(), PIPE_ACCESS_DUPLEX,
                      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                      PIPE_UNLIMITED_INSTANCES, buff_size, buff_size, 0, sa);
  if (pipe == INVALID_HANDLE_VALUE) _ThrowLastError;
  if (!::ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
    const DWORD error = GetLastError(); CloseHandle(pipe); throw error;
  }
  return pipe;
}
