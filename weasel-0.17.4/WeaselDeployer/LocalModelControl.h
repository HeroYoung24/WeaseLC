#pragma once
#include <windows.h>
#include <functional>
int ShowLocalModelControl(HINSTANCE);
bool CheckLocalModelControl(HINSTANCE, const std::function<void(HWND)>& preview = {});
bool CheckOwnedLocalModel();
