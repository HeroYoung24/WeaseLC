#include <windows.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <CorrectionProbe.h>
#include <CorrectionDiagnostics.h>
#include <CorrectionReview.h>
#include <CorrectionLexicon.h>
#include <CorrectionServiceControl.h>
#include <CorrectionBrokerClient.h>
#include "Resource.h"
#include "CorrectionPreferences.h"
#include "CorrectionSettingsDialog.h"
#include "CorrectionHintPreview.h"
#include "CorrectionThemeChecks.h"
#include <CorrectionAppPolicy.h>
#include <CorrectionBehavior.h>
#include <shellapi.h>
#include "LocalModelControl.h"

namespace {
using namespace correction;
constexpr UINT_PTR kPoll = 1;
struct ServiceJob {
  std::atomic_bool done{false};
  HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  bool available = false, endpoint = false, retry = false;
  ServiceReply service;
  BrokerReply health;
  ~ServiceJob() { if (cancel) CloseHandle(cancel); }
};
struct Job {
  std::atomic_bool cancelled{false};
  std::atomic_bool done{false};
  CheckResult result;
  std::wstring text;
  ULONGLONG started = GetTickCount64();
};
struct DialogState {
  Preferences preferences;
  Mode editing = Mode::Local;
  std::shared_ptr<Job> job;
  std::shared_ptr<ServiceJob> serviceJob;
  ULONGLONG servicePolled = 0;
  bool canRetryService = false;
  SuggestionReview review;
  UserLexicon lexicon;
  bool lexiconReady = true;
  std::uint64_t version = 1;
  bool updating = false;
  bool testing = false;
  bool demo = false;
  std::function<std::unique_ptr<IModelClient>()> testClient;
  bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
  const wchar_t* Text(const wchar_t* zh, const wchar_t* en) const { return chinese ? zh : en; }
  ModelConfig& Profile() { return editing == Mode::Online ? preferences.online : preferences.local; }
};
struct AppDialogState {
  DialogState& owner;
  std::vector<std::wstring> apps;
  bool loaded = false;
  std::vector<std::wstring> original;
};
void RefreshApps(HWND dialog, const AppDialogState& state) {
  SendDlgItemMessageW(dialog, IDC_APP_LIST, LB_RESETCONTENT, 0, 0);
  for (const auto& app : state.apps)
    SendDlgItemMessageW(dialog, IDC_APP_LIST, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(app.c_str()));
}
INT_PTR CALLBACK AppsDialogProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
  auto* state = reinterpret_cast<AppDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<AppDialogState*>(l); SetWindowLongPtrW(dialog, DWLP_USER, l);
    state->loaded = state->owner.testing || state->owner.demo || LoadPausedApps(state->apps);
    state->original = state->apps;
    SendDlgItemMessageW(dialog, IDC_APP_NAME, EM_SETLIMITTEXT, kMaxAppName, 0);
    if (!state->loaded) SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(
        L"无法读取现有偏好；为保护原设置，本次不能保存。", L"Preferences unavailable; saving is disabled."));
    EnableWindow(GetDlgItem(dialog, IDOK), state->loaded);
    if (state->loaded && !state->owner.testing && !state->owner.demo) {
      BehaviorHistory history;
      if (LoadBehaviorHistory(history)) {
        CheckDlgButton(dialog, IDC_APP_BEHAVIOR_ENABLED, history.header.enabled ? BST_CHECKED : BST_UNCHECKED);
        const auto text = std::wstring(state->owner.Text(L"本地纠错使用记录（组）：", L"Local correction history (groups): ")) + std::to_wstring(history.entries.size());
        SetDlgItemTextW(dialog, IDC_APP_STATE, text.c_str());
      } else { EnableWindow(GetDlgItem(dialog, IDC_APP_BEHAVIOR_ENABLED), FALSE); SetDlgItemTextW(dialog, IDC_APP_STATE,
          state->owner.Text(L"使用记录不可读；本次不学习，可清除记录后重开。", L"History unavailable; learning skipped. Clear history and reopen.")); }
    } else {
      CheckDlgButton(dialog, IDC_APP_BEHAVIOR_ENABLED, BST_CHECKED);
    }
    RefreshApps(dialog, *state); return TRUE;
  }
  if (!state) return FALSE;
  if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
  if (message != WM_COMMAND) return FALSE;
  const auto id = LOWORD(w);
  if (id == IDC_APP_ADD) {
    wchar_t buffer[kMaxAppName + 1]{}; GetDlgItemTextW(dialog, IDC_APP_NAME, buffer, _countof(buffer));
    std::wstring app;
    if (!AppName(buffer, app) || state->apps.size() >= kMaxPausedApps || app == L"winword.exe") {
      SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(
          L"填写程序名（如 msedge.exe），不含路径；最多64项。Word固定仅普通输入。",
          L"Use a filename such as msedge.exe (no path), max 64. Word stays input-only.")); return TRUE;
    }
    if (std::find(state->apps.begin(), state->apps.end(), app) == state->apps.end()) state->apps.push_back(std::move(app));
    RefreshApps(dialog, *state); SetDlgItemTextW(dialog, IDC_APP_NAME, L"");
    SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(L"按保存后生效。", L"Choose Save to apply.")); return TRUE;
  }
  if (id == IDC_APP_REMOVE) {
    const auto index = SendDlgItemMessageW(dialog, IDC_APP_LIST, LB_GETCURSEL, 0, 0);
    if (index >= 0 && static_cast<size_t>(index) < state->apps.size()) state->apps.erase(state->apps.begin() + index);
    RefreshApps(dialog, *state); return TRUE;
  }
  if (id == IDC_APP_DATA) {
    if (!state->owner.testing) MessageBoxW(dialog, state->owner.Text(
        L"输入习惯：由 Rime 用户词典管理；请使用原有用户词典管理先备份，再导入/恢复。这里不删除日常词库。\n\n"
        L"纠错保留表达：在管理纠错的“管理保留词”中增删或清空；不是词频记录。\n\n"
        L"临时上文：有界且不保存为正文历史；关闭纠错、切换场景或到期时清除。\n\n"
        L"纠错使用记录：仅在本地保存带随机盐的建议/语境摘要和接受、忽略、拒绝次数，不保存原文；用于排序和临时减少重复提示，不自动写入固定搭配或用户词库。可关闭或清除。\n\n"
        L"模型权重：与学习记录分开保存，本窗口不删除模型。",
        L"Input habits: managed by the existing Rime dictionary manager; back up before import/restore. This page never deletes your daily dictionary.\n\n"
        L"Kept expressions: add/remove/clear in Manage kept terms; this is separate from word frequency.\n\n"
        L"Temporary context: bounded, not saved as document history; cleared on disable, context change or expiry.\n\n"
        L"Correction history: local salted pair/context digests and acceptance/ignore/rejection counts, no original text. Used for ranking and temporary suppression, never automatic dictionary entries. You can disable or clear it.\n\n"
        L"Model weights are separate and are not deleted here."), L"WeaseLC", MB_OK | MB_ICONINFORMATION);
    return TRUE;
  }
  if (id == IDC_APP_CLEAR_FEEDBACK) {
    if (state->owner.testing || state->owner.demo ||
        MessageBoxW(dialog, state->owner.Text(L"清除纠错使用记录？不会删除保留词、输入习惯或模型。",
            L"Clear correction usage history? Kept terms, input habits and model files stay unchanged."),
            L"WeaseLC", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
      const bool cleared = state->owner.testing || state->owner.demo || SaveBehaviorOption(false, true);
      SetDlgItemTextW(dialog, IDC_APP_STATE, cleared ? state->owner.Text(L"纠错使用记录已清除。", L"Correction usage history cleared.") :
          state->owner.Text(L"无法清除记录；其他数据未改动。", L"Could not clear history; other data unchanged."));
    }
    return TRUE;
  }
  if (id == IDC_APP_BEHAVIOR_ENABLED) {
    const bool enabled = IsDlgButtonChecked(dialog, id) == BST_CHECKED;
    if (state->owner.testing || state->owner.demo || SaveBehaviorOption(enabled, false))
      SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(L"行为学习开关已立即保存；不改变模型参数。", L"Usage learning choice saved immediately; model parameters unchanged."));
    else { CheckDlgButton(dialog, id, enabled ? BST_UNCHECKED : BST_CHECKED); SetDlgItemTextW(dialog, IDC_APP_STATE,
        state->owner.Text(L"无法保存行为学习开关。", L"Could not save usage learning choice.")); }
    return TRUE;
  }
  if (id == IDC_APP_RESET) {
    if (state->owner.testing || state->owner.demo || MessageBoxW(dialog, state->owner.Text(
        L"重置暂停应用列表？不会开启全局纠错，Word仍仅普通输入。", L"Reset paused apps? This does not enable global correction; Word stays input-only."),
        L"WeaseLC", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
      if (state->owner.testing || state->owner.demo || SavePausedApps({})) {
        state->apps.clear(); state->original.clear(); state->loaded = true; RefreshApps(dialog, *state);
        SetDlgItemTextW(dialog, IDC_APP_NAME, L"");
        EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
        SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(L"暂停列表已重置并保存。", L"Paused apps reset and saved."));
      } else SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(L"无法重置暂停列表。", L"Could not reset paused apps."));
    }
    return TRUE;
  }
  if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
  if (id == IDOK) {
    if (GetWindowTextLengthW(GetDlgItem(dialog, IDC_APP_NAME))) {
      SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(
          L"请先添加或清空程序名，再保存，避免遗漏此项。", L"Add or clear the typed name before saving so it is not lost.")); return TRUE;
    }
    if (!state->owner.testing && !state->owner.demo) {
      std::vector<std::wstring> current;
      if (!LoadPausedApps(current) || current != state->original) {
        SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(
            L"偏好已在其他窗口更改；请取消后重开，避免覆盖。", L"Choices changed elsewhere. Cancel and reopen to avoid overwriting them.")); return TRUE;
      }
    }
    if (!state->loaded || (!state->owner.testing && !state->owner.demo && !SavePausedApps(state->apps))) {
      SetDlgItemTextW(dialog, IDC_APP_STATE, state->owner.Text(L"保存失败；原偏好未更改。", L"Save failed; original preferences unchanged.")); return TRUE;
    }
    EndDialog(dialog, IDOK); return TRUE;
  }
  return FALSE;
}
void ShowService(HWND dialog, DialogState& state, const ServiceJob& job) {
  std::wstring backend = state.Text(L"无法取得监护状态：后端未启动或版本不匹配。", L"Supervisor unavailable: backend absent or version mismatch.");
  if (job.available) {
    switch (static_cast<BrokerProcessStage>(job.service.stage)) {
      case BrokerProcessStage::Stopped: backend = state.Text(L"纠错服务已停止。", L"Correction service stopped."); break;
      case BrokerProcessStage::Starting: backend = state.Text(L"纠错服务正在启动。", L"Correction service starting."); break;
      case BrokerProcessStage::Running:
        backend = job.endpoint ? state.Text(L"纠错协调器已连接。", L"Correction coordinator connected.") :
            state.Text(L"协调器进程运行中，接口尚未响应。", L"Coordinator process running; endpoint not responding."); break;
      case BrokerProcessStage::Backoff: backend = state.Text(L"纠错服务正在等待重试。", L"Correction service waiting to retry."); break;
      case BrokerProcessStage::Exhausted:
        backend = job.service.launchFailed ? state.Text(L"无法启动纠错服务，已停止自动重试。", L"Could not start correction service; automatic retries stopped.") :
            state.Text(L"纠错服务连续退出，已停止自动重试。", L"Correction service repeatedly exited; automatic retries stopped."); break;
    }
  }
  if (job.retry && !job.service.accepted) backend += state.Text(L" 恢复未受理，请稍后重试。", L" Recovery not accepted; check again.");
  SetDlgItemTextW(dialog, IDC_CORRECTION_SERVICE_STATE, backend.c_str());
  std::wstring model = state.Text(L"模型状态未知；可在右侧手动检查，不会自动发送文本。", L"Model untested. Use Check text to test; no text is sent automatically here.");
  if (job.endpoint && job.health.status == BrokerStatus::InvalidRequest)
    model = state.Text(L"当前协调器不支持状态查询，请更新配套后端。", L"Coordinator lacks status support; update the matching backend.");
  else if (job.endpoint && job.health.status != BrokerStatus::Ok)
    model = state.Text(L"已保存配置不可用；请核对模型设置并保存。", L"Saved configuration unavailable; review model settings and save.");
  else if (job.endpoint && !job.health.settings.enabled)
    model = state.Text(L"自动提示未启用（按已保存配置）；手动检查可独立使用。", L"Automatic hints disabled in saved settings; manual checking is separate.");
  else if (job.endpoint && job.health.modelState <= static_cast<DWORD>(State::InvalidResponse)) {
    model = state.Text(L"上次自动检查：", L"Last automatic check: ");
    model += StateText(static_cast<State>(job.health.modelState), state.chinese);
  }
  SetDlgItemTextW(dialog, IDC_CORRECTION_SERVICE_MODEL, model.c_str());
  state.canRetryService = job.available && job.service.retryAvailable &&
      job.service.stage == static_cast<DWORD>(BrokerProcessStage::Exhausted) && !state.demo;
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_SERVICE_RETRY), state.canRetryService);
}
void BeginServicePoll(HWND dialog, DialogState& state, bool retry = false) {
  if (state.testing || state.demo || state.serviceJob || (retry && !state.canRetryService)) return;
  auto job = std::make_shared<ServiceJob>();
  if (!job->cancel) return;
  WCHAR filename[32768]{};
  if (!GetModuleFileNameW(nullptr, filename, _countof(filename))) return;
  const std::wstring self(filename);
  const auto slash = self.find_last_of(L'\\');
  if (slash == std::wstring::npos) return;
  const auto server = self.substr(0, slash + 1) + L"WeaselServer.exe";
  state.serviceJob = job; state.servicePolled = GetTickCount64(); job->retry = retry;
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_SERVICE_RETRY), FALSE);
  try {
    std::thread([job, self, server]() {
      try {
        job->available = QueryCorrectionService(server, job->retry, job->service, job->cancel);
        if (WaitForSingleObject(job->cancel, 0) == WAIT_TIMEOUT) {
          BrokerCall call; BrokerRequest request; request.operation = BrokerOperation::Health;
          job->endpoint = call.Exchange(self, request, job->health, 120);
        }
      } catch (...) {}
      job->done.store(true);
    }).detach();
  } catch (...) {
    state.serviceJob.reset(); ShowService(dialog, state, *job);
  }
}
void PollService(HWND dialog, DialogState& state) {
  if (state.serviceJob && state.serviceJob->done.load()) {
    auto completed = std::move(state.serviceJob);
    ShowService(dialog, state, *completed);
  }
  if (GetTickCount64() - state.servicePolled >= 1000) BeginServicePoll(dialog, state);
}
std::wstring Text(HWND window, int id) {
  const HWND control = GetDlgItem(window, id);
  const int count = GetWindowTextLengthW(control);
  std::wstring text(count + 1, L'\0');
  GetWindowTextW(control, text.data(), count + 1);
  text.resize(count);
  return text;
}
void SetNumber(HWND dialog, int id, unsigned value) { SetDlgItemInt(dialog, id, value, FALSE); }
bool ReadNumber(HWND dialog, int id, unsigned& value) {
  const auto text = Text(dialog, id);
  if (text.empty() || text.size() > 10 || std::any_of(text.begin(), text.end(),
      [](wchar_t c) { return c < L'0' || c > L'9'; })) return false;
  BOOL converted = FALSE;
  value = GetDlgItemInt(dialog, id, &converted, FALSE);
  return converted != FALSE;
}
void Cancel(DialogState& state, HWND dialog) {
  ++state.version;
  if (state.job && !state.job->done.load()) state.job->cancelled.store(true);
  state.review.Clear();
  SendDlgItemMessageW(dialog, IDC_CORRECTION_SUGGESTIONS, LB_RESETCONTENT, 0, 0);
  SetDlgItemTextW(dialog, IDC_CORRECTION_RESULT, L"");
}
void ReadProfile(HWND dialog, DialogState& state) {
  auto& profile = state.Profile();
  const auto endpoint = Text(dialog, IDC_CORRECTION_ENDPOINT);
  const auto model = Text(dialog, IDC_CORRECTION_MODEL);
  const bool identityChanged = endpoint != profile.endpoint || model != profile.model;
  profile.endpoint = endpoint; profile.model = model;
  unsigned timeout = 0;
  profile.timeoutSeconds = ReadNumber(dialog, IDC_CORRECTION_TIMEOUT, timeout) ? timeout : 0;
  const auto selection = SendDlgItemMessageW(dialog, IDC_CORRECTION_SENSITIVITY, CB_GETCURSEL, 0, 0);
  profile.sensitivity = selection >= 0 ? static_cast<unsigned>(selection) : 0;
  profile.useCustomThreshold = IsDlgButtonChecked(dialog, IDC_CORRECTION_CUSTOM_SCORE) == BST_CHECKED;
  unsigned score = 0;
  const bool scoreValid = ReadNumber(dialog, IDC_CORRECTION_SCORE, score);
  profile.minimumScore = scoreValid && score <= 100 ? score / 100.0 : profile.useCustomThreshold ? -1 : .90;
  profile.jsonMode = IsDlgButtonChecked(dialog, IDC_CORRECTION_JSON_MODE) == BST_CHECKED;
  if (identityChanged) CheckDlgButton(dialog, IDC_CORRECTION_CONSENT, BST_UNCHECKED);
  // An old model's raw threshold must not silently transfer to a new model.
  if (identityChanged && profile.useCustomThreshold) {
    profile.useCustomThreshold = false; profile.minimumScore = 0.90;
    state.updating = true;
    CheckDlgButton(dialog, IDC_CORRECTION_CUSTOM_SCORE, BST_UNCHECKED);
    SetNumber(dialog, IDC_CORRECTION_SCORE, 90);
    state.updating = false;
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
        state.Text(L"地址或模型已改变，自定义分数已恢复默认；可重新设置。",
                   L"Endpoint/model changed; custom score reset. You can set it again."));
  }
}
void UpdateButtons(HWND dialog, DialogState& state) {
  const bool busy = state.job && !state.job->done.load();
  const bool enabled = state.preferences.mode != Mode::Off;
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_CHECK), enabled && !busy && state.lexiconReady);
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_CANCEL_CHECK), busy);
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_SCORE),
      IsDlgButtonChecked(dialog, IDC_CORRECTION_CUSTOM_SCORE) == BST_CHECKED);
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_CONSENT), state.preferences.mode == Mode::Online);
  const auto selected = SendDlgItemMessageW(dialog, IDC_CORRECTION_SUGGESTIONS, LB_GETCURSEL, 0, 0);
  const bool actionable = selected != LB_ERR && state.review.Selected(static_cast<std::size_t>(selected),
      Text(dialog, IDC_CORRECTION_TEST_TEXT), state.version) != nullptr;
  for (int id : {IDC_CORRECTION_APPLY, IDC_CORRECTION_IGNORE})
    EnableWindow(GetDlgItem(dialog, id), actionable && state.lexiconReady);
}
void ShowSuggestion(HWND dialog, DialogState& state) {
  const auto selected = SendDlgItemMessageW(dialog, IDC_CORRECTION_SUGGESTIONS, LB_GETCURSEL, 0, 0);
  const auto* item = state.review.Selected(static_cast<std::size_t>(selected), Text(dialog, IDC_CORRECTION_TEST_TEXT), state.version);
  std::wstring detail;
  if (item) {
    detail = item->original + L" → " + item->replacement + L"\r\n" + item->reason + L"\r\n";
    detail += state.Text(L"模型分数（未校准）：", L"Model score (uncalibrated): ");
    detail += std::to_wstring(static_cast<unsigned>(std::lround(item->score * 100))) + L"/100";
  }
  SetDlgItemTextW(dialog, IDC_CORRECTION_RESULT, detail.c_str());
  UpdateButtons(dialog, state);
}
void RefreshSuggestions(HWND dialog, DialogState& state) {
  SendDlgItemMessageW(dialog, IDC_CORRECTION_SUGGESTIONS, LB_RESETCONTENT, 0, 0);
  for (const auto& item : state.review.Items()) {
    const auto line = item.original + L" → " + item.replacement;
    SendDlgItemMessageW(dialog, IDC_CORRECTION_SUGGESTIONS, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }
  if (!state.review.Items().empty()) SendDlgItemMessageW(dialog, IDC_CORRECTION_SUGGESTIONS, LB_SETCURSEL, 0, 0);
  ShowSuggestion(dialog, state);
}
struct TermDialogState { DialogState& owner; UserLexicon working; bool modeless = false; };
struct AutomaticDialogState {
  DialogState& owner; AutomaticOptions working; bool modeless = false;
  CorrectionHintPreview preview;
};
bool ReadAutomaticOptions(HWND dialog, AutomaticDialogState& state, AutomaticOptions& options, bool explain) {
  options = state.working;
  options.enabled = IsDlgButtonChecked(dialog, IDC_AUTO_ENABLED) == BST_CHECKED;
  options.tab = IsDlgButtonChecked(dialog, IDC_AUTO_TAB) == BST_CHECKED;
  options.wordLocalCompatibility = false;
  options.side = static_cast<unsigned>(SendDlgItemMessageW(dialog, IDC_AUTO_SIDE, CB_GETCURSEL, 0, 0));
  struct Field { int id; unsigned low, high; unsigned* output; };
  for (auto field : {Field{IDC_AUTO_GAP, 0, 48, &options.gap}, {IDC_AUTO_WIDTH, 180, 640, &options.width},
       {IDC_AUTO_FONT, 10, 24, &options.font}, {IDC_AUTO_DEBOUNCE, 100, 2000, &options.debounce}}) {
    unsigned value = 0;
    if (!ReadNumber(dialog, field.id, value) || value < field.low || value > field.high) {
      if (explain) {
        SetDlgItemTextW(dialog, IDC_AUTO_STATE, state.owner.Text(L"请填写标注范围内的数字。", L"Enter a number in the shown range."));
        if (!state.owner.testing) SetFocus(GetDlgItem(dialog, field.id));
        SendDlgItemMessageW(dialog, field.id, EM_SETSEL, 0, -1);
      }
      return false;
    }
    *field.output = value;
  }
  return ValidAutomaticOptions(options);
}
INT_PTR CALLBACK AutomaticDialogProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
  auto* state = reinterpret_cast<AutomaticDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<AutomaticDialogState*>(l);
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    const auto& options = state->working;
    CheckDlgButton(dialog, IDC_AUTO_ENABLED, options.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_AUTO_TAB, options.tab ? BST_CHECKED : BST_UNCHECKED);
    for (const auto* label : {state->owner.Text(L"右侧（默认）", L"Right (default)"),
         state->owner.Text(L"左侧", L"Left"), state->owner.Text(L"下方", L"Below")})
      SendDlgItemMessageW(dialog, IDC_AUTO_SIDE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(dialog, IDC_AUTO_SIDE, CB_SETCURSEL, options.side, 0);
    for (const auto pair : {std::pair<int, unsigned>{IDC_AUTO_GAP, options.gap},
         {IDC_AUTO_WIDTH, options.width}, {IDC_AUTO_FONT, options.font}, {IDC_AUTO_DEBOUNCE, options.debounce}}) {
      SetNumber(dialog, pair.first, pair.second);
      SendDlgItemMessageW(dialog, pair.first, EM_SETLIMITTEXT, 4, 0);
    }
    return TRUE;
  }
  if (!state) return FALSE;
  if (message == WM_DESTROY) { state->preview.Close(); return TRUE; }
  if (message == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) { state->preview.Hide(); return FALSE; }
  if (message == WM_MOVE && state->preview.Window()) {
    AutomaticOptions options;
    if (ReadAutomaticOptions(dialog, *state, options, false)) state->preview.Show(dialog, options, state->owner.chinese, !state->owner.testing);
    return TRUE;
  }
  auto close = [&](int result) { if (state->modeless) DestroyWindow(dialog); else EndDialog(dialog, result); };
  if (message == WM_CLOSE) { close(IDCANCEL); return TRUE; }
  if (message != WM_COMMAND) return FALSE;
  if (LOWORD(w) == IDC_AUTO_PREVIEW || (state->preview.Window() &&
      (HIWORD(w) == EN_CHANGE || HIWORD(w) == CBN_SELCHANGE || LOWORD(w) == IDC_AUTO_TAB))) {
    AutomaticOptions options;
    if (ReadAutomaticOptions(dialog, *state, options, LOWORD(w) == IDC_AUTO_PREVIEW)) {
      if (!state->preview.Show(dialog, options, state->owner.chinese, !state->owner.testing))
        SetDlgItemTextW(dialog, IDC_AUTO_STATE, state->owner.Text(L"预览空间不足，请减小宽度或字号。", L"Preview needs more room. Reduce width/font."));
      else SetDlgItemTextW(dialog, IDC_AUTO_STATE, L"");
    } else state->preview.Hide();
    return TRUE;
  }
  if (LOWORD(w) == IDCANCEL) { close(IDCANCEL); return TRUE; }
  if (LOWORD(w) == IDOK) {
    AutomaticOptions options;
    if (!ReadAutomaticOptions(dialog, *state, options, true)) return TRUE;
    state->owner.preferences.automatic = options;
    close(IDOK); return TRUE;
  }
  return FALSE;
}
void CloseTerms(HWND dialog, TermDialogState& state, INT_PTR result) {
  if (state.modeless) DestroyWindow(dialog); else EndDialog(dialog, result);
}
void RefreshTerms(HWND dialog, TermDialogState& state) {
  SendDlgItemMessageW(dialog, IDC_CORRECTION_TERM_LIST, LB_RESETCONTENT, 0, 0);
  for (const auto& term : state.working.terms)
    SendDlgItemMessageW(dialog, IDC_CORRECTION_TERM_LIST, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(term.c_str()));
  const auto count = std::to_wstring(state.working.terms.size()) + L"/256";
  SetDlgItemTextW(dialog, IDC_CORRECTION_TERM_STATE, count.c_str());
}
INT_PTR CALLBACK TermDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* state = reinterpret_cast<TermDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<TermDialogState*>(lParam);
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    SendDlgItemMessageW(dialog, IDC_CORRECTION_TERM_TEXT, EM_SETLIMITTEXT, kMaxKeptTermUnits, 0);
    RefreshTerms(dialog, *state); return TRUE;
  }
  if (!state) return FALSE;
  if (message == WM_CLOSE) { CloseTerms(dialog, *state, IDCANCEL); return TRUE; }
  if (message != WM_COMMAND) return FALSE;
  switch (LOWORD(wParam)) {
    case IDC_CORRECTION_TERM_ADD:
      if (!KeepTerm(state->working, Text(dialog, IDC_CORRECTION_TERM_TEXT)))
        SetDlgItemTextW(dialog, IDC_CORRECTION_TERM_STATE, state->owner.Text(L"请输入 1–64 单元的表达；最多 256 条。", L"Use 1–64 units; at most 256 terms."));
      else { SetDlgItemTextW(dialog, IDC_CORRECTION_TERM_TEXT, L""); RefreshTerms(dialog, *state); }
      return TRUE;
    case IDC_CORRECTION_TERM_REMOVE: {
      auto index = SendDlgItemMessageW(dialog, IDC_CORRECTION_TERM_LIST, LB_GETCURSEL, 0, 0);
      if (index != LB_ERR) RemoveTerm(state->working, static_cast<std::size_t>(index));
      RefreshTerms(dialog, *state); return TRUE;
    }
    case IDC_CORRECTION_TERM_CLEAR: state->working.terms.clear(); RefreshTerms(dialog, *state); return TRUE;
    case IDOK:
      if (!state->owner.demo && !state->owner.testing && !SaveUserLexicon(state->working)) {
        SetDlgItemTextW(dialog, IDC_CORRECTION_TERM_STATE, state->owner.Text(L"保存失败，原词表未更改。", L"Save failed; original terms unchanged.")); return TRUE;
      }
      state->owner.lexicon = state->working; state->owner.lexiconReady = true;
      CloseTerms(dialog, *state, IDOK); return TRUE;
    case IDCANCEL: CloseTerms(dialog, *state, IDCANCEL); return TRUE;
  }
  return FALSE;
}
void LoadProfile(HWND dialog, DialogState& state) {
  state.updating = true;
  const auto& profile = state.Profile();
  SetDlgItemTextW(dialog, IDC_CORRECTION_ENDPOINT, profile.endpoint.c_str());
  SetDlgItemTextW(dialog, IDC_CORRECTION_MODEL, profile.model.c_str());
  SetNumber(dialog, IDC_CORRECTION_TIMEOUT, profile.timeoutSeconds);
  SendDlgItemMessageW(dialog, IDC_CORRECTION_SENSITIVITY, CB_SETCURSEL, profile.sensitivity, 0);
  SetNumber(dialog, IDC_CORRECTION_SCORE, static_cast<unsigned>(std::lround(profile.minimumScore * 100)));
  CheckDlgButton(dialog, IDC_CORRECTION_CUSTOM_SCORE, profile.useCustomThreshold ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dialog, IDC_CORRECTION_JSON_MODE, profile.jsonMode ? BST_CHECKED : BST_UNCHECKED);
  SetDlgItemTextW(dialog, IDC_CORRECTION_TOKEN, L"");
  CheckDlgButton(dialog, IDC_CORRECTION_CONSENT, BST_UNCHECKED);
  state.updating = false;
  UpdateButtons(dialog, state);
}
void BeginCheck(HWND dialog, DialogState& state) {
  if (state.job && !state.job->done.load()) return;
  if (!state.lexiconReady) return;
  Cancel(state, dialog);
  ReadProfile(dialog, state);
  ModelConfig config = state.Profile();
  config.mode = state.preferences.mode;
  config.bearerToken = Text(dialog, IDC_CORRECTION_TOKEN);
  config.onlineConsent = IsDlgButtonChecked(dialog, IDC_CORRECTION_CONSENT) == BST_CHECKED;
  if (config.mode == Mode::Off || !ValidateConfig(config)) {
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, StateText(State::InvalidConfig, state.chinese)); return;
  }
  CheckRequest request;
  request.session = 1; request.version = ++state.version;
  request.text = Text(dialog, IDC_CORRECTION_TEST_TEXT);
  request.safeText = true; request.source = Source::Manual;
  if (request.text.empty() || request.text.size() > kMaxTargetUnits) {
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, StateText(State::InvalidInput, state.chinese)); return;
  }
  state.job = std::make_shared<Job>();
  state.job->text = request.text;
  SetDlgItemTextW(dialog, IDC_CORRECTION_RESULT, L"");
  SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
      state.demo ? state.Text(L"正在进行离线模拟检查，没有调用真实模型。", L"Running an offline fixture; no real model is called.") :
          state.Text(L"正在检查本页文本；编辑文本、修改设置或关闭窗口将丢弃结果。",
                     L"Checking this text. Edits, settings changes or closing discard the result."));
  auto job = state.job;
  // The worker owns its data and never touches HWND. Closing the dialog is safe.
  // The HTTP worker actively cancels its asynchronous request on cancellation.
  try {
    std::thread([job, config = std::move(config), request = std::move(request),
                 factory = state.testClient]() mutable {
      try {
        auto client = factory ? factory() : CreateModelClient(config.provider);
        job->result = client->Check(config, request, job->cancelled);
      } catch (...) {
        job->result.state = State::TransportError;
        job->result.session = request.session; job->result.version = request.version;
      }
      if (!config.bearerToken.empty()) SecureZeroMemory(config.bearerToken.data(), config.bearerToken.size() * sizeof(wchar_t));
      job->done.store(true);  // Publishes the result to the UI polling thread.
    }).detach();
  } catch (...) {
    state.job.reset();
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, state.Text(L"无法启动检查任务。", L"Cannot start check."));
  }
  UpdateButtons(dialog, state);
}
void Poll(HWND dialog, DialogState& state) {
  if (!state.job || !state.job->done.load()) return;
  const auto job = std::move(state.job);
  if (!job->cancelled.load() && job->result.session == 1 && job->result.version == state.version &&
      job->text == Text(dialog, IDC_CORRECTION_TEST_TEXT)) {
    CheckRequest request; request.session = 1; request.version = state.version;
    request.safeText = true; request.text = job->text;
    auto result = job->result;
    FilterKeptTerms(state.lexicon, request, result);
    if (!state.review.Load(request, result) && result.state == State::Suggestions) result.state = State::InvalidResponse;
    std::wstring status = StateText(result.state, state.chinese);
    status += L" (" + std::to_wstring(GetTickCount64() - job->started) + L" ms)";
    if (state.demo) status = state.Text(L"离线模拟结果：", L"Offline fixture: ") + status;
    if (job->result.httpStatus && job->result.httpStatus != 200)
      status += L" HTTP " + std::to_wstring(job->result.httpStatus);
    if (job->result.nativeError) status += L" (" + std::to_wstring(job->result.nativeError) + L")";
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, status.c_str());
    RefreshSuggestions(dialog, state);
  }
  UpdateButtons(dialog, state);
}
INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<DialogState*>(lParam);
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    if (state->demo) SetWindowTextW(dialog,
        state->Text(L"【小狼毫】纠错界面离线演示（模拟响应）", L"Weasel: Offline correction demo (fixtures)"));
    if (state->demo) SetDlgItemTextW(dialog, IDOK, state->Text(L"关闭演示", L"Close demo"));
    state->updating = true;
    for (const auto* label : {state->Text(L"下次同一输入框开始输入时", L"At next composition"),
                              state->Text(L"检测后立即提示", L"Immediately")})
      SendDlgItemMessageW(dialog, IDC_CORRECTION_TIMING, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(dialog, IDC_CORRECTION_TIMING, CB_SETCURSEL, state->preferences.timing, 0);
    for (const auto* label : {state->Text(L"保守（90）", L"Conservative (90)"),
                            state->Text(L"标准（80）", L"Standard (80)"),
                            state->Text(L"积极（65）", L"Aggressive (65)")})
      SendDlgItemMessageW(dialog, IDC_CORRECTION_SENSITIVITY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    CheckRadioButton(dialog, IDC_CORRECTION_OFF, IDC_CORRECTION_ONLINE,
        IDC_CORRECTION_OFF + static_cast<unsigned>(state->preferences.mode));
    state->editing = state->preferences.mode == Mode::Online ? Mode::Online : Mode::Local;
    for (const auto pair : {std::pair<int, unsigned>{IDC_CORRECTION_ENDPOINT, 2048},
         {IDC_CORRECTION_MODEL, 128}, {IDC_CORRECTION_TOKEN, 2048},
         {IDC_CORRECTION_TIMEOUT, 2}, {IDC_CORRECTION_SCORE, 3},
         {IDC_CORRECTION_TEST_TEXT, static_cast<unsigned>(kMaxTargetUnits)}})
      SendDlgItemMessageW(dialog, pair.first, EM_SETLIMITTEXT, pair.second, 0);
    LoadProfile(dialog, *state);
    if (state->demo) SetDlgItemTextW(dialog, IDC_CORRECTION_TEST_TEXT, L"这里有错子。");
    const auto probe = LoadProbeResult();
    const wchar_t* zh[] = {L"尚未测试宿主上下文。", L"等待同一输入框编辑。", L"上次探测可读取邻近文字。",
        L"无可用上下文。", L"焦点改变，探测已取消。", L"输入类型未知，未读取。", L"受保护字段，未读取。",
        L"选区不可用。", L"读取失败或被宿主拒绝。"};
    const wchar_t* en[] = {L"Host context not tested.", L"Waiting for an edit in the same field.", L"Last probe could read nearby text.",
        L"No context available.", L"Focus changed; probe cancelled.", L"Unknown field; skipped.", L"Protected field; skipped.",
        L"Selection unavailable.", L"Host context read failed or was denied."};
    const auto probeIndex = static_cast<unsigned>(probe.status);
    std::wstring probeText = state->chinese ? zh[probeIndex] : en[probeIndex];
    const auto diagnostic = correction::LoadAutomaticDiagnostic();
    if (diagnostic.stage != correction::AutomaticStage::NotRun) {
      probeText = state->Text(L"上次自动检查：", L"Last automatic check: ");
      probeText += correction::AutomaticStageText(diagnostic.stage, state->chinese);
      if (diagnostic.stage == correction::AutomaticStage::FocusBlocked)
        probeText += std::wstring(L" · ") + correction::FocusStageText(diagnostic.focus, state->chinese);
      if (diagnostic.stage == correction::AutomaticStage::ScopeUnavailable ||
          diagnostic.stage == correction::AutomaticStage::ProtectedField)
        probeText += std::wstring(L" · ") + correction::ScopeStageText(correction::LoadScopeDiagnostic().stage, state->chinese);
    }
    SetDlgItemTextW(dialog, IDC_CORRECTION_PROBE_STATUS, probeText.c_str());
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
        state->demo ? state->Text(L"离线演示：点击“检查文本”查看模拟建议；不调用模型，不保存设置。",
                                  L"Offline demo: Check text for fixture suggestions. No model calls or saved settings.") :
            state->Text(L"填写模型名后可手动检查；自动提示需在左下方单独开启（仅本地模型）。",
                        L"Set a model for manual checks. Enable automatic hints separately (local only)."));
    if (!state->lexiconReady) SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
        state->Text(L"保留词表读取失败，请在“管理保留词”中清空并保存后重试。",
                    L"Kept terms could not be read. Clear/save them in Manage kept terms before checking."));
    UpdateButtons(dialog, *state);
    EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_SERVICE_RETRY), FALSE);
    EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_MODEL_CONTROL), !state->testing && !state->demo);
    SetDlgItemTextW(dialog, IDC_CORRECTION_SERVICE_STATE, state->demo || state->testing ?
        state->Text(L"离线演示/自检：不连接纠错服务。", L"Offline demo/self-test: service not contacted.") :
        state->Text(L"正在查询纠错服务状态…", L"Checking correction service status…"));
    SetDlgItemTextW(dialog, IDC_CORRECTION_SERVICE_MODEL,
        state->Text(L"本区显示已保存配置的运行状态；不会自动启动模型或发送文本。",
                    L"Status uses saved settings. This panel does not start a model or send text."));
    BeginServicePoll(dialog, *state);
    if (!state->testing) SetTimer(dialog, kPoll, 100, nullptr);
    // This interactive window is requested by the user; ignore inherited SW_HIDE.
    if (!state->testing) {
      RECT area = {};
      if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0)) {
        RECT rect = {}; GetWindowRect(dialog, &rect);
        SetWindowPos(dialog, HWND_TOP, area.left + (std::max)(0L, (area.right - area.left - rect.right + rect.left) / 2),
            area.top + (std::max)(0L, (area.bottom - area.top - rect.bottom + rect.top) / 2),
            0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
      }
    }
    return TRUE;
  }
  if (!state) return FALSE;
  if (message == WM_TIMER && wParam == kPoll) { Poll(dialog, *state); PollService(dialog, *state); return TRUE; }
  if (message == WM_CLOSE) { SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0); return TRUE; }
  if (message == WM_DESTROY) {
    KillTimer(dialog, kPoll);
    if (state->serviceJob) SetEvent(state->serviceJob->cancel);
    Cancel(*state, dialog);
    SetDlgItemTextW(dialog, IDC_CORRECTION_TOKEN, L"");
    return TRUE;
  }
  if (message != WM_COMMAND || state->updating) return FALSE;
  const int id = LOWORD(wParam), event = HIWORD(wParam);
  if (id == IDC_CORRECTION_SUGGESTIONS && event == LBN_SELCHANGE) { ShowSuggestion(dialog, *state); return TRUE; }
  if ((event == EN_CHANGE && (id == IDC_CORRECTION_TEST_TEXT || id == IDC_CORRECTION_ENDPOINT ||
       id == IDC_CORRECTION_MODEL || id == IDC_CORRECTION_TOKEN || id == IDC_CORRECTION_TIMEOUT ||
       id == IDC_CORRECTION_SCORE)) ||
      (event == CBN_SELCHANGE && (id == IDC_CORRECTION_SENSITIVITY || id == IDC_CORRECTION_TIMING)) ||
      (event == BN_CLICKED && (id == IDC_CORRECTION_CUSTOM_SCORE || id == IDC_CORRECTION_JSON_MODE || id == IDC_CORRECTION_CONSENT))) {
    Cancel(*state, dialog);
    if (event == EN_CHANGE && (id == IDC_CORRECTION_ENDPOINT || id == IDC_CORRECTION_MODEL))
      ReadProfile(dialog, *state);
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
        state->Text(L"文本或设置已改变，旧结果已清除。", L"Text/settings changed; old results cleared."));
    UpdateButtons(dialog, *state);
    return TRUE;
  }
  if (id >= IDC_CORRECTION_OFF && id <= IDC_CORRECTION_ONLINE && event == BN_CLICKED) {
    Cancel(*state, dialog); ReadProfile(dialog, *state);
    state->preferences.mode = static_cast<Mode>(id - IDC_CORRECTION_OFF);
    CheckRadioButton(dialog, IDC_CORRECTION_OFF, IDC_CORRECTION_ONLINE, id);
    if (state->preferences.mode != Mode::Off) state->editing = state->preferences.mode;
    LoadProfile(dialog, *state);
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
        state->Text(L"模式已改变；自动提示需要本地模式，并在左下方单独开启。",
                    L"Mode changed. Automatic hints require Local mode and a separate opt-in."));
    return TRUE;
  }
  switch (id) {
    case IDC_CORRECTION_SERVICE_RETRY: BeginServicePoll(dialog, *state, true); return TRUE;
    case IDC_CORRECTION_APPLY:
    case IDC_CORRECTION_IGNORE: {
      const auto index = static_cast<std::size_t>(SendDlgItemMessageW(dialog, IDC_CORRECTION_SUGGESTIONS, LB_GETCURSEL, 0, 0));
      const auto text = Text(dialog, IDC_CORRECTION_TEST_TEXT);
      const auto* selected = state->review.Selected(index, text, state->version);
      if (!selected) { Cancel(*state, dialog); UpdateButtons(dialog, *state); return TRUE; }
      const auto item = *selected;
      if (id == IDC_CORRECTION_IGNORE) {
        state->review.Ignore(index, text, state->version); RefreshSuggestions(dialog, *state);
        SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, state->Text(L"已忽略本次建议，没有保存忽略记录。", L"Ignored this suggestion without saving a rule."));
      } else {
        std::wstring corrected;
        if (!state->review.Apply(index, text, state->version, corrected)) return TRUE;
        SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_SETSEL, item.start, item.start + item.original.size());
        SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(item.replacement.c_str()));
        const bool applied = Text(dialog, IDC_CORRECTION_TEST_TEXT) == corrected;
        Cancel(*state, dialog); UpdateButtons(dialog, *state);
        if (applied && !state->testing) SetFocus(GetDlgItem(dialog, IDC_CORRECTION_TEST_TEXT));
        SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, applied ?
            state->Text(L"已修改本页文字，可 Ctrl+Z 撤销；请重新检查。", L"Updated this page. Ctrl+Z undoes; check again.") :
            state->Text(L"本页修改未完成，请检查文字后重新检查。", L"The edit did not complete. Review the text and check again."));
      }
      return TRUE;
    }
    case IDC_CORRECTION_TERMS: {
      TermDialogState terms{*state, state->lexicon};
      if (DialogBoxParamW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE)),
          MAKEINTRESOURCEW(IDD_CORRECTION_TERMS), dialog, TermDialogProc, reinterpret_cast<LPARAM>(&terms)) == IDOK) {
        Cancel(*state, dialog); UpdateButtons(dialog, *state);
        SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, state->Text(L"保留词已保存，旧结果已清除。", L"Kept terms saved; old results cleared."));
      }
      return TRUE;
    }
    case IDC_CORRECTION_AUTOMATIC: {
      AutomaticDialogState settings{*state, state->preferences.automatic};
      DialogBoxParamW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE)),
          MAKEINTRESOURCEW(IDD_CORRECTION_AUTOMATIC), dialog, AutomaticDialogProc, reinterpret_cast<LPARAM>(&settings));
      return TRUE;
    }
    case IDC_CORRECTION_APPS: {
      AppDialogState apps{*state};
      DialogBoxParamW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE)),
          MAKEINTRESOURCEW(IDD_CORRECTION_APPS), dialog, AppsDialogProc, reinterpret_cast<LPARAM>(&apps));
      return TRUE;
    }
    case IDC_CORRECTION_MODEL_CONTROL: {
      if (state->demo || state->testing) return TRUE;
      wchar_t executable[32768]{};
      const auto length = GetModuleFileNameW(nullptr, executable, _countof(executable));
      if (length && length < _countof(executable)) {
        SHELLEXECUTEINFOW launch{sizeof(launch)};
        launch.hwnd = dialog; launch.lpFile = executable; launch.lpParameters = L"/local-model"; launch.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&launch)) SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
            state->Text(L"无法打开本地模型运行窗口。", L"Could not open local model control."));
      }
      return TRUE;
    }
    case IDC_CORRECTION_CHECK: if (!state->testing) BeginCheck(dialog, *state); return TRUE;
    case IDC_CORRECTION_CANCEL_CHECK:
      Cancel(*state, dialog);
      SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
          state->Text(L"检查已取消，正在释放请求。", L"Check cancelled; releasing the request."));
      UpdateButtons(dialog, *state);
      return TRUE;
    case IDC_CORRECTION_DEFAULTS:
      Cancel(*state, dialog);
      state->preferences = Preferences{}; state->editing = Mode::Local;
      state->updating = true;
      CheckRadioButton(dialog, IDC_CORRECTION_OFF, IDC_CORRECTION_ONLINE, IDC_CORRECTION_OFF);
      SendDlgItemMessageW(dialog, IDC_CORRECTION_TIMING, CB_SETCURSEL, 0, 0);
      LoadProfile(dialog, *state);
      return TRUE;
    case IDOK:
      if (state->testing) return TRUE;
      if (state->demo) { EndDialog(dialog, IDOK); return TRUE; }
      ReadProfile(dialog, *state);
      if (state->preferences.automatic.enabled && state->preferences.mode == Mode::Local &&
          (!ValidateConfig(state->preferences.local) || !Text(dialog, IDC_CORRECTION_TOKEN).empty())) {
        MessageBoxW(dialog, state->Text(L"自动提示需要有效的本地回环接口及模型名，目前不支持保存访问密钥。",
            L"Automatic hints require a valid loopback endpoint and model, without an access token."), L"Weasel", MB_OK | MB_ICONERROR);
        return TRUE;
      }
      state->preferences.timing = static_cast<unsigned>(SendDlgItemMessageW(dialog, IDC_CORRECTION_TIMING, CB_GETCURSEL, 0, 0));
      if (!SavePreferences(state->preferences)) {
        MessageBoxW(dialog, state->Text(L"无法保存。超时需为 2–60 秒，分数需为 0–100。", L"Cannot save. Timeout must be 2–60 seconds, score 0–100."),
                    L"Weasel", MB_OK | MB_ICONERROR); return TRUE;
      }
      EndDialog(dialog, IDOK); return TRUE;
    case IDCANCEL:
      Cancel(*state, dialog);
      if (state->testing) DestroyWindow(dialog); else EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}
}  // namespace

INT_PTR ShowCorrectionSettings(HINSTANCE instance, bool offlineDemo) {
  class DemoTransport final : public IModelTransport {
   public:
    TransportResult Post(const ModelConfig&, const Endpoint&, const std::string& body,
        const std::atomic_bool& cancelled) override {
      if (cancelled.load()) return {State::Cancelled, 0, 0, {}};
      if (body.find("错子") != std::string::npos)
        return {State::NoSuggestion, 0, 200,
            R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"{\"version\":1,\"decision\":\"suggestions\",\"suggestions\":[{\"original\":\"错子\",\"replacement\":\"错字\",\"reason\":\"演示用固定响应，不是真实推理\",\"score\":0.95}]}"}}]})"};
      return {State::NoSuggestion, 0, 200,
          R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"{\"version\":1,\"decision\":\"no_change\",\"suggestions\":[]}"}}]})"};
    }
  };
  DialogState state;
  state.demo = offlineDemo;
  if (offlineDemo) {
    state.preferences.mode = Mode::Local;
    state.preferences.local.model = L"offline-fixture";
    state.testClient = []() { return CreateModelClient(Provider::ChatCompletions, std::make_unique<DemoTransport>()); };
  } else { state.preferences = LoadPreferences(); state.lexiconReady = LoadUserLexicon(state.lexicon); }
  return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_SETTINGS), nullptr,
                        DialogProc, reinterpret_cast<LPARAM>(&state));
}
// Render only this diagnostic's hidden controls, never capture the desktop.
static void SaveDialogPreview(HWND dialog, const char* filename) {
  RECT rect = {}; GetWindowRect(dialog, &rect);
  const int width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0 || width > 3000 || height > 2000) return;
  HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
  BITMAPINFO info = {}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
  void* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (memory && bitmap) {
    auto previous = SelectObject(memory, bitmap);
    SendMessageW(dialog, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_NONCLIENT | PRF_ERASEBKGND);
    const DWORD bytes = width * height * 4;
    BITMAPFILEHEADER header = {}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader); header.bfSize = header.bfOffBits + bytes;
    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char*>(pixels), bytes);
    SelectObject(memory, previous);
  }
  if (bitmap) DeleteObject(bitmap);
  if (memory) DeleteDC(memory);
  if (screen) ReleaseDC(nullptr, screen);
}
bool CheckCorrectionDialogResources(HINSTANCE instance) {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } comScope{com};
  class FixtureClient final : public IModelClient {
   public:
    CheckResult Check(const ModelConfig&, const CheckRequest& request,
        const std::atomic_bool&) override {
      Sleep(20);
      CheckResult result;
      result.session = request.session; result.version = request.version;
      result.state = State::Suggestions;
      result.suggestions.push_back({0, L"模拟", L"示例", L"离线测试建议", .95});
      return result;
    }
  };
  bool valid = true;
  std::ofstream report("CorrectionDialogChecks.log", std::ios::trunc);
  auto check = [&](bool condition, const char* label) {
    report << label << " = " << condition << '\n'; report.flush();
    valid = condition && valid;
  };
  CheckCorrectionThemes(check);
  const LANGID previous = GetThreadUILanguage();
  for (const LANGID language : {MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)}) {
    SetThreadUILanguage(language);
    check(CheckLocalModelControl(instance, [&](HWND window) {
      if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SaveDialogPreview(window, "LocalModelControlPreview.bmp");
    }), "offline local model UI never discovers files launches models or queries endpoints");
    DialogState state; state.testing = true;
    HWND dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_SETTINGS),
        nullptr, DialogProc, reinterpret_cast<LPARAM>(&state));
    report << "Language = " << language << '\n';
    if (!dialog) { report << "Create error = " << GetLastError() << '\n'; valid = false; continue; }
    for (int id : {IDC_CORRECTION_OFF, IDC_CORRECTION_LOCAL, IDC_CORRECTION_ONLINE,
        IDC_CORRECTION_ENDPOINT, IDC_CORRECTION_MODEL, IDC_CORRECTION_TOKEN, IDC_CORRECTION_TIMEOUT,
        IDC_CORRECTION_SCORE, IDC_CORRECTION_TEST_TEXT, IDC_CORRECTION_STATE, IDC_CORRECTION_RESULT,
        IDC_CORRECTION_CHECK, IDC_CORRECTION_CANCEL_CHECK, IDC_CORRECTION_CLI_RESERVED,
        IDC_CORRECTION_JSON_MODE, IDC_CORRECTION_CONSENT, IDC_CORRECTION_DEFAULTS,
        IDC_CORRECTION_SUGGESTIONS, IDC_CORRECTION_APPLY, IDC_CORRECTION_IGNORE,
        IDC_CORRECTION_TERMS, IDC_CORRECTION_AUTOMATIC, IDC_CORRECTION_APPS,
        IDC_CORRECTION_SERVICE_STATE, IDC_CORRECTION_SERVICE_MODEL, IDC_CORRECTION_SERVICE_RETRY, IDC_CORRECTION_MODEL_CONTROL})
      check(GetDlgItem(dialog, id) != nullptr, "control present");
    check(!IsWindowVisible(dialog), "hidden test window");
    AppDialogState appState{state};
    HWND appDialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_APPS), dialog,
        AppsDialogProc, reinterpret_cast<LPARAM>(&appState));
    check(appDialog && appState.loaded && appState.apps.empty(), "offline applications page never reads saved app preferences");
    if (appDialog) {
      SetDlgItemTextW(appDialog, IDC_APP_NAME, L"MsEdge.EXE");
      SendMessageW(appDialog, WM_COMMAND, IDC_APP_ADD, 0);
      check(appState.apps == std::vector<std::wstring>{L"msedge.exe"}, "applications page adds normalized executable");
      SetDlgItemTextW(appDialog, IDC_APP_NAME, L"C:\\private\\app.exe");
      SendMessageW(appDialog, WM_COMMAND, IDC_APP_ADD, 0);
      check(appState.apps.size() == 1, "applications page does not store a private filesystem path");
      SendDlgItemMessageW(appDialog, IDC_APP_LIST, LB_SETCURSEL, 0, 0);
      SendMessageW(appDialog, WM_COMMAND, IDC_APP_REMOVE, 0);
      check(appState.apps.empty(), "applications page can restore correction by removing pause");
      SendMessageW(appDialog, WM_COMMAND, IDC_APP_CLEAR_FEEDBACK, 0);
      check(Text(appDialog, IDC_APP_STATE) == state.Text(L"纠错使用记录已清除。", L"Correction usage history cleared."),
          "offline data cleanup operates without writing real feedback or dictionaries");
      CheckDlgButton(appDialog, IDC_APP_BEHAVIOR_ENABLED, BST_UNCHECKED);
      SendMessageW(appDialog, WM_COMMAND, IDC_APP_BEHAVIOR_ENABLED, 0);
      check(IsDlgButtonChecked(appDialog, IDC_APP_BEHAVIOR_ENABLED) == BST_UNCHECKED &&
          Text(appDialog, IDC_APP_STATE) == state.Text(L"行为学习开关已立即保存；不改变模型参数。", L"Usage learning choice saved immediately; model parameters unchanged."),
          "usage learning opt-out has an independent visible confirmation without real preference writes");
      appState.loaded = false;
      SendMessageW(appDialog, WM_COMMAND, IDC_APP_RESET, 0);
      check(appState.loaded && appState.apps.empty() && IsWindowEnabled(GetDlgItem(appDialog, IDOK)),
          "application policy has an explicit recovery path without changing global correction mode");
      if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SaveDialogPreview(appDialog, "CorrectionAppsPreview.bmp");
      DestroyWindow(appDialog);
    }
    check(!state.serviceJob && !IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_SERVICE_RETRY)),
        "offline UI test never queries/restarts installed backend");
    ServiceJob service;
    service.available = true;
    for (auto stage : {BrokerProcessStage::Starting, BrokerProcessStage::Backoff, BrokerProcessStage::Exhausted}) {
      service.service.stage = static_cast<DWORD>(stage);
      service.service.retryAvailable = stage == BrokerProcessStage::Exhausted;
      ShowService(dialog, state, service);
      check(IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_SERVICE_RETRY)) == (stage == BrokerProcessStage::Exhausted),
          "manual recovery offered only after automatic retries exhausted");
    }
    service.service.stage = static_cast<DWORD>(BrokerProcessStage::Running);
    service.endpoint = false;
    ShowService(dialog, state, service);
    const auto notReady = Text(dialog, IDC_CORRECTION_SERVICE_STATE);
    service.endpoint = true; service.health.status = BrokerStatus::Ok;
    service.health.settings.enabled = 1; service.health.modelState = static_cast<DWORD>(State::TransportError);
    ShowService(dialog, state, service);
    check(Text(dialog, IDC_CORRECTION_SERVICE_STATE) != notReady &&
        Text(dialog, IDC_CORRECTION_SERVICE_MODEL).find(StateText(State::TransportError, state.chinese)) != std::wstring::npos &&
        !IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_SERVICE_RETRY)),
        "model unreachable differs from coordinator endpoint failure and cannot restart a live coordinator");
    service.health.modelState = static_cast<DWORD>(State::NoSuggestion);
    ShowService(dialog, state, service);
    check(Text(dialog, IDC_CORRECTION_SERVICE_MODEL).find(StateText(State::NoSuggestion, state.chinese)) != std::wstring::npos,
        "successful no-suggestion is distinct from model failure");
    AutomaticDialogState automatic{state, state.preferences.automatic, true};
    HWND autoDialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_AUTOMATIC), dialog,
        AutomaticDialogProc, reinterpret_cast<LPARAM>(&automatic));
    check(autoDialog != nullptr, "automatic layout dialog created");
    if (autoDialog) {
      check(IsDlgButtonChecked(autoDialog, IDC_AUTO_ENABLED) == BST_UNCHECKED &&
          SendDlgItemMessageW(autoDialog, IDC_AUTO_SIDE, CB_GETCOUNT, 0, 0) == 3 &&
          SendDlgItemMessageW(autoDialog, IDC_AUTO_SIDE, CB_GETCURSEL, 0, 0) == 0,
          "automatic opt-in defaults and layout choices");
      if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SaveDialogPreview(autoDialog, "CorrectionAutomaticPreview.bmp");
      check(GetDlgItem(autoDialog, IDC_AUTO_PREVIEW) != nullptr, "offline preview control present");
      SendMessageW(autoDialog, WM_COMMAND, IDC_AUTO_PREVIEW, 0);
      check(automatic.preview.Window() && !IsWindowVisible(automatic.preview.Window()), "preview renders without model or focus in hidden test");
      IAccessible* accessible = nullptr;
      const auto access = AccessibleObjectFromWindow(automatic.preview.Window(), OBJID_CLIENT, IID_IAccessible,
          reinterpret_cast<void**>(&accessible));
      VARIANT child{}; child.vt = VT_I4; child.lVal = CHILDID_SELF;
      VARIANT role{}, accState{}; BSTR name = nullptr;
      check(access == S_OK && accessible && accessible->get_accRole(child, &role) == S_OK &&
          role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_TOOLTIP, "suggestion exposes standard accessible tooltip role");
      check(accessible && accessible->get_accState(child, &accState) == S_OK && accState.vt == VT_I4 &&
          (accState.lVal & STATE_SYSTEM_READONLY) && !(accState.lVal & STATE_SYSTEM_FOCUSABLE),
          "suggestion accessibility never makes the hint an editable focus target");
      check(accessible && accessible->get_accName(child, &name) == S_OK && name && SysStringLen(name),
          "suggestion preview has an accessible name");
      if (name) SysFreeString(name); if (accessible) accessible->Release();
      RECT initialPreview{}; GetWindowRect(automatic.preview.Window(), &initialPreview);
      SetDlgItemInt(autoDialog, IDC_AUTO_FONT, 24, FALSE);
      RECT largePreview{}; GetWindowRect(automatic.preview.Window(), &largePreview);
      check(largePreview.bottom - largePreview.top > initialPreview.bottom - initialPreview.top,
          "preview responds to font changes");
      if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SaveDialogPreview(automatic.preview.Window(), "CorrectionHintPreview.bmp");
      if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) {
        weasel::UIStyle theme;
        theme.back_color = 0xff202020; theme.candidate_text_color = 0xffeeeeee;
        theme.hilited_candidate_back_color = 0xfff09040; theme.border_color = 0xff808080;
        automatic.preview.SetStyleSnapshot(theme);
        SaveDialogPreview(automatic.preview.Window(), "CorrectionHintDarkPreview.bmp");
        theme.back_color = 0xfff8f8f8; theme.candidate_text_color = 0xff222222;
        automatic.preview.SetStyleSnapshot(theme);
        SaveDialogPreview(automatic.preview.Window(), "CorrectionHintLightPreview.bmp");
        check(!IsWindowVisible(automatic.preview.Window()), "theme snapshots do not activate or display the hidden preview");
      }
      SetDlgItemInt(autoDialog, IDC_AUTO_FONT, 14, FALSE);
      SetDlgItemTextW(autoDialog, IDC_AUTO_GAP, L"");
      SendMessageW(autoDialog, WM_COMMAND, IDOK, 0);
      check(IsWindow(autoDialog) && !state.preferences.automatic.enabled && !IsWindowVisible(automatic.preview.Window()),
          "empty numeric parameter is rejected and pauses preview");
      SetDlgItemInt(autoDialog, IDC_AUTO_GAP, 8, FALSE);
      SetDlgItemTextW(autoDialog, IDC_AUTO_WIDTH, L"320x");
      SendMessageW(autoDialog, WM_COMMAND, IDOK, 0);
      check(IsWindow(autoDialog) && !state.preferences.automatic.enabled, "numeric paste suffix is rejected without saving");
      SetDlgItemInt(autoDialog, IDC_AUTO_WIDTH, 1, FALSE);
      SendMessageW(autoDialog, WM_COMMAND, IDOK, 0);
      check(IsWindow(autoDialog) && !state.preferences.automatic.enabled, "invalid layout cannot be accepted");
      SetDlgItemInt(autoDialog, IDC_AUTO_WIDTH, 400, FALSE);
      CheckDlgButton(autoDialog, IDC_AUTO_ENABLED, BST_CHECKED);
      SendMessageW(autoDialog, WM_COMMAND, IDCANCEL, 0);
      check(!state.preferences.automatic.enabled && !state.preferences.automatic.wordLocalCompatibility &&
          !automatic.preview.Window(), "layout cancel leaves options unchanged and closes preview");
      autoDialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_AUTOMATIC), dialog,
          AutomaticDialogProc, reinterpret_cast<LPARAM>(&automatic));
      if (autoDialog) {
        CheckDlgButton(autoDialog, IDC_AUTO_ENABLED, BST_CHECKED);
        SetDlgItemInt(autoDialog, IDC_AUTO_WIDTH, 400, FALSE);
        SendMessageW(autoDialog, WM_COMMAND, IDOK, 0);
        check(state.preferences.automatic.enabled && state.preferences.automatic.width == 400 &&
            !state.preferences.automatic.wordLocalCompatibility && !IsWindow(autoDialog),
            "layout confirmation changes parent memory only");
        state.preferences.automatic = AutomaticOptions{};
      } else check(false, "automatic layout recreated");
    }
    check(!IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_CHECK)) &&
        IsDlgButtonChecked(dialog, IDC_CORRECTION_OFF) == BST_CHECKED, "off default");
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_CORRECTION_LOCAL, BN_CLICKED), 0);
    check(state.preferences.mode == Mode::Local && IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_CHECK)), "local manual check enabled");
    // Exercise the production worker/poll/version flow with an in-memory client.
    state.testClient = []() { return std::make_unique<FixtureClient>(); };
    SetDlgItemTextW(dialog, IDC_CORRECTION_MODEL, L"fixture");
    SetDlgItemTextW(dialog, IDC_CORRECTION_TEST_TEXT, L"模拟文本");
    BeginCheck(dialog, state);
    check(state.job != nullptr, "fixture job started");
    for (unsigned attempt = 0; state.job && !state.job->done.load() && attempt < 200; ++attempt) Sleep(1);
    Poll(dialog, state);
    check(Text(dialog, IDC_CORRECTION_RESULT).find(L"示例") != std::wstring::npos, "fixture result displayed");
    if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SaveDialogPreview(dialog, "CorrectionDialogPreview.bmp");
    check(IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_APPLY)), "current suggestion is actionable");
    SendMessageW(dialog, WM_COMMAND, IDC_CORRECTION_APPLY, 0);
    check(Text(dialog, IDC_CORRECTION_TEST_TEXT) == L"示例文本" && state.review.Items().empty(), "apply changes own text and invalidates results");
    check(SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_CANUNDO, 0, 0) != 0 &&
        SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_UNDO, 0, 0) != 0 &&
        Text(dialog, IDC_CORRECTION_TEST_TEXT) == L"模拟文本", "apply uses native undo");
    auto runFixture = [&]() {
      BeginCheck(dialog, state);
      for (unsigned attempt = 0; state.job && !state.job->done.load() && attempt < 200; ++attempt) Sleep(1);
      Poll(dialog, state);
    };
    runFixture(); SendMessageW(dialog, WM_COMMAND, IDC_CORRECTION_IGNORE, 0);
    check(state.review.Items().empty() && state.lexicon.terms.empty(), "ignore does not learn expression");
    runFixture(); SendMessageW(dialog, WM_COMMAND, IDC_CORRECTION_KEEP, 0);
    check(state.lexicon.terms.empty() && !GetDlgItem(dialog, IDC_CORRECTION_KEEP), "retired keep action cannot store an expression");
    KeepTerm(state.lexicon, L"模拟"); // Existing user-managed entries remain removable and compatible.
    runFixture();
    check(state.review.Items().empty() && Text(dialog, IDC_CORRECTION_RESULT).empty(), "kept original suppresses repeated false positive");
    TermDialogState terms{state, state.lexicon, true};
    HWND termDialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_TERMS),
        dialog, TermDialogProc, reinterpret_cast<LPARAM>(&terms));
    check(termDialog && !IsWindowVisible(termDialog), "term manager hidden resources available");
    if (termDialog) {
      for (int id : {IDC_CORRECTION_TERM_LIST, IDC_CORRECTION_TERM_TEXT, IDC_CORRECTION_TERM_ADD,
          IDC_CORRECTION_TERM_REMOVE, IDC_CORRECTION_TERM_CLEAR, IDC_CORRECTION_TERM_STATE, IDOK, IDCANCEL})
        check(GetDlgItem(termDialog, id) != nullptr, "term manager control present");
      SetDlgItemTextW(termDialog, IDC_CORRECTION_TERM_TEXT, L"小狼毫");
      SendMessageW(termDialog, WM_COMMAND, IDC_CORRECTION_TERM_ADD, 0);
      if (language == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SaveDialogPreview(termDialog, "CorrectionTermsPreview.bmp");
      SendDlgItemMessageW(termDialog, IDC_CORRECTION_TERM_LIST, LB_SETCURSEL, 1, 0);
      SendMessageW(termDialog, WM_COMMAND, IDC_CORRECTION_TERM_REMOVE, 0);
      check(terms.working.terms == state.lexicon.terms, "term manager removes selected entry only");
      SetDlgItemTextW(termDialog, IDC_CORRECTION_TERM_TEXT, L"另一个词");
      SendMessageW(termDialog, WM_COMMAND, IDC_CORRECTION_TERM_ADD, 0);
      SendMessageW(termDialog, WM_COMMAND, IDCANCEL, 0);
      check(state.lexicon.terms.size() == 1, "term manager cancel discards edits");
    }
    terms.working = state.lexicon;
    termDialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_TERMS),
        dialog, TermDialogProc, reinterpret_cast<LPARAM>(&terms));
    if (termDialog) {
      SendMessageW(termDialog, WM_COMMAND, IDC_CORRECTION_TERM_CLEAR, 0);
      SendMessageW(termDialog, WM_COMMAND, IDOK, 0);
      check(state.lexicon.terms.empty(), "term manager clear saved in test memory only");
    } else check(false, "term manager create for clear");
    BeginCheck(dialog, state);
    SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_SETSEL, 0, -1);
    SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"文本已改变"));
    for (unsigned attempt = 0; state.job && !state.job->done.load() && attempt < 200; ++attempt) Sleep(1);
    Poll(dialog, state);
    check(Text(dialog, IDC_CORRECTION_RESULT).empty(), "stale result discarded");
    state.job = std::make_shared<Job>();
    const auto version = state.version;
    SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_SETSEL, 0, -1);
    SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"自有模拟文本"));
    check(state.version > version && state.job->cancelled.load(), "edit cancels job");
    state.job->done.store(true); Poll(dialog, state);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_CORRECTION_ONLINE, BN_CLICKED), 0);
    SetDlgItemTextW(dialog, IDC_CORRECTION_ENDPOINT, L"https://example.com/v1/chat/completions");
    SetDlgItemTextW(dialog, IDC_CORRECTION_MODEL, L"fixture");
    CheckDlgButton(dialog, IDC_CORRECTION_CONSENT, BST_CHECKED);
    CheckDlgButton(dialog, IDC_CORRECTION_CUSTOM_SCORE, BST_CHECKED);
    SetDlgItemTextW(dialog, IDC_CORRECTION_ENDPOINT, L"https://example.org/v1/chat/completions");
    check(IsDlgButtonChecked(dialog, IDC_CORRECTION_CONSENT) == BST_UNCHECKED &&
        IsDlgButtonChecked(dialog, IDC_CORRECTION_CUSTOM_SCORE) == BST_UNCHECKED,
        "endpoint change resets consent and threshold");
    CheckDlgButton(dialog, IDC_CORRECTION_CUSTOM_SCORE, BST_CHECKED);
    SetDlgItemTextW(dialog, IDC_CORRECTION_SCORE, L"0.9");
    ReadProfile(dialog, state);
    check(state.Profile().minimumScore < 0, "decimal score cannot silently become a zero threshold");
    CheckDlgButton(dialog, IDC_CORRECTION_CUSTOM_SCORE, BST_UNCHECKED);
    ReadProfile(dialog, state);
    check(!state.Profile().useCustomThreshold && state.Profile().minimumScore == .90,
        "invalid inactive score draft cannot block using the default threshold");
    SendMessageW(dialog, WM_COMMAND, IDC_CORRECTION_DEFAULTS, 0);
    check(state.preferences.mode == Mode::Off && !IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_CHECK)), "defaults disable checks");
    state.serviceJob = std::make_shared<ServiceJob>();
    const auto closingService = state.serviceJob;
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    check(!IsWindow(dialog) && WaitForSingleObject(closingService->cancel, 0) == WAIT_OBJECT_0,
        "closing dialog cancels its management request without callbacks to destroyed window");
  }
  SetThreadUILanguage(previous);
  return valid;
}

bool CheckCorrectionLocalDialog(HINSTANCE instance) {
  DialogState state; state.testing = true;
  HWND dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_SETTINGS),
      nullptr, DialogProc, reinterpret_cast<LPARAM>(&state));
  if (!dialog) return false;
  bool valid = !IsWindowVisible(dialog);
  std::ofstream report("CorrectionLocalDialogChecks.log", std::ios::trunc);
  SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_CORRECTION_LOCAL, BN_CLICKED), 0);
  SetDlgItemTextW(dialog, IDC_CORRECTION_ENDPOINT, L"http://127.0.0.1:18963/v1/chat/completions");
  SetDlgItemTextW(dialog, IDC_CORRECTION_MODEL, L"weasel-qwen3-4b");
  SetDlgItemInt(dialog, IDC_CORRECTION_TIMEOUT, 10, FALSE);
  for (const auto* text : {L"你好。", L"这里有错子。"}) {
    SetDlgItemTextW(dialog, IDC_CORRECTION_TEST_TEXT, text);
    BeginCheck(dialog, state);
    const auto start = GetTickCount64();
    while (state.job && !state.job->done.load() && GetTickCount64() - start < 12000) Sleep(10);
    bool passed = state.job && state.job->done.load();
    if (passed) passed = wcscmp(text, L"你好。") == 0 ? state.job->result.state == State::NoSuggestion :
        state.job->result.state == State::Suggestions && !state.job->result.suggestions.empty();
    Poll(dialog, state);
    if (wcscmp(text, L"你好。") != 0)
      passed = passed && Text(dialog, IDC_CORRECTION_RESULT).find(L"错字") != std::wstring::npos;
    report << "local worker/result display = " << passed << '\n';
    valid = passed && valid;
  }
  SendMessageW(dialog, WM_COMMAND, IDC_CORRECTION_APPLY, 0);
  const bool applied = Text(dialog, IDC_CORRECTION_TEST_TEXT) == L"这里有错字。" && state.review.Items().empty();
  report << "real suggestion applied to own page = " << applied << '\n'; valid = valid && applied;
  const bool undo = SendDlgItemMessageW(dialog, IDC_CORRECTION_TEST_TEXT, EM_UNDO, 0, 0) != 0 &&
      Text(dialog, IDC_CORRECTION_TEST_TEXT) == L"这里有错子。";
  report << "real suggestion edit undo = " << undo << '\n'; valid = valid && undo;
  BeginCheck(dialog, state);
  const auto keepStart = GetTickCount64();
  while (state.job && !state.job->done.load() && GetTickCount64() - keepStart < 12000) Sleep(10);
  Poll(dialog, state); KeepTerm(state.lexicon, L"错子");
  const bool kept = std::find(state.lexicon.terms.begin(), state.lexicon.terms.end(), L"错子") != state.lexicon.terms.end();
  report << "legacy kept expression loaded in test memory = " << kept << '\n'; valid = valid && kept;
  BeginCheck(dialog, state);
  const auto repeatStart = GetTickCount64();
  while (state.job && !state.job->done.load() && GetTickCount64() - repeatStart < 12000) Sleep(10);
  const bool completed = state.job && state.job->done.load() && state.job->result.state == State::Suggestions &&
      !state.job->result.suggestions.empty();
  Poll(dialog, state);
  const bool suppressed = completed && state.review.Items().empty() && Text(dialog, IDC_CORRECTION_RESULT).empty();
  report << "kept term suppresses subsequent real model suggestion = " << suppressed << '\n'; valid = valid && suppressed;
  BeginCheck(dialog, state);
  const auto job = state.job;
  SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
  bool closed = !IsWindow(dialog) && job && job->cancelled.load();
  const auto start = GetTickCount64();
  while (job && !job->done.load() && GetTickCount64() - start < 3000) Sleep(10);
  closed = closed && job && job->done.load();
  report << "close cancels real HTTP worker = " << closed << '\n';
  return closed && valid;
}
