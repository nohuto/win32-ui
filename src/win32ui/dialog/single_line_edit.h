// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// multiline edit without line breaks, discards typed breaks and replaces pasted breaks with spaces
void KeepSingleLine(HWND edit);
void AllowLineBreaks(HWND edit);
// Ctrl+A selects all in an edit or rich edit, or all edit descendants of window
// suppresses the control character beep, repeated calls have no effect
void AddSelectAllShortcut(HWND window);

} // namespace win32ui
