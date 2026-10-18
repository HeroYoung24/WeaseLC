#pragma once
#include <windows.h>
#include <msctf.h>
#include <atlcomcli.h>

namespace correction {
// Interface addresses are not object identities (COM tear-off interfaces).
inline bool SameIdentity(IUnknown* left, IUnknown* right) {
  if (!left || !right) return left == right;
  if (left == right) return true;
  CComPtr<IUnknown> a, b;
  return SUCCEEDED(left->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a))) && a &&
      SUCCEEDED(right->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b))) && b && a == b;
}
enum class FocusStage : DWORD {
  Ready, MissingContext, MissingManager, QueryFailed, NoDocument,
  TopFailed, NoTop, DifferentContext, ViewFailed, WindowFailed, DifferentWindow
};
struct FocusResult {
  FocusStage stage = FocusStage::Ready;
  HRESULT error = S_OK;
  explicit operator bool() const { return stage == FocusStage::Ready; }
};
inline const wchar_t* FocusStageText(FocusStage stage, bool chinese) {
  const wchar_t* zh[] = {L"就绪",L"无编辑上下文",L"无线程管理器",L"焦点查询失败",L"无焦点文档",
    L"编辑框查询失败",L"文档无编辑框",L"已切换编辑框",L"编辑视图不可用",L"宿主窗口不可用",L"宿主不在前台"};
  const wchar_t* en[] = {L"ready",L"no context",L"no manager",L"focus query failed",L"no focused document",
    L"context query failed",L"no context in document",L"different context",L"view unavailable",L"host window unavailable",L"host not foreground"};
  const auto index = static_cast<unsigned>(stage);
  if (index > static_cast<unsigned>(FocusStage::DifferentWindow)) return chinese ? L"未知" : L"unknown";
  return chinese ? zh[index] : en[index];
}
inline FocusResult CheckContextFocus(ITfThreadMgr* manager, ITfContext* target,
                                     bool checkWindow = false, HWND foreground = nullptr) {
  if (!target) return {FocusStage::MissingContext, E_POINTER};
  if (!manager) return {FocusStage::MissingManager, E_POINTER};
  CComPtr<ITfDocumentMgr> document;
  HRESULT hr = manager->GetFocus(&document);
  if (FAILED(hr)) return {FocusStage::QueryFailed, hr};
  if (hr != S_OK || !document) return {FocusStage::NoDocument, hr};
  CComPtr<ITfContext> context;
  hr = document->GetTop(&context);
  if (hr != S_OK) return {FocusStage::TopFailed, hr};
  if (!context) return {FocusStage::NoTop, hr};
  if (!SameIdentity(context, target)) return {FocusStage::DifferentContext, S_FALSE};
  if (checkWindow) {
    CComPtr<ITfContextView> view;
    hr = target->GetActiveView(&view);
    if (hr != S_OK || !view) return {FocusStage::ViewFailed, hr != S_OK ? hr : E_FAIL};
    HWND host = nullptr;
    hr = view->GetWnd(&host);
    if (hr != S_OK || !host) return {FocusStage::WindowFailed, hr != S_OK ? hr : E_FAIL};
    const HWND root = GetAncestor(host, GA_ROOT);
    if (!root || !foreground || root != GetAncestor(foreground, GA_ROOT))
      return {FocusStage::DifferentWindow, S_FALSE};
  }
  return {};
}
}  // namespace correction
