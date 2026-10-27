// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

void KeepSingleLine(HWND edit);
void AllowLineBreaks(HWND edit);
// suppresses control character beep
void AddSelectAllShortcut(HWND window);

} // namespace win32ui
