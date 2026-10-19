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
    BYTE signal = 0; DWORD bytes = 0;
    // No request text travels on stdin. Its EOF is owned by the input backend.
    ReadFile(parent, &signal, 1, &bytes, nullptr);
    broker.Stop(); return 0;
  } catch (...) { return 4; }
}
