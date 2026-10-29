#pragma once
#include <windows.h>
#include <initializer_list>
#include "resource.h"

namespace correction {
inline bool CorrectionMenuTargetsForeground(HWND host, HWND foreground) {
  if (!host || !foreground) return false;
  const auto root = GetAncestor(host, GA_ROOT);
  return root && root == GetAncestor(foreground, GA_ROOT);
}
// Both native right-click and TSF menus must refresh the same live state.
inline void UpdateCorrectionMenuState(HMENU menu, bool inputOnly, bool paused, bool foreground = true) {
  if (!menu) return;
  CheckMenuItem(menu, ID_WEASELTRAY_CORRECTION_PAUSE,
      MF_BYCOMMAND | (paused || inputOnly ? MF_CHECKED : MF_UNCHECKED));
  EnableMenuItem(menu, ID_WEASELTRAY_CORRECTION_PAUSE,
      MF_BYCOMMAND | (inputOnly || !foreground ? MF_GRAYED : MF_ENABLED));
}
}  // namespace correction
