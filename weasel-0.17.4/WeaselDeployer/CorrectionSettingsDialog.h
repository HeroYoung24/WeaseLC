#pragma once
#include <windows.h>

INT_PTR ShowCorrectionSettings(HINSTANCE instance, bool offlineDemo = false);
// Creates the real resource/dialog off screen, sends UI notifications, and
// exercises close/cancel without persistence, network or IME activation.
bool CheckCorrectionDialogResources(HINSTANCE instance);
// Explicit diagnostic: fixed synthetic text, loopback model alias only; no save.
bool CheckCorrectionLocalDialog(HINSTANCE instance);
