#pragma once
#include "CorrectionProbe.h"
#include "CorrectionFocus.h"
#include "CorrectionScope.h"
#if defined(WEASELTSF_EXPORTS) && !defined(WEASEL_CORRECTION_PROBE_BUILD)
#include "CorrectionBrokerWire.h"
#endif

namespace correction {
enum class AutomaticStage : DWORD {
  NotRun, Disabled, Captured, FocusBlocked, ScopeUnavailable, ProtectedField,
  ReadOnly, TextChanged, SelectionUnavailable, SelectionMoved, RangeMismatch,
  LayoutUnavailable, CandidateUnavailable, WaitingComposition, WorkerFailed,
  WorkerStarted, NoSuggestion, InvalidReply, Ready, Shown, TabApplied, RequestDenied, SettingsChanged, StatusUnavailable, Expired,
  TabWriteUnconfirmed, TabRestoreUnconfirmed, TabPersistenceUnconfirmed
};
struct AutomaticDiagnostic {
  DWORD version = 1;
  AutomaticStage stage = AutomaticStage::NotRun;
  FocusStage focus = FocusStage::Ready;
  HRESULT error = S_OK;
  FILETIME time{};
};
// Separate key: diagnostic updates must not invalidate SettingsTime's root key.
inline constexpr wchar_t kDiagnosticKey[] = WEASEL_CORRECTION_REG_KEY L"\\Diagnostics";
inline ScopeDiagnostic LoadScopeDiagnostic(const wchar_t* path = kDiagnosticKey) {
  ScopeDiagnostic result;
  DWORD bytes = sizeof(result);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"LastScope", RRF_RT_REG_BINARY,
      nullptr, &result, &bytes) != ERROR_SUCCESS || bytes != sizeof(result) || result.version != 1 ||
      result.stage > ScopeStage::NativePlainText || result.variantType > 0xffff) return {};
  return result;
}
inline void SaveScopeDiagnostic(const ScopeDiagnostic& result, const wchar_t* path = kDiagnosticKey) {
#if defined(WEASELTSF_EXPORTS) && !defined(WEASEL_CORRECTION_PROBE_BUILD)
  if (!wcscmp(path, kDiagnosticKey)) {
    RelayCorrectionMetadata(BrokerMetadata::Scope, &result, sizeof(result)); return;
  }
#endif
  const auto previous = LoadScopeDiagnostic(path);
  if (previous.stage == result.stage && previous.error == result.error &&
      previous.scope == result.scope && previous.variantType == result.variantType) return;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
  RegSetValueExW(key, L"LastScope", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&result), sizeof(result));
  RegCloseKey(key);
}
inline void SaveAutomaticDiagnostic(AutomaticDiagnostic result, const wchar_t* path = kDiagnosticKey) {
#if defined(WEASELTSF_EXPORTS) && !defined(WEASEL_CORRECTION_PROBE_BUILD)
  if (!wcscmp(path, kDiagnosticKey)) {
    GetSystemTimeAsFileTime(&result.time);
    RelayCorrectionMetadata(BrokerMetadata::Automatic, &result, sizeof(result)); return;
  }
#endif
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0,
      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
  GetSystemTimeAsFileTime(&result.time);
  RegSetValueExW(key, L"LastAutomatic", 0, REG_BINARY,
      reinterpret_cast<const BYTE*>(&result), sizeof(result));
  RegCloseKey(key);
}
inline AutomaticDiagnostic LoadAutomaticDiagnostic(const wchar_t* path = kDiagnosticKey) {
  AutomaticDiagnostic result;
  DWORD bytes = sizeof(result);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"LastAutomatic", RRF_RT_REG_BINARY,
      nullptr, &result, &bytes) != ERROR_SUCCESS || bytes != sizeof(result) || result.version != 1 ||
      result.stage > AutomaticStage::TabPersistenceUnconfirmed || result.focus > FocusStage::DifferentWindow)
    return {};
  return result;
}
inline const wchar_t* AutomaticStageText(AutomaticStage stage, bool chinese) {
  const wchar_t* zh[] = {L"尚无自动检查记录",L"自动纠错关闭",L"已捕获新上屏文字",L"焦点核对未通过",
    L"输入范围不可确认",L"受保护字段，已跳过",L"只读字段，已跳过",L"原文已变化",L"选区不可读",
    L"光标或组合已变化",L"组合范围不匹配",L"光标位置不可用",L"候选框位置不可用",L"等待下次组合输入",
    L"模型子进程启动或通信失败",L"模型检查中",L"未返回可展示建议",L"模型响应无效或分数不足",
    L"建议已就绪",L"提示已显示",L"Tab 后续会话回读通过",L"宿主编辑请求失败",L"设置已变化",L"宿主状态不可用",L"建议已过期",
    L"Tab 写入返回成功但未确认改文",L"Tab 恢复组合或选区后未确认改文",L"Tab 后续会话未确认改文"};
  const wchar_t* en[] = {L"No automatic check yet",L"Automatic correction off",L"New commit captured",L"Focus check failed",
    L"Input scope unavailable",L"Protected field skipped",L"Read-only field skipped",L"Original text changed",L"Selection unavailable",
    L"Selection/composition changed",L"Composition range mismatch",L"Caret layout unavailable",L"Candidate layout unavailable",L"Waiting for composition",
    L"Worker launch/IPC failed",L"Model checking",L"No displayable suggestion",L"Invalid/filtered reply",L"Suggestion ready",L"Hint shown",L"Tab read back in subsequent session",L"Edit lock denied",L"Settings changed",L"Host status unavailable",L"Suggestion expired",
    L"Tab write acknowledged but unconfirmed",L"Tab write unconfirmed after composition/selection restore",L"Tab write unconfirmed in subsequent session"};
  const auto index = static_cast<unsigned>(stage);
  if (index > static_cast<unsigned>(AutomaticStage::TabPersistenceUnconfirmed)) return chinese ? L"未知" : L"unknown";
  return chinese ? zh[index] : en[index];
}
}  // namespace correction
