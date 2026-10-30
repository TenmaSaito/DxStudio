#pragma once

// Unity-style Console window: shows dxs::Debug log entries with level icons,
// per-level filters with counts, Collapse and Clear.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace dxs {

// Creates the console as a child of `hParent`. Resize it with MoveWindow.
HWND CreateConsolePanel(HWND hParent, HINSTANCE hInstance);
void ClearConsolePanel(HWND hPanel);

} // namespace dxs
