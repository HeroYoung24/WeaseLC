#include <windows.h>
#include "../WeaselServer/CorrectionBroker.h"

int RunCorrectionBroker() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const auto parent = GetStdHandle(STD_INPUT_HANDLE);
  BOOL inJob = FALSE;
  if (!parent || parent == INVALID_HANDLE_VALUE || GetFileType(parent) != FILE_TYPE_PIPE ||
      !IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) || !inJob) return 2;
  try {
    correction::CorrectionBroker broker;
    if (!broker.Start()) return 3;
    // No request text travels on stdin. Its EOF is owned by the input backend.
    // Observe listener health too: a fatal accept failure must not leave a
    // live-looking child with no endpoint. Restarting the backend recreates it.
    DWORD available = 0;
    while (broker.IsRunning() && PeekNamedPipe(parent, nullptr, 0, nullptr, &available, nullptr) && !available)
      Sleep(50);
    const bool healthy = broker.IsRunning();
    broker.Stop(); return healthy ? 0 : 3;
  } catch (...) { return 4; }
}
