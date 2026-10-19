#pragma once
#include <windows.h>
#include <msctf.h>
#include <inputscope.h>
#include <atlcomcli.h>

namespace correction {
enum class ScopeStage : DWORD {
  NotRun, Ready, MissingTarget, PasswordStyle, PropertyQuery, MissingProperty,
  PropertyValue, ValueType, ScopeInterface, ScopeQuery, EmptyScopes,
  TooManyScopes, UnsupportedScope, ProtectedScope, KeyboardUnavailable,
  KeyboardDisabled, EmptyContext, ReadOnly, StatusUnavailable
};
enum class ScopeUse { NearbyText, LocalCommit };
struct ScopeDiagnostic {
  DWORD version = 1;
  ScopeStage stage = ScopeStage::NotRun;
  HRESULT error = S_OK;
  LONG scope = 0;
  DWORD variantType = VT_EMPTY;
  explicit operator bool() const { return stage == ScopeStage::Ready; }
};
static_assert(sizeof(ScopeDiagnostic) == 20, "Scope diagnostic wire size");
// A default scope is usable only for this IME's tracked local commits, never as
// permission to read arbitrary nearby/history text. Query failures stay closed.
inline ScopeDiagnostic CommittedInputState(ITfContext* context) {
  ScopeDiagnostic result;
  if (!context) { result.stage = ScopeStage::MissingTarget; result.error = E_POINTER; return result; }
  TF_STATUS status{};
  result.error = context->GetStatus(&status);
  if (result.error != S_OK) { result.stage = ScopeStage::StatusUnavailable; return result; }
  if (status.dwDynamicFlags & TF_SD_READONLY) { result.stage = ScopeStage::ReadOnly; return result; }
  CComPtr<ITfCompartmentMgr> manager;
  result.error = context->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(&manager));
  if (result.error != S_OK || !manager) { result.stage = ScopeStage::KeyboardUnavailable; return result; }
  const GUID* flags[] = {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT};
  for (unsigned i = 0; i < 2; ++i) {
    CComPtr<ITfCompartment> compartment;
    result.error = manager->GetCompartment(*flags[i], &compartment);
    if (result.error != S_OK || !compartment) { result.stage = ScopeStage::KeyboardUnavailable; return result; }
    VARIANT value; VariantInit(&value);
    result.error = compartment->GetValue(&value);
    const bool unset = (result.error == S_OK || result.error == S_FALSE) && value.vt == VT_EMPTY;
    const bool known = result.error == S_OK && value.vt == VT_I4;
    const bool disabled = known && value.lVal != 0;
    VariantClear(&value);
    if (!unset && !known) { result.stage = ScopeStage::KeyboardUnavailable; return result; }
    if (disabled) { result.stage = i ? ScopeStage::EmptyContext : ScopeStage::KeyboardDisabled; return result; }
  }
  result.stage = ScopeStage::Ready; result.error = S_OK; return result;
}
inline ScopeDiagnostic InspectInputScope(ITfContext* context, TfEditCookie cookie, ITfRange* range,
                                          ScopeUse use = ScopeUse::NearbyText) {
  ScopeDiagnostic result;
  if (!context || !range) { result.stage = ScopeStage::MissingTarget; result.error = E_POINTER; return result; }
  CComPtr<ITfContextView> view;
  HWND window = nullptr;
  if (context->GetActiveView(&view) == S_OK && view && view->GetWnd(&window) == S_OK && window &&
      (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD)) {
    result.stage = ScopeStage::PasswordStyle; return result;
  }
  if (use == ScopeUse::LocalCommit) {
    result = CommittedInputState(context);
    if (!result) return result;
    result.stage = ScopeStage::NotRun;
  }
  CComPtr<ITfReadOnlyProperty> property;
  result.error = context->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
  if (result.error != S_OK) { result.stage = ScopeStage::PropertyQuery; return result; }
  if (!property) { result.stage = ScopeStage::MissingProperty; return result; }
  VARIANT value; VariantInit(&value);
  result.error = property->GetValue(cookie, range, &value);
  result.variantType = value.vt;
  CComPtr<ITfInputScope> input;
  if (result.error != S_OK) result.stage = ScopeStage::PropertyValue;
  else if (value.vt != VT_UNKNOWN || !value.punkVal) result.stage = ScopeStage::ValueType;
  else {
    result.error = value.punkVal->QueryInterface(IID_ITfInputScope, reinterpret_cast<void**>(&input));
    if (result.error != S_OK || !input) result.stage = ScopeStage::ScopeInterface;
  }
  VariantClear(&value);
  if (result.stage != ScopeStage::NotRun || !input) return result;
  InputScope* scopes = nullptr; UINT count = 0;
  result.error = input->GetInputScopes(&scopes, &count);
  if (result.error != S_OK) result.stage = ScopeStage::ScopeQuery;
  else if (!scopes || !count) result.stage = ScopeStage::EmptyScopes;
  else if (count > 64) result.stage = ScopeStage::TooManyScopes;
  else {
    result.stage = ScopeStage::Ready;
    for (UINT i = 0; i < count; ++i) {
      const auto scope = scopes[i];
      if (scope == IS_PASSWORD || scope == IS_NUMERIC_PASSWORD || scope == IS_PRIVATE ||
          scope == IS_NUMERIC_PIN || scope == IS_ALPHANUMERIC_PIN || scope == IS_ALPHANUMERIC_PIN_SET) {
        result.stage = ScopeStage::ProtectedScope; result.scope = scope; break;
      }
      if (scope != IS_TEXT && !(use == ScopeUse::LocalCommit && scope == IS_DEFAULT) && result.stage == ScopeStage::Ready) {
        result.stage = ScopeStage::UnsupportedScope; result.scope = scope;
      }
    }
  }
  CoTaskMemFree(scopes);
  return result;
}
inline bool ProtectedScope(const ScopeDiagnostic& result) {
  return result.stage == ScopeStage::PasswordStyle || result.stage == ScopeStage::ProtectedScope ||
      result.stage == ScopeStage::KeyboardDisabled || result.stage == ScopeStage::EmptyContext;
}
inline const wchar_t* ScopeStageText(ScopeStage stage, bool chinese) {
  const wchar_t* zh[] = {L"尚无范围记录",L"允许的输入范围",L"无目标范围",L"密码输入框",L"范围属性查询失败",
    L"宿主未提供范围属性",L"范围属性读取失败",L"范围属性类型不可用",L"范围接口不可用",
    L"范围类型查询失败",L"范围类型为空",L"范围类型数量异常",L"宿主类型不在文本白名单",L"私密/密码/PIN 字段",L"输入许可状态不可确认",L"宿主禁止输入",L"无有效输入字段",L"只读字段",L"宿主状态不可用"};
  const wchar_t* en[] = {L"No scope record",L"Allowed input scope",L"Missing target",L"Password control",
    L"Scope property query failed",L"Scope property absent",L"Scope property read failed",L"Scope value type unavailable",
    L"Scope interface unavailable",L"Scope query failed",L"Empty scope list",L"Oversized scope list",
    L"Scope not in text allowlist",L"Private/password/PIN field",L"Keyboard state unavailable",L"Keyboard disabled",L"Empty input context",L"Read-only field",L"Host status unavailable"};
  const auto index = static_cast<unsigned>(stage);
  if (index > static_cast<unsigned>(ScopeStage::StatusUnavailable)) return chinese ? L"未知" : L"unknown";
  return chinese ? zh[index] : en[index];
}
}  // namespace correction
