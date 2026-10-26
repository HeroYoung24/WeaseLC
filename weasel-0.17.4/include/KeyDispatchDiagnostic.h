#pragma once
#include <windows.h>

namespace correction {
// Counts and equality failures only. No key codes, message times, context
// addresses, text, application names or document identities leave the TIP.
enum KeyPairReason : DWORD {
  NoPendingKey = 1, DifferentContext = 2, DifferentKey = 4,
  DifferentFlags = 8, DifferentMessageTime = 16
};
struct KeyDispatchDiagnostic {
  DWORD version = 1, callbacks = 0, dispatches = 0, reuses = 0;
  DWORD unhandled = 0, actualMisses = 0, reasons = 0, invalidations = 0;
  FILETIME time{};
};
static_assert(sizeof(KeyDispatchDiagnostic) == 40, "bounded key dispatch metadata");
inline DWORD KeyPairReasons(bool pending, bool context, bool key, bool flags, bool time) {
  return !pending ? NoPendingKey :
      (context ? 0u : DifferentContext) | (key ? 0u : DifferentKey) |
      (flags ? 0u : DifferentFlags) | (time ? 0u : DifferentMessageTime);
}
}
