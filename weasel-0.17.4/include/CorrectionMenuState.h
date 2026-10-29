#pragma once
#include <windows.h>
#include <initializer_list>
#include "resource.h"

namespace correction {
// Both native right-click and TSF menus must refresh the same live state.
inline void UpdateCorrectionMenuState(HMENU menu, bool inputOnly, bool paused, bool canFeedback) {
  if (!menu) return;
  CheckMenuItem(menu, ID_WEASELTRAY_CORRECTION_PAUSE,
      MF_BYCOMMAND | (paused || inputOnly ? MF_CHECKED : MF_UNCHECKED));
  EnableMenuItem(menu, ID_WEASELTRAY_CORRECTION_PAUSE,
      MF_BYCOMMAND | (inputOnly ? MF_GRAYED : MF_ENABLED));
  for (const auto id : {ID_WEASELTRAY_CORRECTION_KEEP, ID_WEASELTRAY_CORRECTION_FEEDBACK})
    EnableMenuItem(menu, id, MF_BYCOMMAND | (canFeedback && !inputOnly && !paused ? MF_ENABLED : MF_GRAYED));
}
}  // namespace correction
