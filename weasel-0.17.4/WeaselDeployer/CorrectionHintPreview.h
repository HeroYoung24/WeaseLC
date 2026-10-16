#pragma once
#include <CorrectionAutomatic.h>
#include <CorrectionHintView.h>

// Explicit settings preview. Uses only compiled sample strings, owns no model,
// text-service state, keyboard sink or application document.
class CorrectionHintPreview {
 public:
  CorrectionHintPreview() = default;
  CorrectionHintPreview(const CorrectionHintPreview&) = delete;
  CorrectionHintPreview& operator=(const CorrectionHintPreview&) = delete;
  ~CorrectionHintPreview() { Close(); }
  void Close() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    UnregisterClassW(kClass, GetModuleHandleW(nullptr));
  }
  void Hide() { if (window_) ShowWindow(window_, SW_HIDE); }
  HWND Window() const { return window_; }
  bool Show(HWND owner, const correction::AutomaticOptions& options, bool chinese, bool visible) {
    options_ = options; chinese_ = chinese;
    if (!window_) {
      WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = kClass;
      if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
      window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
          kClass, chinese ? L"纠错提示预览（固定示例）" : L"Correction preview (fixed sample)", WS_POPUP,
          0, 0, 0, 0, owner, nullptr, wc.hInstance, this);
      if (!window_ || !SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA)) { Close(); return false; }
    }
    RECT anchor{}; GetWindowRect(owner, &anchor);
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    const UINT dpi = correction::AutomaticWindowDpi(owner);
    const LONG width = (std::min)(static_cast<LONG>(MulDiv(options.width, dpi, 96)), monitor.rcWork.right - monitor.rcWork.left);
    HDC dc = GetDC(window_);
    const auto layout = correction::MeasureHint(dc, width, dpi, options.font, L"错子", L"错字", options.tab, chinese, true);
    ReleaseDC(window_, dc);
    const RECT placed = correction::PlaceAutomaticHint(anchor, monitor.rcWork, SIZE{width, layout.height}, options.side, MulDiv(options.gap, dpi, 96));
    if (!layout.complete || IsRectEmpty(&placed) || placed.bottom - placed.top < layout.height) { Hide(); return false; }
    SetWindowPos(window_, HWND_TOP, placed.left, placed.top, placed.right - placed.left, placed.bottom - placed.top,
        SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : 0));
    InvalidateRect(window_, nullptr, TRUE); return true;
  }
 private:
  static constexpr const wchar_t* kClass = L"WeaseLC.SettingsHintPreview.V1";
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<CorrectionHintPreview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<CorrectionHintPreview*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w, l);
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
      PAINTSTRUCT ps{};
      const HDC dc = message == WM_PAINT ? BeginPaint(window, &ps) : reinterpret_cast<HDC>(w);
      RECT rect{}; GetClientRect(window, &rect);
      correction::PaintHint(dc, rect, correction::AutomaticWindowDpi(window), self->options_.font,
          L"错子", L"错字", self->options_.tab, self->chinese_, true);
      if (message == WM_PAINT) EndPaint(window, &ps);
      return 0;
    }
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return DefWindowProcW(window, message, w, l);
  }
  HWND window_ = nullptr;
  correction::AutomaticOptions options_;
  bool chinese_ = true;
};
