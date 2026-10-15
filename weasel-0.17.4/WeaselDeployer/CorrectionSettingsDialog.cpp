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
#include "Resource.h"
#include "CorrectionPreferences.h"
#include "CorrectionSettingsDialog.h"

namespace {
using namespace correction;
constexpr UINT_PTR kPoll = 1;
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
  std::uint64_t version = 1;
  bool updating = false;
  bool testing = false;
  bool demo = false;
  std::function<std::unique_ptr<IModelClient>()> testClient;
  bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
  const wchar_t* Text(const wchar_t* zh, const wchar_t* en) const { return chinese ? zh : en; }
  ModelConfig& Profile() { return editing == Mode::Online ? preferences.online : preferences.local; }
};
std::wstring Text(HWND window, int id) {
  const HWND control = GetDlgItem(window, id);
  const int count = GetWindowTextLengthW(control);
  std::wstring text(count + 1, L'\0');
  GetWindowTextW(control, text.data(), count + 1);
  text.resize(count);
  return text;
}
void SetNumber(HWND dialog, int id, unsigned value) { SetDlgItemInt(dialog, id, value, FALSE); }
void Cancel(DialogState& state, HWND dialog) {
  ++state.version;
  if (state.job && !state.job->done.load()) state.job->cancelled.store(true);
  SetDlgItemTextW(dialog, IDC_CORRECTION_RESULT, L"");
}
void ReadProfile(HWND dialog, DialogState& state) {
  auto& profile = state.Profile();
  const auto endpoint = Text(dialog, IDC_CORRECTION_ENDPOINT);
  const auto model = Text(dialog, IDC_CORRECTION_MODEL);
  const bool identityChanged = endpoint != profile.endpoint || model != profile.model;
  profile.endpoint = endpoint; profile.model = model;
  profile.timeoutSeconds = GetDlgItemInt(dialog, IDC_CORRECTION_TIMEOUT, nullptr, FALSE);
  const auto selection = SendDlgItemMessageW(dialog, IDC_CORRECTION_SENSITIVITY, CB_GETCURSEL, 0, 0);
  profile.sensitivity = selection >= 0 ? static_cast<unsigned>(selection) : 0;
  profile.useCustomThreshold = IsDlgButtonChecked(dialog, IDC_CORRECTION_CUSTOM_SCORE) == BST_CHECKED;
  BOOL scoreValid = FALSE;
  const unsigned score = GetDlgItemInt(dialog, IDC_CORRECTION_SCORE, &scoreValid, FALSE);
  profile.minimumScore = scoreValid ? score / 100.0 : -1;
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
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_CHECK), enabled && !busy);
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_CANCEL_CHECK), busy);
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_SCORE),
      IsDlgButtonChecked(dialog, IDC_CORRECTION_CUSTOM_SCORE) == BST_CHECKED);
  EnableWindow(GetDlgItem(dialog, IDC_CORRECTION_CONSENT), state.preferences.mode == Mode::Online);
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
    std::wstring status = StateText(job->result.state, state.chinese);
    status += L" (" + std::to_wstring(GetTickCount64() - job->started) + L" ms)";
    if (state.demo) status = state.Text(L"离线模拟结果：", L"Offline fixture: ") + status;
    if (job->result.httpStatus && job->result.httpStatus != 200)
      status += L" HTTP " + std::to_wstring(job->result.httpStatus);
    if (job->result.nativeError) status += L" (" + std::to_wstring(job->result.nativeError) + L")";
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE, status.c_str());
    std::wstring display;
    for (const auto& item : job->result.suggestions) {
      display += item.original + L" → " + item.replacement + L"\r\n" + item.reason + L"\r\n";
      display += state.Text(L"模型分数（未校准）：", L"Model score (uncalibrated): ");
      display += std::to_wstring(static_cast<unsigned>(std::lround(item.score * 100))) + L"/100\r\n\r\n";
    }
    SetDlgItemTextW(dialog, IDC_CORRECTION_RESULT, display.c_str());
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
    for (const auto* label : {state->Text(L"下次同一场景输入时（预留）", L"At next input (reserved)"),
                              state->Text(L"检测后立即提示（预留）", L"Immediately (reserved)")})
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
    const std::wstring probeText = state->chinese ? zh[probeIndex] : en[probeIndex];
    SetDlgItemTextW(dialog, IDC_CORRECTION_PROBE_STATUS, probeText.c_str());
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
        state->demo ? state->Text(L"离线演示：点击“检查文本”查看模拟建议；不调用模型，不保存设置。",
                                  L"Offline demo: Check text for fixture suggestions. No model calls or saved settings.") :
            state->Text(L"未连接。填写模型名，选择模式后可手动检查；不会自动读取其他应用。",
                        L"Not connected. Choose mode and model for manual checks; other apps are not read."));
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
  if (message == WM_TIMER && wParam == kPoll) { Poll(dialog, *state); return TRUE; }
  if (message == WM_CLOSE) { SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0); return TRUE; }
  if (message == WM_DESTROY) {
    KillTimer(dialog, kPoll);
    Cancel(*state, dialog);
    SetDlgItemTextW(dialog, IDC_CORRECTION_TOKEN, L"");
    return TRUE;
  }
  if (message != WM_COMMAND || state->updating) return FALSE;
  const int id = LOWORD(wParam), event = HIWORD(wParam);
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
    if (state->preferences.mode != Mode::Off) state->editing = state->preferences.mode;
    LoadProfile(dialog, *state);
    SetDlgItemTextW(dialog, IDC_CORRECTION_STATE,
        state->Text(L"模式已改变；仅影响本页手动检查，尚未启用自动纠错。",
                    L"Mode changed for manual checks only. Automatic correction is not enabled."));
    return TRUE;
  }
  switch (id) {
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
  } else state.preferences = LoadPreferences();
  return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_SETTINGS), nullptr,
                        DialogProc, reinterpret_cast<LPARAM>(&state));
}
bool CheckCorrectionDialogResources(HINSTANCE instance) {
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
  const LANGID previous = GetThreadUILanguage();
  for (const LANGID language : {MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)}) {
    SetThreadUILanguage(language);
    DialogState state; state.testing = true;
    HWND dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_CORRECTION_SETTINGS),
        nullptr, DialogProc, reinterpret_cast<LPARAM>(&state));
    report << "Language = " << language << '\n';
    if (!dialog) { report << "Create error = " << GetLastError() << '\n'; valid = false; continue; }
    for (int id : {IDC_CORRECTION_OFF, IDC_CORRECTION_LOCAL, IDC_CORRECTION_ONLINE,
        IDC_CORRECTION_ENDPOINT, IDC_CORRECTION_MODEL, IDC_CORRECTION_TOKEN, IDC_CORRECTION_TIMEOUT,
        IDC_CORRECTION_SCORE, IDC_CORRECTION_TEST_TEXT, IDC_CORRECTION_STATE, IDC_CORRECTION_RESULT,
        IDC_CORRECTION_CHECK, IDC_CORRECTION_CANCEL_CHECK, IDC_CORRECTION_CLI_RESERVED,
        IDC_CORRECTION_JSON_MODE, IDC_CORRECTION_CONSENT, IDC_CORRECTION_DEFAULTS})
      check(GetDlgItem(dialog, id) != nullptr, "control present");
    check(!IsWindowVisible(dialog), "hidden test window");
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
    SendMessageW(dialog, WM_COMMAND, IDC_CORRECTION_DEFAULTS, 0);
    check(state.preferences.mode == Mode::Off && !IsWindowEnabled(GetDlgItem(dialog, IDC_CORRECTION_CHECK)), "defaults disable checks");
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    check(!IsWindow(dialog), "window destroyed");
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
