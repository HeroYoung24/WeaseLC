#pragma once
#include <windows.h>

namespace correction {
// Probe-only ABI. Ordinary builds have no callback registration/export.
constexpr DWORD kCommitCaptureVersion = 1;
constexpr DWORD kCommitCaptureCapacity = 1024;
enum class CaptureKind : DWORD { Cancel, Commit };
struct CommitCapture {
  DWORD abi = kCommitCaptureVersion;
  CaptureKind kind = CaptureKind::Cancel;
  ULONGLONG generation = 0;
  ULONGLONG sequence = 0;
  DWORD units = 0;
  WCHAR text[kCommitCaptureCapacity + 1] = {};
};
using CommitCaptureSink = void(WINAPI*)(const CommitCapture*, void*);
}  // namespace correction
