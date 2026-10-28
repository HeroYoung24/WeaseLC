#pragma once
#include <windows.h>
#include <oleacc.h>
#pragma comment(lib, "oleacc.lib")

namespace correction {
// Reuse Windows' standard accessible HWND object. Annotate its existing name
// (the window title) as a read-only tooltip; no second suggestion text store.
inline bool AnnotateCorrectionHint(HWND window) {
  IAccPropServices* properties = nullptr;
  if (CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&properties)) != S_OK) return false;
  VARIANT role{}; role.vt = VT_I4; role.lVal = ROLE_SYSTEM_TOOLTIP;
  VARIANT state{}; state.vt = VT_I4;
  state.lVal = STATE_SYSTEM_READONLY | (IsWindowVisible(window) ? 0 : STATE_SYSTEM_INVISIBLE);
  const bool valid = properties->SetHwndProp(window, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_ROLE, role) == S_OK &&
      properties->SetHwndProp(window, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_STATE, state) == S_OK;
  properties->Release(); return valid;
}
inline void ClearCorrectionAccessibility(HWND window) {
  IAccPropServices* properties = nullptr;
  if (CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&properties)) == S_OK) {
    const MSAAPROPID ids[]{PROPID_ACC_ROLE, PROPID_ACC_STATE};
    properties->ClearHwndProps(window, OBJID_CLIENT, CHILDID_SELF, ids, _countof(ids));
    properties->Release();
  }
}
inline void AnnounceCorrectionHint(HWND window) {
  if (IsWindowVisible(window)) NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
}
}  // namespace correction
